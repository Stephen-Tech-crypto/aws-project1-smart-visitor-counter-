/*
  Smart Visitor Counter — ESP32 firmware

  On each debounced press of a push button, publishes a small JSON
  message over MQTT/TLS to AWS IoT Core. All counting logic lives in
  the cloud (Lambda + DynamoDB) — this sketch's only job is to report
  "a press happened" reliably.

  Wiring:
    Push button between BUTTON_PIN and GND (uses internal pull-up,
    so no external resistor is needed).
    Onboard/external LED on STATUS_LED_PIN blinks to acknowledge a
    registered press.

  Libraries required (install via Library Manager):
    - PubSubClient (Nick O'Leary)
    - ArduinoJson
  Board package: esp32 by Espressif Systems
*/

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------- USER CONFIG ----------

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// From: aws iot describe-endpoint --endpoint-type iot:Data-ATS
const char* AWS_IOT_ENDPOINT = "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com";
const int   AWS_IOT_PORT     = 8883;

const char* DEVICE_ID = "esp32-visitor-counter-01";
// Must match the topic your IoT policy and IoT rule allow.
String MQTT_TOPIC = String("visitorcounter/") + DEVICE_ID + "/press";

const int BUTTON_PIN     = 4;   // GPIO4, active LOW
const int STATUS_LED_PIN = 2;   // onboard LED on most ESP32 dev boards
const unsigned long DEBOUNCE_MS = 250;

// ---------- CERTIFICATES ----------
// Paste the contents of each PEM file between the R"EOF( ... )EOF" markers.
// Keep the BEGIN/END lines and line breaks exactly as in the original file.

const char AWS_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE_AmazonRootCA1_PEM_HERE
-----END CERTIFICATE-----
)EOF";

const char DEVICE_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE_DEVICE_CERTIFICATE_PEM_HERE
-----END CERTIFICATE-----
)EOF";

const char DEVICE_PRIVATE_KEY[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
PASTE_DEVICE_PRIVATE_KEY_PEM_HERE
-----END RSA PRIVATE KEY-----
)EOF";

// ---------- GLOBALS ----------

WiFiClientSecure tlsClient;
PubSubClient mqttClient(tlsClient);

unsigned long lastPressMillis = 0;
int lastButtonState = HIGH;

// ---------- SETUP HELPERS ----------

void connectWiFi() {
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectAWSIoT() {
  tlsClient.setCACert(AWS_ROOT_CA);
  tlsClient.setCertificate(DEVICE_CERT);
  tlsClient.setPrivateKey(DEVICE_PRIVATE_KEY);

  mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);

  Serial.println("Connecting to AWS IoT Core...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID)) {
      Serial.println("Connected to AWS IoT Core.");
    } else {
      Serial.printf("MQTT connect failed, rc=%d. Retrying in 3s...\n", mqttClient.state());
      delay(3000);
    }
  }
}

void publishPressEvent() {
  StaticJsonDocument<128> doc;
  doc["device_id"] = DEVICE_ID;
  doc["event"] = "press";
  doc["uptime_ms"] = millis();

  char payload[128];
  size_t len = serializeJson(doc, payload);

  bool ok = mqttClient.publish(MQTT_TOPIC.c_str(), payload, len);
  Serial.printf("Publish %s: %s\n", ok ? "OK" : "FAILED", payload);

  // Brief LED flash to confirm the press was sent.
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(80);
  digitalWrite(STATUS_LED_PIN, LOW);
}

// ---------- ARDUINO ENTRY POINTS ----------

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);

  connectWiFi();
  connectAWSIoT();
}

void loop() {
  if (!mqttClient.connected()) {
    connectAWSIoT();
  }
  mqttClient.loop();

  int reading = digitalRead(BUTTON_PIN);

  // Falling edge = button just pressed (active LOW with pull-up).
  if (reading == LOW && lastButtonState == HIGH &&
      (millis() - lastPressMillis) > DEBOUNCE_MS) {
    lastPressMillis = millis();
    publishPressEvent();
  }

  lastButtonState = reading;
}
