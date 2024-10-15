// Blynk configuration
#define BLYNK_TEMPLATE_ID "TMPL39JXb5Jw8"
#define BLYNK_TEMPLATE_NAME "lightsRelay"
#define BLYNK_AUTH_TOKEN "NtWkPSojUQ1pM1IDSfpbhjaFMwhjwzg6" // Enter your Blynk auth token

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Streaming.h"
#include <Ticker.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <time.h>  // Include time library for NTP
#include <Dusk2Dawn.h> // Include Dusk2Dawn library for astronomical calculations
#include <ESP8266WebServer.h> // Include WebServer library
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h> // Library for time functions

#define DEBUG
#define L1 2   // Output pin to control Lamp state
#define RELAY 4 // Relay pin

// WiFi configuration
const char* ssid = "Airtel_2348";           // SSID 
const char* password = "air94250";        // Password

const int ID = ESP.getChipId();              // Device ID

// MQTT broker information
const char* mqttServer = "test.mosquitto.org";   // Server
const char* mqttUser = "";              // User
const char* mqttPassword = "";          // Password
const int mqttPort = 1883;              // Port
const char* mqttTopicSub = "IVA/lamp/OnOff/cmd";  // Topic
const int CONNECTION_WAIT_TIME = 100;   // How long ESP8266 should wait before next attempt to connect to WiFi or MQTT Broker

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);  // Create a web server on port 80

// Define the pins to use for SDA and SCL
int SCL_pin = 5;   //D1 -> GPIO 5  
int SDA_pin = 13;  //D2 -> GPIO 4 

RTC_DS3231 rtc;

String onTime = "";
String offTime = "";

// NTP time configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;   // Set GMT offset for your timezone (e.g., 19800 for India Standard Time - GMT+5:30)
const int daylightOffset_sec = 0;   // No daylight saving time

// Example: location (latitude, longitude) and UTC offset
#define LATITUDE 28.6139
#define LONGITUDE 77.2090
#define UTC_OFFSET +5.5

Dusk2Dawn myLocation(LATITUDE, LONGITUDE, UTC_OFFSET);
int sunriseTime, sunsetTime;
String sunriseStr = "";
String sunsetStr = "";

char auth[] = BLYNK_AUTH_TOKEN;   
BlynkTimer timer;

// Variables to store time input
int startHour = 0, startMinute = 0, stopHour = 0, stopMinute = 0;

// Blynk Virtual Pins
#define START_HOUR_PIN V1
#define START_MINUTE_PIN V2
#define STOP_HOUR_PIN V3
#define STOP_MINUTE_PIN V4
#define MANUAL_RELAY_PIN V0

BLYNK_WRITE(START_HOUR_PIN) { startHour = param.asInt(); } // Start Hour
BLYNK_WRITE(START_MINUTE_PIN) { startMinute = param.asInt(); } // Start Minute
BLYNK_WRITE(STOP_HOUR_PIN) { stopHour = param.asInt(); } // Stop Hour
BLYNK_WRITE(STOP_MINUTE_PIN) { stopMinute = param.asInt(); } // Stop Minute

// Manual relay control
BLYNK_WRITE(MANUAL_RELAY_PIN) {
  bool value1 = param.asInt();
  if (value1 == 1) {
    digitalWrite(RELAY, HIGH);  // Turn relay ON
    digitalWrite(L1, LOW);  // Turn on the Lamp
  } else {
    digitalWrite(RELAY, LOW); // Turn relay OFF
    digitalWrite(L1, HIGH);  // Turn off the Lamp
  }
}

// void controlRelay() {
//   int currentHour = hour();
//   int currentMinute = minute();

//   if ((currentHour == startHour && currentMinute >= startMinute) || 
//       (currentHour > startHour && currentHour < stopHour) || 
//       (currentHour == stopHour && currentMinute <= stopMinute)) {
//     digitalWrite(RELAY, HIGH); // Turn relay ON
//     digitalWrite(L1, LOW);  // Turn on the Lamp
//   } else {
//     digitalWrite(RELAY, LOW); // Turn relay OFF
//     digitalWrite(L1, HIGH);  // Turn off the Lamp
//   }
// }

