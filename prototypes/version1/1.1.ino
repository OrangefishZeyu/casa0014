
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

// =====================================
// 1. WiFi Configuration
// =====================================

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

// =====================================
// 2. MQTT Configuration
// =====================================

const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;

const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

// Use your assigned Luminaire topic
const char* mqtt_topic =
  "student/CASA0014/luminaire/0";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// =====================================
// 3. Ultrasonic Sensor
// =====================================

const int trigPin = 2;
const int echoPin = 3;

// Distance range in cm
const float minDistance = 5.0;
const float maxDistance = 40.0;

// Maximum LED brightness
const int maxBrightness = 180;

float filteredDistance = 0;
bool firstReading = true;

// =====================================
// 4. Vespera LED Configuration
// =====================================

const int num_leds = 72;
const int payload_size = num_leds * 3;

byte RGBpayload[payload_size];

// =====================================
// 5. Timers
// =====================================

unsigned long lastSensorRead = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

// =====================================
// 6. Setup
// =====================================

void setup() {
  Serial.begin(9600);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512);

  WiFi.begin(ssid, password);

  Serial.println("Distance Lamp Starting...");
}

// =====================================
// 7. WiFi Connection
// =====================================

void checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWifiAttempt < 10000) {
    return;
  }

  lastWifiAttempt = millis();

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
}

// =====================================
// 8. MQTT Connection
// =====================================

void checkMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  if (millis() - lastMqttAttempt < 3000) {
    return;
  }

  lastMqttAttempt = millis();

  Serial.println("Connecting to MQTT...");

  String clientId =
    "CASA0014-Distance-" + String(millis());

  if (mqttClient.connect(
        clientId.c_str(),
        mqtt_username,
        mqtt_password
      )) {
    Serial.println("MQTT Connected!");
  } else {
    Serial.print("MQTT Failed, state: ");
    Serial.println(mqttClient.state());
  }
}

// =====================================
// 9. Read Distance
// =====================================

float readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration =
    pulseIn(echoPin, HIGH, 30000UL);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

// =====================================
// 10. Distance to Brightness
// =====================================

int distanceToBrightness(float distance) {
  if (distance <= minDistance) {
    return maxBrightness;
  }

  if (distance >= maxDistance) {
    return 0;
  }

  float ratio =
    (maxDistance - distance) /
    (maxDistance - minDistance);

  return (int)(ratio * maxBrightness);
}

// =====================================
// 11. Send LED Colours
// =====================================

void sendBrightness(int brightness) {
  if (!mqttClient.connected()) {
    return;
  }

  // Warm white colour
  byte red = brightness;
  byte green = brightness * 0.75;
  byte blue = brightness * 0.35;

  for (int i = 0; i < num_leds; i++) {
    RGBpayload[i * 3 + 0] = red;
    RGBpayload[i * 3 + 1] = green;
    RGBpayload[i * 3 + 2] = blue;
  }

  bool success = mqttClient.publish(
    mqtt_topic,
    RGBpayload,
    payload_size
  );

  if (success) {
    Serial.println("MQTT Published!");
  } else {
    Serial.println("MQTT Publish Failed");
  }
}

// =====================================
// 12. Main Loop
// =====================================

void loop() {
  checkWiFi();
  checkMQTT();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  if (millis() - lastSensorRead >= 100) {
    lastSensorRead = millis();

    float distance = readDistance();

    if (distance < 0) {
      Serial.println("No echo detected");
      return;
    }

    // Smooth distance readings
    if (firstReading) {
      filteredDistance = distance;
      firstReading = false;
    } else {
      filteredDistance =
        filteredDistance * 0.7 +
        distance * 0.3;
    }

    int brightness =
      distanceToBrightness(filteredDistance);

    Serial.print("Distance: ");
    Serial.print(filteredDistance);
    Serial.print(" cm | Brightness: ");
    Serial.println(brightness);

    sendBrightness(brightness);
  }
}
