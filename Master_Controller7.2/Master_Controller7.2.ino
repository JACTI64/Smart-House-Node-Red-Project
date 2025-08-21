#include <WiFi.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "Adafruit_MCP9808.h"
#include <Adafruit_INA260.h>

BluetoothSerial SerialBT;
Preferences preferences;
WiFiClient wifiClient;
PubSubClient client(wifiClient);

Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();
Adafruit_INA260 ina260 = Adafruit_INA260();

// MQTT credentials
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[32];
char mqtt_password[32];

// Wi-Fi credentials
char wifi_ssid[32];
char wifi_password[64];

// Pins
#define motorPin1 32
#define motorPin2 33
#define relayPin1 25
#define relayPin2 26

// Speed control flag
bool speedControlActive = false; // Tracks whether speed topic is active

void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_BT");
  preferences.begin("settings", false);

  // Load stored Wi-Fi and MQTT credentials
  preferences.getString("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
  preferences.getString("wifi_pass", wifi_password, sizeof(wifi_password));
  preferences.getString("mqtt_server", mqtt_server, sizeof(mqtt_server));
  preferences.getString("mqtt_port", mqtt_port, sizeof(mqtt_port));
  preferences.getString("mqtt_user", mqtt_user, sizeof(mqtt_user));
  preferences.getString("mqtt_pass", mqtt_password, sizeof(mqtt_password));

  WiFi.begin(wifi_ssid, wifi_password);

  // MQTT setup
  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);

  // Pin setup
  pinMode(motorPin1, OUTPUT);
  pinMode(motorPin2, OUTPUT);
  pinMode(relayPin1, OUTPUT);
  pinMode(relayPin2, OUTPUT);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Handle fan speed from topic "speed"
  if (String(topic) == "speed") {
    int fanLevel = message.toInt(); // Expecting 0 to 3
    fanLevel = constrain(fanLevel, 0, 3);
    int pwmValue = map(fanLevel, 0, 3, 0, 100);

    // Always write PWM value (0-100)
    analogWrite(motorPin1, pwmValue);
    analogWrite(motorPin2, pwmValue);

    // Track whether speed control is active
    speedControlActive = (fanLevel > 0);
    return;
  }

  // Handle AC relay if speed control not active
  if (String(topic) == "AC" && !speedControlActive) {
    digitalWrite(relayPin1, (message == "true") ? HIGH : LOW);
  }

  // Handle Ceiling Fans relay if speed control not active
  if (String(topic) == "Ceiling_Fans" && !speedControlActive) {
    digitalWrite(relayPin2, (message == "true") ? HIGH : LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      client.subscribe("speed");
      client.subscribe("AC");
      client.subscribe("Ceiling_Fans");
    } else {
      delay(5000);
    }
  }
}