// Function to control the relay based on Blynk time input
void controlRelay() {
  int currentHour = hour();
  int currentMinute = minute();

  // Turn relay ON when current time matches the start time
  if (currentHour == startHour && currentMinute == startMinute) {
    digitalWrite(RELAY, HIGH); // Turn relay ON
    digitalWrite(L1, LOW);  // Turn on the Lamp
  }
  // Turn relay OFF when current time matches the stop time
  else if (currentHour == stopHour && currentMinute == stopMinute) {
    digitalWrite(RELAY, LOW); // Turn relay OFF
    digitalWrite(L1, HIGH);  // Turn off the Lamp
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(L1, OUTPUT);
  pinMode(RELAY, OUTPUT);

  digitalWrite(L1, HIGH); // Initialize LED to OFF state
  digitalWrite(RELAY, LOW); // Initialize Relay to OFF state

  Serial << " Device ID: " << ID << endl;
  Serial << " MqttTopic: " << mqttTopicSub << endl << endl;

  connectToWiFi();
  connectToMQTT();

  // Initialize the Wire library with custom I2C pins
  Wire.begin(SDA_pin, SCL_pin);

  // Initialize the RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1) delay(10);
  }
  
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize NTP and set time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  syncRTCWithNTP();

  // Get current time from RTC
  DateTime now = rtc.now();

  // Calculate sunrise and sunset times
  sunriseTime = myLocation.sunrise(now.year(), now.month(), now.day(), false);  // false indicates local time
  sunsetTime = myLocation.sunset(now.year(), now.month(), now.day(), false);    // false indicates local time

  // Convert sunriseTime and sunsetTime from minutes to HH:MM format
  sunriseStr = String(sunriseTime / 60) + ":" + String(sunriseTime % 60);
  sunsetStr = String(sunsetTime / 60) + ":" + String(sunsetTime % 60);

  Serial.println("Sunrise Time: " + sunriseStr);
  Serial.println("Sunset Time: " + sunsetStr);

   // Start the web server
  server.on("/", handleRoot);  // Route for the root web page
  server.on("/toggle", handleRelayToggle);  // Route to toggle the relay
  server.begin();
  Serial.println("Web server started");

  // Initialize the Blynk library
  Blynk.begin(auth, ssid, password, "blynk.cloud", 80);

  // Set up a timer to check the relay state every minute
  timer.setInterval(60000L, controlRelay);
}

/* Sync RTC with NTP */
void syncRTCWithNTP() {
  Serial.print("Syncing time with NTP server...");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time from NTP server");
    return;
  }
  Serial.println("Time synced!");

  DateTime ntpTime = DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  rtc.adjust(ntpTime);  // Adjust RTC to NTP time
}

/* Connect to WiFi */
void connectToWiFi() {
  Serial << "Connecting to WiFi";
  WiFi.begin(ssid, password);

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 30) { // Attempt connection 30 times
    delay(CONNECTION_WAIT_TIME);
    Serial << ".";
    attempt++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial << " - Connected to WiFi: " << ssid << endl;
    Serial << " - IP address: " << WiFi.localIP() << endl;
  } else {
    Serial.println("Failed to connect to WiFi");
  }
}

/* Connect to MQTT */
void connectToMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  while (!client.connected()) {
    Serial << endl << " Connecting to MQTT Broker";
    if (client.connect(String("Sonoff (ID: " + String(ID) + ")").c_str(), mqttUser, mqttPassword)) {
      Serial.println();
      Serial << " - Connected to MQTT Broker: " << mqttServer << ":" << mqttPort << endl;
      client.subscribe(mqttTopicSub);
      Serial << " - Subscribed to topic: " << mqttTopicSub << endl;
    } else {
      Serial << ".";
      delay(CONNECTION_WAIT_TIME / 2);
    }
  }
}

