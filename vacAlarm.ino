#include <Arduino.h>
#include <ESP8266WiFi.h>
// #include <ESP8266HTTPClient.h>
// #include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266Ping.h>
#include "secrets.h"

// #define PCBLED D0 // 16 , LED_BUILTIN
#define ESPLED D4 // 2
#define ANLG_IN A0
#define PIR_IN D2


char defaultSSID[] = WIFI_DEFAULT_SSID;
char defaultPASS[] = WIFI_DEFAULT_PASS;

char apiKey[] = THINGSP_WR_APIKEY;
char autoRemoteMac[] = AUTOREM_MAC;
char autoRemotePlus6[] = AUTOREM_PLUS6;
char autoRemotePass[] = AUTOREM_PASS;
char autoRemoteTTL[] = "15";

char otaAuthPin[] = OTA_AUTH_PIN;

// ~~~~ Constants and variables
String httpHeader;
String serverReply;
String localIPaddress;
String formatedTime;

bool movement = false;
bool tempMove = false;
int analogValue = 0;
int analogThreshold = 50; 

bool noAuRe_ThSp = false;       // for debugging
bool pingResult = true;
bool wifiAvailable = false;
bool connectionLost = false;
bool allowLightAlarm = true;
bool allowMovementAlarm = true;

unsigned long connectionLostTime = 0;
unsigned long movementAlarmDebounce = 30000;

unsigned long lastPingTime = 0;
unsigned long lastThingSpeakTime = 0;
unsigned long lastNTPpullTime = 0;
unsigned long lastMovementMillis = 0;
unsigned long lastSensorsTime = 0;

const int uploadInterval = 15000;
const int sensorsInterval = 250;
const int pingInteval = 60000;
const int ntpInterval = 2000;

const char* thinkSpeakAPIurl = "api.thingspeak.com"; // "184.106.153.149" or api.thingspeak.com
const char* autoRemoteURL = "autoremotejoaomgcd.appspot.com";

// Network Time Protocol
// 1H (3600) for winter time / 2H (7200) for summer time (CH)
// 1H (7200) for winter time / 2H (10800) for summer time (GR)
const long utcOffsetInSeconds = 10800;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

ESP8266WebServer server(80);
WiFiClient client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);


void setup() {
    // pinMode(PCBLED, OUTPUT);
    pinMode(ESPLED, OUTPUT);
    pinMode(PIR_IN, INPUT);

    // digitalWrite(PCBLED, HIGH);
    digitalWrite(ESPLED, HIGH);

    // attachInterrupt(digitalPinToInterrupt(PIR_IN), movementDetected, CHANGE); // LOW

    Serial.begin(115200);
    delay(100);

    WiFiManager wifiManager;
    // wifiManager.resetSettings();
    wifiManager.setConfigPortalTimeout(120);
    wifiManager.autoConnect(defaultSSID, defaultPASS);

    server.on("/", handle_OnConnect);
    server.onNotFound(handle_NotFound);

    server.begin();
    Serial.println("HTTP server starter on port 80.");

    timeClient.begin();

    handleOTA();
    delay(100);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        wifiAvailable = false;
        Serial.println("Failed to connect on WiFi network!");
        Serial.println("Operating offline.");

        // ESP8266WebServer server(8080);
        // server.begin();
        // Serial.println("\r\nHTTP server starter on port 8080.");
    }
    else {
        wifiAvailable = true;
        Serial.println("Connected to WiFi.");
        Serial.print("IP: ");
        localIPaddress = (WiFi.localIP()).toString();
        Serial.println(localIPaddress);

        // server.begin();
        // Serial.println("\r\nHTTP server starter on port 80.");
    }

    delay(5000);
}

