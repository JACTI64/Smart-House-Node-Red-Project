//LED board B
#include <WiFi.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Arduino.h>

// ==== Bluetooth, Wi-Fi, MQTT ====
BluetoothSerial SerialBT;
Preferences preferences;
WiFiClient wifiClient;
PubSubClient client(wifiClient);

String mqtt_server;
String ssid;
String password;

void connectWiFi(String ssid, String password);
bool waitForWiFiConnect();
void setupMQTT();
void connectMQTT();
void reconnect();

// ==== LED Pins ====
const int ledPins[] = {15, 5, 19, 14, 26, 32, 2, 4, 27, 18, 25, 33};
const int numLeds = sizeof(ledPins) / sizeof(ledPins[0]);

// ==== Fade Config ====
const int fadeSteps = 32;          // More steps = smoother fade
const int brightnessLevel = 1000;  // Base unit for brightness timing (µs)

// Flag to start LED test when "start" is received
volatile bool runLedTest = false;

// ================= LED FUNCTIONS =================
void setBrightness(int pin, int brightness) {
  if (brightness <= 0) {
    digitalWrite(pin, LOW);
    delayMicroseconds(brightnessLevel);
  } else {
    int onTime = brightness * brightnessLevel / fadeSteps;
    int offTime = brightnessLevel - onTime;

    digitalWrite(pin, HIGH);
    delayMicroseconds(onTime);
    digitalWrite(pin, LOW);
    delayMicroseconds(offTime);
  }
}

void fadeInOut(int pin, int totalDurationMs) {
  int perStep = totalDurationMs / (2 * fadeSteps);

  for (int i = 0; i <= fadeSteps; i++) {
    for (int j = 0; j < perStep; j++) {
      setBrightness(pin, i);
      client.loop(); //  keep MQTT alive
    }
  }
  for (int i = fadeSteps; i >= 0; i--) {
    for (int j = 0; j < perStep; j++) {
      setBrightness(pin, i);
      client.loop(); // keep MQTT alive
    }
  }
  digitalWrite(pin, LOW);
  delay(5);
}

// ================= LED TEST SEQUENCE =================
void testSequence() {
  const int totalTestDuration = 27500;  // 27.5 seconds active
  int perLedDuration = totalTestDuration / numLeds; // ≈ 3333 ms per LED

  Serial.printf("Starting LED test: %d ms total, %d ms per LED\n", totalTestDuration, perLedDuration);

  for (int i = 0; i < numLeds; i++) {
    Serial.printf("Testing LED %d on pin %d...\n", i + 1, ledPins[i]);
    fadeInOut(ledPins[i], perLedDuration);
  }

  Serial.println("LED test complete! Total sequence = 1 minute.");
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  Serial.printf("Message arrived [%s]: %s\n", topic, message.c_str());

  if (String(topic) == "control" && message.equalsIgnoreCase("start")) {
    Serial.println("Start command received — beginning 1-minute sequence");
    runLedTest = true;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_Config");
  preferences.begin("wifi", false);

  for (int i = 0; i < numLeds; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  Serial.println("Waiting for Wi-Fi credentials via Bluetooth...");
  SerialBT.println("Send credentials as: \"SSID\",\"Password\",\"MQTT_IP\"");

  while (true) {
    if (SerialBT.available()) {
      String received = SerialBT.readStringUntil('\n');
      received.trim();

      int firstComma = received.indexOf(',');
      int secondComma = received.indexOf(',', firstComma + 1);

      if (firstComma > 0 && secondComma > firstComma) {
        String newSSID = received.substring(0, firstComma);
        String newPassword = received.substring(firstComma + 1, secondComma);
        String deviceIP = received.substring(secondComma + 1);

        newSSID.replace("\"", "");
        newPassword.replace("\"", "");
        deviceIP.replace("\"", "");

        preferences.putString("ssid", newSSID);
        preferences.putString("password", newPassword);
        preferences.putString("device_ip", deviceIP);

        mqtt_server = deviceIP;

        connectWiFi(newSSID, newPassword);

        if (waitForWiFiConnect()) {
          Serial.println("Wi-Fi Connected!");
          Serial.print("ESP32 IP: ");
          Serial.println(WiFi.localIP());
          SerialBT.println("Connected successfully!");

          setupMQTT();
          connectMQTT();
          break;
        } else {
          Serial.println("Wi-Fi connection failed.");
          SerialBT.println("Connection failed.");
        }
      } else {
        SerialBT.println("Invalid format. Use: \"SSID\",\"Password\",\"MQTT_IP\"");
      }
    }
    delay(500);
  }
}

// ================= MAIN LOOP =================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (runLedTest) {
    runLedTest = false; // prevent retriggering mid-sequence

    // === 1msecond delay phase ===
    Serial.println("Waiting 1m second before LED test...");
    unsigned long startWait = millis();
    while (millis() - startWait < 1) {
      client.loop(); // keep MQTT alive during delay
      delay(10);
    }

    // === 40-second LED test ===
    testSequence();
  }
}

// ================= NETWORK HELPERS =================
void connectWiFi(String ssid, String password) {
  WiFi.begin(ssid.c_str(), password.c_str());
}

bool waitForWiFiConnect() {
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void setupMQTT() {
  client.setServer(mqtt_server.c_str(), 1883);
  client.setCallback(callback);
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_LedBoard")) {
      Serial.println("Connected.");
      client.subscribe("control");
    } else {
      Serial.print("Failed. rc=");
      Serial.print(client.state());
      Serial.println(" retrying...");
      delay(5000);
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_LedBoard")) {
      client.subscribe("control");
    } else {
      delay(5000);
    }
  }
}
