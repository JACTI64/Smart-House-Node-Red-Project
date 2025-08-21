#include <WiFi.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <PubSubClient.h>

BluetoothSerial SerialBT;
Preferences preferences;

String mqtt_server;
const char* topic = "daylight_cycle";
const int pins[] = {13, 12, 27, 26, 25, 4};

WiFiClient espClient;
PubSubClient client(espClient);
String deviceIP;

void setup() {
    Serial.begin(115200);
    SerialBT.begin("ESP32_Config");
    preferences.begin("wifi", false);
    preferences.clear();

    for (int i = 0; i < 6; i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }

    Serial.println("Waiting for Wi-Fi credentials via Bluetooth...");
    SerialBT.println("Send SSID and DEVICE_IP as: SSID,DEVICE_IP");

    while (true) {
        if (SerialBT.available()) {
            String receivedData = SerialBT.readStringUntil('\n');
            receivedData.trim();

            Serial.print("\nRaw received data: ");
            Serial.println(receivedData);

            int commaIndex = receivedData.indexOf(',');

            if (commaIndex > 0) {
                String newSSID = receivedData.substring(0, commaIndex);
                deviceIP = receivedData.substring(commaIndex + 1);

                newSSID.replace("\"", "");
                deviceIP.replace("\"", "");

                Serial.println("Parsed Wi-Fi Credentials:");
                Serial.println("SSID: " + newSSID);
                Serial.println("Device IP: " + deviceIP);

                preferences.putString("ssid", newSSID);
                preferences.putString("device_ip", deviceIP);
                preferences.end();

                mqtt_server = deviceIP;
                WiFi.begin(newSSID.c_str());

                if (waitForWiFiConnect()) {
                    Serial.println("Wi-Fi Connected!");
                    setupMQTT();
                    break;
                } else {
                    Serial.println("Connection failed! Try again.");
                    SerialBT.println("Failed! Send credentials again.");
                }
            } else {
                Serial.println("Invalid data format. Expected SSID,DEVICE_IP");
                SerialBT.println("Invalid format. Send SSID,DEVICE_IP");
            }
        } else {
            Serial.print(".");
            delay(500);
        }
    }
}

void setupMQTT() {
    client.setServer(mqtt_server.c_str(), 1883);
    client.setCallback(callback);
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

void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.print("Message received: ");
    Serial.println(message);

    if (message == "true") {
        for (int i = 0; i < 6; i++) {
            digitalWrite(pins[i], HIGH);
            delay(1000);
            digitalWrite(pins[i], LOW);
        }
    } else if (message == "false") {
        for (int i = 0; i < 6; i++) {
            digitalWrite(pins[i], LOW);
        }
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP32Client")) {
            Serial.println("Connected!");
            client.subscribe(topic);
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" Trying again in 5 seconds...");
            delay(5000);
        }
    }
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
}