void handleOTA() {
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname("vacAlarm");

    ArduinoOTA.setPassword((const char *)otaAuthPin);

    ArduinoOTA.onStart([]() {
        Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

bool pingStatus() {
    IPAddress ipThingSpeak (184, 106, 153, 149);
    IPAddress ipGoogle (8, 8, 8, 8);

    bool pingRet;    
    pingRet = Ping.ping(ipThingSpeak);

    if (!pingRet) {
        pingRet = Ping.ping(ipGoogle);
    }

    lastPingTime = millis();
    return pingRet;
}

void sendToAutoRemote(char message[], char deviceKey[], char password[]) {
    if (wifiAvailable && !noAuRe_ThSp) {
        digitalWrite(ESPLED, LOW);
        client.stop();
        if (client.connect(autoRemoteURL,80)) {
            String url = "/sendmessage?key=";
            url += (String)deviceKey;
            url += "&message=";
            url += (String)message;
            url += "&sender=";
            url += "vacAlarm";
            url += "&password=";
            url += (String)password;
            url += "&ttl=";
            url += (String)autoRemoteTTL;

            client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                    "Host: " + autoRemoteURL + "\r\n" +
                    "Connection: close\r\n\r\n");
            // Timeout 5 sec
            unsigned long timeout = millis();
            while (client.available() == 0) {
                if (millis() - timeout > 5000) {
                    client.stop();
                    Serial.println("ERROR: could not send message to AutoRemote!");
                    return;
                }
            }

            while (client.available()) {
                serverReply = client.readStringUntil('\r');
            }
            
            serverReply.trim();
            client.stop();
            // Serial.println("Message sent to AutoRemote!");
        }
        else {
            Serial.println("ERROR: could not send data to AutoRemote!");
        }
        digitalWrite(ESPLED, HIGH);
    }
}

void thingSpeakRequest(int lightLevel, bool movementStatus) {
    if (wifiAvailable && !noAuRe_ThSp) {
        digitalWrite(ESPLED, LOW);
        client.stop();
        if (client.connect(thinkSpeakAPIurl,80)) 
        {
            String postStr = apiKey;
            postStr +="&field1=";
            postStr += String(lightLevel);
            postStr +="&field2=";
            postStr += String(movementStatus);
            postStr += "\r\n\r\n";

            client.print("POST /update HTTP/1.1\n");
            client.print("Host: api.thingspeak.com\n");
            client.print("Connection: close\n");
            client.print("X-THINGSPEAKAPIKEY: " + (String)apiKey + "\n");
            client.print("Content-Type: application/x-www-form-urlencoded\n");
            client.print("Content-Length: ");
            client.print(postStr.length());
            client.print("\n\n");
            client.print(postStr);
            client.stop();
            // Serial.println("Data uploaded to thingspeak!");
        }
        else {
            Serial.println("ERROR: could not upload data to thingspeak!");
        }
        digitalWrite(ESPLED, HIGH);
    }
    lastThingSpeakTime = millis();
}

void handle_OnConnect() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTMLpresentData(analogValue, movement));
    digitalWrite(ESPLED, HIGH);
}

void handle_NotFound(){
    server.send(404, "text/html", HTMLnotFound());
}

String HTMLpresentData(int lightLvl, bool movementStatus){
    String ptr = "<!DOCTYPE html> <html>\n";
    ptr +="<meta http-equiv=\"refresh\" content=\"5\" >\n";
    ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    ptr +="<title>vacAlarm</title>\n";
    ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
    ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
    ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
    ptr +="</style>\n";
    ptr +="</head>\n";
    ptr +="<body>\n";
    ptr +="<div id=\"webpage\">\n";
    ptr +="<h1>vacAlarm</h1>\n";
    
    ptr +="<p><b>Local IP:</b> ";
    ptr += (String)localIPaddress;
    ptr +="</p>";

    ptr +="<p><b>Light level:</b> ";
    ptr +=(String)lightLvl;
    ptr +=" [0-1024]";
    ptr +="<p><b>Movement:</b> ";
    ptr +=(String)movementStatus;
    ptr +=" [0/1]</p>";
    ptr += "<p><b>Timestamp:</b> ";
    ptr +=(String)formatedTime;
    ptr += "</p>";
    ptr +="<p></p>";
    ptr +="<p><i><b>Light level threshold:</b> ";
    ptr +=(String)analogThreshold;
    ptr += "</i></p>";
    
    ptr +="</div>\n";
    ptr +="</body>\n";
    ptr +="</html>\n";
    return ptr;
}

