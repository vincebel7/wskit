#include <ArduinoMqttClient.h>
#include <DHT22.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Adafruit_BMP085.h>

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

// CONFIGURE
char ssid[] = "";
char pass[] = "";

const char broker[] = "";
const char mqttUser[] = "";
const char mqttPass[] = "";
int port = 1883;
const char topic[] = "General";


DHT22 dht(DHTPIN);
Adafruit_BME280 bme280;
Adafruit_BME680 bme680;
Adafruit_BMP085 bmp180;
enum BMEType { BME_NONE, BME_280, BME_680, BME_BMP180 };
BMEType bmeType = BME_NONE;

// Auto-restart after this many consecutive network failures
const int maxNetworkFailures = 5;
int networkFailures = 0;

void maybeRestartAfterFailure() {
  networkFailures++;
  Serial.print("Network failure #");
  Serial.println(networkFailures);
  if (networkFailures >= maxNetworkFailures) {
    Serial.println("Too many failures, restarting device...");
    delay(1000);
    #if defined(ESP32)
      ESP.restart();
    #elif defined(ARDUINO_SAMD_MKR1000)
      NVIC_SystemReset();
    #endif
  }
}

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

  // Don't let MQTT connect block forever if broker is unreachable
  mqttClient.setConnectionTimeout(5000);

  #if defined(ESP32)
    Wire.begin(I2C_SDA, I2C_SCL);
  #else
    Wire.begin();
  #endif
  if (bme680.begin(0x76) || bme680.begin(0x77)) {
    bmeType = BME_680;
    Serial.println("BME680 found");
  } else if (bme280.begin(0x76) || bme280.begin(0x77)) {
    bmeType = BME_280;
    Serial.println("BME280 found");
  } else if (bmp180.begin()) {
    bmeType = BME_BMP180;
    Serial.println("BMP180 found");
  } else {
    Serial.println("No pressure sensor found");
  }

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
  if (WiFi.status() != WL_CONNECTED || (uint32_t)WiFi.localIP() == 0) {
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

      maybeRestartAfterFailure();
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

      maybeRestartAfterFailure();
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

  // --- Read pressure sensor (pressure always; temp/humidity as DHT22 fallback where available) ---
  float p = NAN;
  float bmeTemp = NAN, bmeHumidity = NAN;
  bool bmeOk = false;
  if (bmeType == BME_680) {
    if (bme680.performReading()) {
      p           = bme680.pressure / 100.0F;  // Pa -> hPa
      bmeTemp     = bme680.temperature;
      bmeHumidity = bme680.humidity;
      bmeOk = true;
    } else {
      Serial.println("BME680 read error");
    }
  } else if (bmeType == BME_280) {
    float rawP = bme280.readPressure();
    float rawT = bme280.readTemperature();
    float rawH = bme280.readHumidity();
    bool humidityValid = !isnan(rawH) && rawH >= 0.0 && rawH <= 100.0;
    if (!isnan(rawP) && !isnan(rawT)) {
      p       = rawP / 100.0F;  // Pa -> hPa
      bmeTemp = rawT;
      if (humidityValid) bmeHumidity = rawH;
      bmeOk = true;
    } else {
      Serial.println("BME280 read error");
    }
    if (!humidityValid) Serial.println("BME280: humidity invalid (chip may be BMP280 — no humidity sensor)");
  } else if (bmeType == BME_BMP180) {
    float rawP = bmp180.readPressure();
    float rawT = bmp180.readTemperature();
    if (!isnan(rawP) && !isnan(rawT)) {
      p       = rawP / 100.0F;  // Pa -> hPa
      bmeTemp = rawT;
      // bmeHumidity stays NAN — BMP180 has no humidity sensor
      bmeOk = true;
    } else {
      Serial.println("BMP180 read error");
    }
  }

  // Fall back to pressure sensor for temp (and humidity, if sensor supports it)
  float t_out = dhtOk ? t : bmeTemp;
  float h_out = dhtOk ? h : bmeHumidity;
  if (!dhtOk && bmeOk) {
    if (bmeType == BME_BMP180)
      Serial.println("DHT22 unavailable: using BMP180 for temperature only (no humidity fallback)");
    else
      Serial.println("DHT22 unavailable: using BME sensor for temp/humidity");
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
  if (!isnan(t_out)) msg += ",\"temperature\": " + String(t_out, 1);
  if (!isnan(h_out)) msg += ",\"humidity\": "    + String(h_out, 1);
  if (bmeOk)         msg += ",\"pressure\": "    + String(p, 1);
  msg += "}";

  // --- Publish ---
  mqttClient.beginMessage(topic);
  mqttClient.print(msg);
  mqttClient.endMessage();

  Serial.print("Published: ");
  Serial.println(msg);

  // Publish succeeded, reset the failure counter
  networkFailures = 0;

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
