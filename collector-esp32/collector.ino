#include <ArduinoMqttClient.h>
#include <DHT22.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

#if defined(ESP32)
  #include <WiFi.h>
  #define LEDPIN 2
  #define DHTPIN 4
  #define I2C_SDA 21
  #define I2C_SCL 22
#elif defined(ARDUINO_SAMD_MKR1000)
  #include <WiFi101.h>
  #define LEDPIN A5
  #define DHTPIN A6
  // MKR1000 I2C uses default Wire pins: SDA = 11, SCL = 12
#else
  #error "Unsupported board. Please compile for ESP32 or MKR1000."
#endif

DHT22 dht(DHTPIN);
Adafruit_BME280 bme;
bool bmeAvailable = false;

char ssid[] = "";
char pass[] = "";

const char broker[] = "";
const char mqttUser[] = "";
const char mqttPass[] = "";
int port = 1883;
const char topic[] = "General";

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// Publish interval (seconds)
const unsigned long publishInterval = 10;

char macStr[18];
byte mac[6];

void setup() {
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW);

  Serial.begin(115200);

  #if defined(ESP32)
    Wire.begin(I2C_SDA, I2C_SCL);
  #else
    Wire.begin();
  #endif
  bmeAvailable = bme.begin(0x76) || bme.begin(0x77);
  Serial.println(bmeAvailable ? "BME280 found" : "BME280 not found");

  // Boot indicator
  for (int i = 0; i < 3; i++) {
    digitalWrite(LEDPIN, HIGH);
    delay(200);
    digitalWrite(LEDPIN, LOW);
    delay(200);
  }

  // Connect to WiFi just to get MAC address
  Serial.println("Connecting to WiFi for MAC...");
  WiFi.begin(ssid, pass);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.macAddress(mac);
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println(macStr);
    
    #if defined(ESP32)
      WiFi.disconnect(true);
    #elif defined(ARDUINO_SAMD_MKR1000)
      WiFi.disconnect();
      WiFi.end();
    #endif
  } else {
    Serial.println("\nFailed to connect for MAC, using default ID");
    strcpy(macStr, "00:00:00:00:00:00");
  }
}

void loop() {
  // --- Ensure WiFi connection ---
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
    #if defined(ESP32)
      WiFi.disconnect(true);
    #elif defined(ARDUINO_SAMD_MKR1000)
      WiFi.disconnect();
    #endif
    WiFi.begin(ssid, pass);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(" WiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(" WiFi failed, waiting...");

      #if defined(ESP32)
        WiFi.disconnect(true);
      #elif defined(ARDUINO_SAMD_MKR1000)
        WiFi.disconnect();
      #endif

      delay(publishInterval * 1000);
      return;
    }
  }

  // --- Ensure MQTT connection ---
  if (!mqttClient.connected()) {
    mqttClient.setUsernamePassword(mqttUser, mqttPass);
    Serial.println("Connecting to MQTT...");
    if (!mqttClient.connect(broker, port)) {
      Serial.print("MQTT failed, code = ");
      Serial.println(mqttClient.connectError());
      #if defined(ESP32)
        WiFi.disconnect(true);
      #elif defined(ARDUINO_SAMD_MKR1000)
        WiFi.disconnect();
      #endif

      delay(publishInterval * 1000);
      return;
    }
    Serial.println("MQTT connected");
  }

  // --- Read DHT22 ---
  float h = dht.getHumidity();
  float t = dht.getTemperature();
  bool dhtOk = !isnan(h) && !isnan(t);
  if (!dhtOk) Serial.println("DHT22 read error");

  // --- Read BME280 (pressure) ---
  float p = NAN;
  bool bmeOk = false;
  if (bmeAvailable) {
    p = bme.readPressure() / 100.0F;  // Pa -> hPa
    bmeOk = !isnan(p);
    if (!bmeOk) Serial.println("BME280 read error");
  }

  // Skip publish only if all sensors failed
  if (!dhtOk && !bmeOk) {
    Serial.println("All sensors failed, skipping publish");
    #if defined(ESP32)
      WiFi.disconnect(true);
    #elif defined(ARDUINO_SAMD_MKR1000)
      WiFi.disconnect();
    #endif
    delay(publishInterval * 1000);
    return;
  }

  // --- Build JSON message ---
  String msg = "{";
  msg += "\"id\": \"" + String(macStr) + "\"";
  if (dhtOk) {
    msg += ",\"temperature\": " + String(t, 1);
    msg += ",\"humidity\": " + String(h, 1);
  }
  if (bmeOk) {
    msg += ",\"pressure\": " + String(p, 1);
  }
  msg += "}";

  // --- Publish ---
  mqttClient.beginMessage(topic);
  mqttClient.print(msg);
  mqttClient.endMessage();

  Serial.print("Published: ");
  Serial.println(msg);

  // --- Blink LED once ---
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);

  // --- Disconnect to save power ---
  mqttClient.stop();
    #if defined(ESP32)
      WiFi.disconnect(true);
    #elif defined(ARDUINO_SAMD_MKR1000)
      WiFi.disconnect();
      WiFi.end();
    #endif

  Serial.println("Sleeping...");
  delay(publishInterval * 1000);  // replace with deep sleep later
}