/* MQTT Callback */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received message: [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert the payload into a String
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print(message);
  Serial.println();

  // Deserialize the JSON message
  StaticJsonDocument<200> doc;
  deserializeJson(doc, message);

  // Check the lamp control state
  if (doc["state"].isNull() == false) {
    String state = doc["state"].as<String>();
    if (state == "on") {
      digitalWrite(L1, LOW);  // Turn on the Lamp
      digitalWrite(RELAY, HIGH);  // Turn relay ON
      Serial.println("Lamp turned ON");
    } else if (state == "off") {
      digitalWrite(L1, HIGH);  // Turn off the Lamp
      digitalWrite(RELAY, LOW); // Turn relay OFF
      Serial.println("Lamp turned OFF");
    }
  }

  // Handle onTime and offTime received via MQTT
  if (doc["onTime"].isNull() == false) {
    onTime = doc["onTime"].as<String>();
    Serial.print("Received onTime: ");
    Serial.println(onTime);
  }
  
  if (doc["offTime"].isNull() == false) {
    offTime = doc["offTime"].as<String>();
    Serial.print("Received offTime: ");
    Serial.println(offTime);
  }
}

/* Handle root page request */
void handleRoot() {
  String html = "<html><body><h1>WiFi Lamp Control</h1>";
  html += "<p>Current Time: " + String(hour()) + ":" + String(minute()) + ":" + String(second()) + "</p>";
  html += "<p>Sunrise: " + sunriseStr + "</p>";
  html += "<p>Sunset: " + sunsetStr + "</p>";
  html += "<p>Control the lamp manually:</p>";
  html += "<form action='/toggle' method='POST'><input type='submit' value='Toggle Lamp'></form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

/* Handle relay toggle request */
void handleRelayToggle() {
  if (digitalRead(L1) == HIGH) {
    digitalWrite(L1, LOW);  // Turn on the Lamp
    digitalWrite(RELAY, HIGH);  // Turn relay ON
  } else {
    digitalWrite(L1, HIGH);  // Turn off the Lamp
    digitalWrite(RELAY, LOW); // Turn relay OFF
  }
  server.send(200, "text/plain", "Lamp state toggled");
}

void loop() {
  client.loop();
  server.handleClient();  // Handle incoming web server requests
  Blynk.run();  // Run Blynk
  timer.run();  // Run Blynk timer

  // Regularly sync RTC with NTP server (e.g., every 1 hour)
  static unsigned long lastSyncTime = 0;
  if (millis() - lastSyncTime > 3600000) {
    syncRTCWithNTP();
    lastSyncTime = millis();
  }

  // Control lamp using onTime and offTime from MQTT or Blynk
  DateTime now = rtc.now();
  String currentTime = String(now.hour()) + ":" + String(now.minute());
  if (onTime == currentTime || currentTime == (String(startHour) + ":" + String(startMinute))) {
    digitalWrite(L1, LOW);  // Turn on the Lamp
    digitalWrite(RELAY, HIGH);  // Turn relay ON
    Serial.println("Lamp turned ON by schedule");
  } else if (offTime == currentTime || currentTime == (String(stopHour) + ":" + String(stopMinute))) {
    digitalWrite(L1, HIGH);  // Turn off the Lamp
    digitalWrite(RELAY, LOW);  // Turn relay OFF
    Serial.println("Lamp turned OFF by schedule");
  }

  // Check if current time matches sunrise or sunset time
  if (String(currentTime) == sunsetStr) {
    digitalWrite(RELAY, HIGH);
    digitalWrite(L1, LOW);  // Turn on the Lamp at sunset
    Serial.println("Lamp turned ON at sunset");
  }
  if (String(currentTime) == sunriseStr) {
    digitalWrite(RELAY, LOW);
    digitalWrite(L1, HIGH);  // Turn off the Lamp at sunrise
    Serial.println("Lamp turned OFF at sunrise");
  }
}