String HTMLnotFound(){
    String ptr = "<!DOCTYPE html> <html>\n";
    ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    ptr +="<title>RJD Monitor</title>\n";
    ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
    ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
    ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
    ptr +="</style>\n";
    ptr +="</head>\n";
    ptr +="<body>\n";
    ptr +="<div id=\"webpage\">\n";
    ptr +="<h1>You know this 404 thing ?</h1>\n";
    ptr +="<p>What you asked can not be found... :'( </p>";
    ptr +="</div>\n";
    ptr +="</body>\n";
    ptr +="</html>\n";
    return ptr;
}

void pullNTPtime(bool printData) {
    if (wifiAvailable) {
        timeClient.update();
        formatedTime = timeClient.getFormattedTime();

        if (printData) {
            // Serial.print(daysOfTheWeek[timeClient.getDay()]);
            // Serial.print(", ");
            // Serial.print(timeClient.getHours());
            // Serial.print(":");
            // Serial.print(timeClient.getMinutes());
            // Serial.print(":");
            // Serial.println(timeClient.getSeconds());
            Serial.println(timeClient.getFormattedTime()); // format time like 23:05:00
        }
        lastNTPpullTime = millis();
    }
}

void serialPrintAll(int lightLevel, bool movementStatus) {
    Serial.println(timeClient.getFormattedTime());
    Serial.print("Light level: ");
    Serial.print(String(lightLevel));
    Serial.println(" [0-1024]");
    Serial.print("Movement: ");
    Serial.print(String(movementStatus));
    Serial.println(" [0/1]");
    // Serial.println(" ms");
    Serial.println();
}

void loop(){
    ArduinoOTA.handle();
    server.handleClient();

    // read sensors
    if (millis() > lastSensorsTime + sensorsInterval) {
        analogValue = analogRead(ANLG_IN);
        analogValue = map(analogValue, 0, 1024, 1024, 0);
        movement = digitalRead(PIR_IN);
        if (movement) {
            tempMove = true;
        }
        lastSensorsTime = millis();
    }

    // PIR led handler
    // digitalWrite(PCBLED, !movement);

    // AutoRemote LIGHT
    if ((analogValue > analogThreshold) && allowLightAlarm) {
        Serial.print("WARNING: light detected! (");
        Serial.print(analogValue);
        Serial.println("/1024)\r\n");
        sendToAutoRemote("alarm_light", autoRemotePlus6, autoRemotePass);
        allowLightAlarm = false;
    }
    if ((analogValue < analogThreshold) && !allowLightAlarm) {
        allowLightAlarm = true;
    }

    // AutoRemote MOVEMENT
    if (movement && allowMovementAlarm) {
        Serial.println("WARNING: movement detected!\r\n");
        sendToAutoRemote("alarm_movement", autoRemotePlus6, autoRemotePass);
        allowMovementAlarm = false;
        lastMovementMillis = millis();
    }
    if ((millis() >= (lastMovementMillis + movementAlarmDebounce)) && !allowMovementAlarm) {
        allowMovementAlarm = true;
    }

    // pull the time
    if ((millis() > lastNTPpullTime + ntpInterval) && wifiAvailable) {
        pullNTPtime(false);
    }

    // upload data to ThingSpeak
    if (millis() > lastThingSpeakTime + uploadInterval) {
        serialPrintAll(analogValue, tempMove);
        thingSpeakRequest(analogValue, tempMove);
        tempMove = false;
    }

    // check Internet connectivity
    if (millis() > lastPingTime + pingInteval) {
        pingResult = pingStatus();
        Serial.print("\r\nPing status: ");
        Serial.println((String)pingResult);
        Serial.println("\r\n");

        connectionLost = !pingResult;

        if ((!pingResult) && (!connectionLost)) {
            Serial.println("\r\nWARNING: no Internet connectivity!\r\n");
            connectionLostTime = millis();
            connectionLost = true;
        }
    }

    // reboot if no Internet
    if ((millis() > connectionLostTime + 300000) && connectionLost) {
        if (!pingResult) {
            Serial.println("No Internet connection. Rebooting in 2 sec...");
            delay(2000);
            ESP.restart();
        }
    }

    // reboot device if no WiFi for 5 minutes (1h : 3600000)
    if ((millis() > 300000) && (!wifiAvailable)) {
        Serial.println("No WiFi connection. Rebooting in 2 sec...");
        delay(2000);
        ESP.restart();
    }
}