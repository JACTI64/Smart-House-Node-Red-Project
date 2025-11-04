//Redownload this file onto arduino
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
String mqtt_server;
String ssid;
String password;


// Topics and pin mappings
const char* topics[] = {
  "wash", "dryer",
  "LED_Bulbs1", "LED_Bulbs2", "LED_Bulbs3",
  "Smart_Devices1", "Smart_Devices2",
  "TVs1", "TVs2",
  "Dishwasher", "Oven",
  "Microwaves",
  "Ceiling_Fans",
  "Fridge", "AC"
};
const int pinMap[] = {12,13,25,26,27,32,33,2,4,18,19,5,23,14,15};
const int numTopics = sizeof(topics)/sizeof(topics[0]);

// PWM motor control
const int motorPin1 = 23;  // Ceiling
const int motorPin2 = 15;  // AC

int solar1Pin=34;
int solar2Pin=35;
double R=47;


// Sensors
Adafruit_INA260 ina260 = Adafruit_INA260();
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808(); // Temperature
const int I2C_SDA = 21;
const int I2C_SCL = 22;

// MQTT Topics
const char* current_topic = "current";
const char* voltage_topic = "voltage";
const char* power_topic = "power";
const char* temperature_topic = "temperature";

// Timing
unsigned long lastSensorPublishTime = 0;
unsigned long sensorPublishInterval = 15000; // 15 seconds

void connectWiFi(String ssid, String password);
bool waitForWiFiConnect();
void setupMQTT();
void connectMQTT();
void updateMotorPWM();

void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("]: ");
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);

    // Handle fan speed from topic "speed"
    if (String(topic) == "speed") {
        int fanLevel = message.toInt(); // Expecting 0 to 5
        fanLevel = constrain(fanLevel, 0, 3);
        int pwmValue = map(fanLevel, 0, 3, 0, 100); // PWM from 0 to 100
        analogWrite(motorPin1, pwmValue); // Apply PWM to motorPin1
        analogWrite(motorPin2, pwmValue); // Apply PWM to motorPin2
    } else {
        // Handle digital ON/OFF controls
        for (int i = 0; i < numTopics; i++) {
            if (String(topic) == topics[i]) {
                digitalWrite(pinMap[i], message == "1" ? HIGH : LOW);
                break;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);

    pinMode(solar1Pin, INPUT);
    pinMode(solar2Pin, INPUT);

    // PWM setup
    pinMode(motorPin1, OUTPUT);
    pinMode(motorPin2, OUTPUT);
    analogWrite(motorPin1, 0);
    analogWrite(motorPin2, 0); // Start fan off

    // Initialize sensors
    if (!ina260.begin()) {
    Serial.println("Failed to initialize INA260!");
    } else {
    Serial.println("INA260 initialized successfully.");
    }


    if (!tempsensor.begin(0x18)) {
        Serial.println("Failed to initialize MCP9808!");
    } else {
        Serial.println("MCP9808 initialized successfully.");
    }

    tempsensor.setResolution(3);

    SerialBT.begin("ESP32_Config");
    preferences.begin("wifi", false);
    // preferences.clear(); // Leave commented unless debugging

    Serial.println("Waiting for Wi-Fi credentials via Bluetooth...");
    SerialBT.println("Send credentials as: \"SSID\",\"Password\",\"MQTT_IP\"");

    while (true) {
        if (SerialBT.available()) {
            String received = SerialBT.readStringUntil('\n');
            received.trim();

            Serial.print("Received: ");
            Serial.println(received);

            int firstComma = received.indexOf(',');
            int secondComma = received.indexOf(',', firstComma + 1);
            int thirdComma = received.indexOf(',', secondComma + 1);

            if (firstComma > 0 && secondComma > firstComma && thirdComma == -1) {
                String newSSID = received.substring(0, firstComma);
                String newPassword = received.substring(firstComma + 1, secondComma);
                String deviceIP = received.substring(secondComma + 1);

                newSSID.replace("\"", "");
                newPassword.replace("\"", "");
                deviceIP.replace("\"", "");

                Serial.print("Received MQTT Server IP: ");
                Serial.println(deviceIP);

                preferences.putString("ssid", newSSID);
                preferences.putString("password", newPassword);
                preferences.putString("device_ip", deviceIP);
                preferences.end();

                mqtt_server = deviceIP;

                Serial.print("Attempting to connect to SSID: ");
                Serial.println(ssid);

                connectWiFi(newSSID, newPassword);

                if (waitForWiFiConnect()) {
                Serial.println("Wi-Fi Connected!");
                Serial.print("ESP32 IP: ");
                Serial.println(WiFi.localIP());
                SerialBT.println("Connected successfully!");

                setupMQTT();
                connectMQTT();  // Force MQTT connect
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


    for (int i = 0; i < numTopics; i++) {
        pinMode(pinMap[i], OUTPUT);
        digitalWrite(pinMap[i], LOW);
    }
}

void loop() {
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();

    // Solar power calculation
    double solarV = analogRead(solar1Pin);       // in raw ADC units
    double volt_after = analogRead(solar2Pin);    // in raw ADC units
    double volt_diff = solarV - volt_after;
    double solarC = volt_diff / R;               // in mA
    double solarPower = solarV * solarC;        // in uW
    solarPower = (solarPower / 1000) + 0.5;       // convert to mW
    unsigned long currentTime = millis();

    // Sensor data publishing
    if (currentTime - lastSensorPublishTime >= sensorPublishInterval) {
        float current = ina260.readCurrent();            // mA
        float voltage = ina260.readBusVoltage();         // mV
        float power = ina260.readPower() / 1000;          // mW
        float temperature = tempsensor.readTempF();      // °F

        if (client.connected()) {
            client.publish(current_topic, String(current).c_str());
            client.publish(voltage_topic, String(voltage).c_str());
            client.publish(power_topic, String(power).c_str());

            client.publish("solarC", String(solarC).c_str());
            client.publish("solarV", String(solarV).c_str()); 
            client.publish("solar", String(solarPower).c_str());
            client.publish(temperature_topic, String(temperature).c_str());
        }

        lastSensorPublishTime = currentTime;
    }
}


void connectWiFi(String ssid, String password) {
  Serial.println("=== Attempting Wi-Fi Connection ===");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password.length() > 0 ? password : "(no password)");

  if (password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }
}

bool waitForWiFiConnect() {
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(". Status: ");
    Serial.println(WiFi.status());
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
        if (client.connect("ESP32Client")) {
            Serial.println("Connected.");
            for (int i = 0; i < numTopics; i++) {
                client.subscribe(topics[i]);
            }
            client.subscribe("speed");
        } else {
            Serial.print("Failed. rc=");
            Serial.print(client.state());
            Serial.println(" Retrying in 5 seconds...");
            delay(5000);
        }
    }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ArduinoClient")) {
      client.subscribe("speed");
    } else {
      delay(5000);
    }
  }
}
