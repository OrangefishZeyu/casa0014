
#include <SPI.h>
#include <WiFiNINA.h>e:\casa0014\project\2\arduino_secrets.h
#include <PubSubClient.h>
#include "arduino_secrets.h"

// ======================================
// WiFi and MQTT
// ======================================

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;

const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

const char* mqtt_topic =
  "student/CASA0014/luminaire/0";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ======================================
// LED Configuration
// ======================================

const int NUM_LEDS = 72;
const int PAYLOAD_SIZE = NUM_LEDS * 3;

uint8_t ledData[PAYLOAD_SIZE];

// ======================================
// Ultrasonic Sensors
// ======================================

const int TRIG_A = 2;
const int ECHO_A = 3;

const int TRIG_B = 4;
const int ECHO_B = 5;

// ======================================
// Rotary Encoders
// ======================================

const int ENC_A_CLK = A0;
const int ENC_A_DT  = A1;
const int ENC_A_SW  = A2;

const int ENC_B_CLK = A3;
const int ENC_B_DT  = A4;
const int ENC_B_SW  = A5;

// ======================================
// Control State
// ======================================

bool lightA = true;
bool lightB = true;

int hueA = 0;
int hueB = 120;

int brightnessA = 0;
int brightnessB = 0;

int lastClkA;
int lastClkB;

bool lastButtonA = HIGH;
bool lastButtonB = HIGH;

bool stableButtonA = HIGH;
bool stableButtonB = HIGH;

unsigned long debounceA = 0;
unsigned long debounceB = 0;

const unsigned long DEBOUNCE_MS = 35;

// Distance and refresh settings
const int MIN_DISTANCE = 5;
const int MAX_DISTANCE = 50;

const unsigned long SENSOR_INTERVAL = 50;
const unsigned long MQTT_INTERVAL = 100;

unsigned long lastSensorUpdate = 0;
unsigned long lastMqttUpdate = 0;

bool ledDirty = true;

// ======================================
// HSV to RGB
// ======================================

void hsvToRgb(
  int hue,
  uint8_t brightness,
  uint8_t &r,
  uint8_t &g,
  uint8_t &b
) {
  hue = (hue % 360 + 360) % 360;

  int region = hue / 60;
  int remainder = hue % 60;

  int rising = brightness * remainder / 60;
  int falling = brightness * (60 - remainder) / 60;

  switch (region) {
    case 0:
      r = brightness; g = rising; b = 0;
      break;
    case 1:
      r = falling; g = brightness; b = 0;
      break;
    case 2:
      r = 0; g = brightness; b = rising;
      break;
    case 3:
      r = 0; g = falling; b = brightness;
      break;
    case 4:
      r = rising; g = 0; b = brightness;
      break;
    default:
      r = brightness; g = 0; b = falling;
      break;
  }
}

// ======================================
// Read Distance
// ======================================

int readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration =
    pulseIn(echoPin, HIGH, 12000UL);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2;
}

// ======================================
// Distance to Brightness
// ======================================

int distanceToBrightness(int distance) {
  if (distance < 0) return -1;

  distance = constrain(
    distance,
    MIN_DISTANCE,
    MAX_DISTANCE
  );

  return map(
    distance,
    MIN_DISTANCE,
    MAX_DISTANCE,
    255,
    0
  );
}

// ======================================
// Read Rotary Encoder
// ======================================

void updateEncoder(
  int clkPin,
  int dtPin,
  int &lastClk,
  int &hue
) {
  int currentClk = digitalRead(clkPin);

  if (currentClk != lastClk) {
    if (currentClk == LOW) {
      if (digitalRead(dtPin) != currentClk) {
        hue += 12;
      } else {
        hue -= 12;
      }

      hue = (hue % 360 + 360) % 360;
      ledDirty = true;
    }

    lastClk = currentClk;
  }
}

// ======================================
// Read Encoder Button
// ======================================

void updateButton(
  int swPin,
  bool &lastReading,
  bool &stableState,
  unsigned long &lastChange,
  bool &lightEnabled
) {
  bool reading = digitalRead(swPin);

  if (reading != lastReading) {
    lastChange = millis();
  }

  if (millis() - lastChange >= DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        lightEnabled = !lightEnabled;
        ledDirty = true;
      }
    }
  }

  lastReading = reading;
}

// ======================================
// Build LED RGB Payload
// ======================================

