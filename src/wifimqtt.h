#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include "Credential1.h"

// ===== RFID Configuration =====
const int SS_PIN = 26;
const int RST_PIN = 5;
MFRC522 rfid(SS_PIN, RST_PIN);

// ===== LED Configuration =====
const byte ledGreen = 21;
const byte ledRed = 22;
const byte led = 4; // From first code

// ===== MQTT Configuration =====
String clientID = "ESP32-";
const char *mqtt_server = "192.168.1.18";
const char *mqtt_user = "Melek";
const char *mqtt_password = "Melek2005";
WiFiClient espClient;
PubSubClient client(espClient);

// ===== Global Variables =====
bool checkIn = true;
unsigned long previousMillis = millis();
const unsigned long DHT_INTERVAL = 5000; // Send DHT data every 5 seconds

// ===== WiFi Connection =====
void connectAP()
{
    Serial.println("\nConnecting to WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);
    byte cnt = 0;

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(".");
        cnt++;
        if (cnt > 30)
        {
            Serial.println("\nWiFi connection timeout! Restarting...");
            ESP.restart();
        }
    }

    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

// ===== MQTT Reconnect =====
void reconnect()
{
    int attempts = 0;
    while (!client.connected() && attempts < 10)
    {
        Serial.print("Attempting MQTT connection to: ");
        Serial.print(mqtt_server);
        Serial.print(":1883 with user: ");
        Serial.println(mqtt_user);

        clientID = "ESP32-";
        clientID += String(random(0xffff), HEX);
        Serial.print("Client ID: ");
        Serial.println(clientID);

        if (client.connect(clientID.c_str(), mqtt_user, mqtt_password))
        {
            Serial.println("✓ Connected to MQTT!");
            client.subscribe("fromNodeRED");
            client.subscribe("LedState");
            client.subscribe("JSONfromNodeRED");
            client.subscribe("RFIDCommand");
            return;
        }
        else
        {
            int state = client.state();
            Serial.print("✗ Failed, code: ");
            Serial.println(state);

            switch (state)
            {
            case -4:
                Serial.println("MQTT_CONNECTION_TIMEOUT");
                break;
            case -3:
                Serial.println("MQTT_CONNECTION_LOST");
                break;
            case -2:
                Serial.println("MQTT_CONNECT_FAILED");
                break;
            case -1:
                Serial.println("MQTT_DISCONNECTED");
                break;
            case 0:
                Serial.println("Connected");
                break;
            case 1:
                Serial.println("Bad protocol version");
                break;
            case 2:
                Serial.println("Bad client id");
                break;
            case 3:
                Serial.println("Unavailable");
                break;
            case 4:
                Serial.println("Bad credentials");
                break;
            case 5:
                Serial.println("Unauthorized");
                break;
            }

            Serial.println("Retrying in 5 seconds...\n");
            delay(5000);
            attempts++;
        }
    }
    Serial.println("Failed to connect after multiple attempts");
}

// ===== MQTT Callback =====
void callback(char *topic, byte *message, unsigned int length)
{
    String messageTemp;
    for (int i = 0; i < length; i++)
    {
        messageTemp += (char)message[i];
    }

    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(" | Message: ");
    Serial.println(messageTemp);

    if (String(topic) == "fromNodeRED")
    {
        Serial.println("Received from Node-RED: " + messageTemp);
    }
    else if (String(topic) == "LedState")
    {
        if (messageTemp == "ON")
        {
            digitalWrite(led, HIGH);
            Serial.println("LED turned ON");
        }
        else if (messageTemp == "OFF")
        {
            digitalWrite(led, LOW);
            Serial.println("LED turned OFF");
        }
    }
    else if (String(topic) == "JSONfromNodeRED")
    {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, messageTemp);

        if (error)
        {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        const char *device = doc["device"];
        int threshold = doc["threshold"];

        Serial.println("=== JSON Data Received ===");
        Serial.print("Device: ");
        Serial.println(device);
        Serial.print("Threshold: ");
        Serial.println(threshold);
        Serial.println("========================\n");
    }
    else if (String(topic) == "RFIDCommand")
    {
        if (messageTemp == "ENABLE")
        {
            checkIn = true;
            digitalWrite(ledGreen, HIGH);
            Serial.println("RFID Check-in ENABLED");
        }
        else if (messageTemp == "DISABLE")
        {
            checkIn = false;
            digitalWrite(ledGreen, LOW);
            Serial.println("RFID Check-in DISABLED");
        }
    }
}

// ===== RFID Card Detection & Processing =====
void processRFIDCard()
{
    if (!rfid.PICC_IsNewCardPresent())
    {
        return;
    }
    if (!rfid.PICC_ReadCardSerial())
    {
        return;
    }

    // Read UID
    String content = "";
    for (byte i = 0; i < rfid.uid.size; i++)
    {
        content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
        content.concat(String(rfid.uid.uidByte[i], HEX));
    }
    content.toUpperCase();

    // Get card type
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

    Serial.println("\n=== RFID Card Detected ===");
    Serial.print("Card UID: ");
    Serial.println(content);
    Serial.print("Card Type: ");
    Serial.println(rfid.PICC_GetTypeName(piccType));
    Serial.println("===========================\n");

    // Check for specific card
    if (content.substring(1) == "AC AC AC AC")
    {
        Serial.println("✓ Authorized card detected!");
        digitalWrite(ledGreen, HIGH);
        digitalWrite(ledRed, LOW);

        // Send RFID data via MQTT
        JsonDocument doc;
        doc["cardUID"] = content;
        doc["status"] = "Authorized";
        doc["timestamp"] = millis();

        char buff[256];
        serializeJson(doc, buff);
        client.publish("RFID/readings", buff);

        delay(500);
        digitalWrite(ledGreen, LOW);
    }
    else
    {
        Serial.println("✗ Unauthorized card detected!");
        digitalWrite(ledRed, HIGH);
        digitalWrite(ledGreen, LOW);

        // Send RFID data via MQTT
        JsonDocument doc;
        doc["cardUID"] = content;
        doc["status"] = "Unauthorized";
        doc["timestamp"] = millis();

        char buff[256];
        serializeJson(doc, buff);
        client.publish("RFID/readings", buff);

        delay(500);
        digitalWrite(ledRed, LOW);
    }

    // Halt PICC
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}

// ===== Setup =====
void setup()
{
    Serial.begin(115200);
    delay(2000);

    // Initialize pins
    pinMode(ledGreen, OUTPUT);
    pinMode(ledRed, OUTPUT);
    pinMode(led, OUTPUT);
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledRed, LOW);
    digitalWrite(led, LOW);

    // Initialize RFID
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("✓ RFID module is ready");

    // Connect to WiFi & MQTT
    connectAP();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    Serial.println("\nSystem ready!\n");
}

// ===== Main Loop =====
void loop()
{
    // Ensure MQTT connection
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    // Process RFID card detection
    if (checkIn)
    {
        processRFIDCard();
    }

    // Optional: Send DHT readings every 5 seconds (if DHT sensor present)
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= DHT_INTERVAL)
    {
        previousMillis = currentMillis;
        // Uncomment if DHT sensor is connected:
        // sendDHTvalues();
    }
}