void updateLedData() {
  uint8_t rA, gA, bA;
  uint8_t rB, gB, bB;

  hsvToRgb(
    hueA,
    lightA ? brightnessA : 0,
    rA, gA, bA
  );

  hsvToRgb(
    hueB,
    lightB ? brightnessB : 0,
    rB, gB, bB
  );

  for (int i = 0; i < NUM_LEDS; i++) {
    int index = i * 3;

    // Region A: LEDs 18-53
    if (i >= 18 && i <= 53) {
      ledData[index] = rA;
      ledData[index + 1] = gA;
      ledData[index + 2] = bA;
    }

    // Region B: remaining LEDs
    else {
      ledData[index] = rB;
      ledData[index + 1] = gB;
      ledData[index + 2] = bB;
    }
  }
}

// ======================================
// WiFi Connection
// ======================================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi");

  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 15000
  ) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.println(WiFi.localIP());
  }
}

// ======================================
// MQTT Connection
// ======================================

void connectMQTT() {
  if (mqttClient.connected()) return;

  Serial.print("Connecting to MQTT...");

  String clientId =
    "CASA0014-Controller-" +
    String(millis());

  if (
    mqttClient.connect(
      clientId.c_str(),
      mqtt_username,
      mqtt_password
    )
  ) {
    Serial.println("Connected!");
    ledDirty = true;
  } else {
    Serial.print("Failed, state=");
    Serial.println(mqttClient.state());
  }
}

// ======================================
// Setup
// ======================================

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_A, OUTPUT);
  pinMode(ECHO_A, INPUT);

  pinMode(TRIG_B, OUTPUT);
  pinMode(ECHO_B, INPUT);

  pinMode(ENC_A_CLK, INPUT_PULLUP);
  pinMode(ENC_A_DT, INPUT_PULLUP);
  pinMode(ENC_A_SW, INPUT_PULLUP);

  pinMode(ENC_B_CLK, INPUT_PULLUP);
  pinMode(ENC_B_DT, INPUT_PULLUP);
  pinMode(ENC_B_SW, INPUT_PULLUP);

  lastClkA = digitalRead(ENC_A_CLK);
  lastClkB = digitalRead(ENC_B_CLK);

  lastButtonA = digitalRead(ENC_A_SW);
  lastButtonB = digitalRead(ENC_B_SW);

  stableButtonA = lastButtonA;
  stableButtonB = lastButtonB;

  mqttClient.setServer(
    mqtt_server,
    mqtt_port
  );

  // 216-byte payload plus MQTT overhead
  mqttClient.setBufferSize(512);

  connectWiFi();
  connectMQTT();

  Serial.println("Controller ready!");
}

// ======================================
// Main Loop
// ======================================

void loop() {

  // 1. Read both rotary encoders
  updateEncoder(
    ENC_A_CLK,
    ENC_A_DT,
    lastClkA,
    hueA
  );

  updateEncoder(
    ENC_B_CLK,
    ENC_B_DT,
    lastClkB,
    hueB
  );

  // 2. Read both buttons
  updateButton(
    ENC_A_SW,
    lastButtonA,
    stableButtonA,
    debounceA,
    lightA
  );

  updateButton(
    ENC_B_SW,
    lastButtonB,
    stableButtonB,
    debounceB,
    lightB
  );

  // 3. Update sensor brightness
  if (
    millis() - lastSensorUpdate >=
    SENSOR_INTERVAL
  ) {
    lastSensorUpdate = millis();

    int distanceA =
      readDistance(TRIG_A, ECHO_A);

    int distanceB =
      readDistance(TRIG_B, ECHO_B);

    int newA = distanceToBrightness(distanceA);
    int newB = distanceToBrightness(distanceB);

    if (newA >= 0 && newA != brightnessA) {
      brightnessA = newA;
      ledDirty = true;
    }

    if (newB >= 0 && newB != brightnessB) {
      brightnessB = newB;
      ledDirty = true;
    }
  }

  // 4. Maintain connections
  static unsigned long lastReconnect = 0;

  if (
    millis() - lastReconnect >= 3000
  ) {
    lastReconnect = millis();

    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (
      WiFi.status() == WL_CONNECTED &&
      !mqttClient.connected()
    ) {
      connectMQTT();
    }
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // 5. Publish LED data
  if (
    mqttClient.connected() &&
    ledDirty &&
    millis() - lastMqttUpdate >=
    MQTT_INTERVAL
  ) {
    lastMqttUpdate = millis();

    updateLedData();

    bool sent = mqttClient.publish(
      mqtt_topic,
      ledData,
      PAYLOAD_SIZE
    );

    if (sent) {
      ledDirty = false;
    }
  }
}
