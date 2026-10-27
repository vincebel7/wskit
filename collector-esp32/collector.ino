#include <ArduinoMqttClient.h>
#include <DHT22.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_SHT4x.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <esp_sleep.h>
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

// Publish interval (seconds). ESP32 deep sleeps between readings; MKR1000 stays awake.
const unsigned long publishInterval = 300;

// Optional static IP (ESP32 only) - the biggest battery saving you can configure:
// skipping DHCP cuts each wake from ~1.5-2 s to ~0.5 s. Reserve the IP on your router
// (or pick one outside its DHCP pool). Leave staticIP as 0.0.0.0 to use DHCP.
// gateway is required when staticIP is set. DNS defaults to the gateway if left 0.0.0.0.
IPAddress staticIP(0, 0, 0, 0);
IPAddress gateway(0, 0, 0, 0);  // your router's IP, e.g. 192.168.3.1
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(0, 0, 0, 0);


Adafruit_SHT4x sht4;
bool shtFound = false;  // SHT4x (SHT40/41/45) replaces the DHT22 when present
DHT22 dht(DHTPIN);
Adafruit_BME280 bme280;
Adafruit_BME680 bme680;
Adafruit_BMP085 bmp180;
enum BMEType { BME_NONE, BME_280, BME_680, BME_BMP180 };
BMEType bmeType = BME_NONE;

// Auto-restart after this many consecutive network failures
const int maxNetworkFailures = 5;
#if defined(ESP32)
  // RTC memory survives deep sleep (but not a reset or power loss)
  RTC_DATA_ATTR int networkFailures = 0;
  // AP from the last successful connect, so the next wake can skip the channel scan
  RTC_DATA_ATTR uint8_t cachedBssid[6];
  RTC_DATA_ATTR int32_t cachedChannel = 0;  // 0 = nothing cached
#else
  int networkFailures = 0;
#endif

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

char macStr[18];
byte mac[6];

bool waitForWiFi(unsigned long timeoutMs) {
  unsigned long startAttempt = millis();
  // Poll quickly: a cached-AP connect can finish in a few hundred ms
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeoutMs) {
    delay(10);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectWiFi() {
  Serial.println("Connecting to WiFi...");
  #if defined(ESP32)
    WiFi.persistent(false);  // don't rewrite credentials to flash on every wake
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);    // modem sleep delays incoming packets (e.g. the PUBACK) until the next beacon
    if ((uint32_t)staticIP != 0) {
      WiFi.config(staticIP, gateway, subnet, (uint32_t)dns != 0 ? dns : gateway);
    }

    // Fast path: connect straight to the AP from the last wake
    if (cachedChannel != 0) {
      WiFi.begin(ssid, pass, cachedChannel, cachedBssid);
      if (waitForWiFi(3000)) return true;
      Serial.println("Cached AP failed, doing full scan...");
      cachedChannel = 0;
      WiFi.disconnect();
    }

    WiFi.begin(ssid, pass);
    if (!waitForWiFi(10000)) return false;
    memcpy(cachedBssid, WiFi.BSSID(), sizeof(cachedBssid));
    cachedChannel = WiFi.channel();
    return true;
  #else
    WiFi.begin(ssid, pass);
    return waitForWiFi(10000);
  #endif
}

void disconnectWiFi() {
  #if defined(ESP32)
    WiFi.disconnect(true);
  #elif defined(ARDUINO_SAMD_MKR1000)
    WiFi.disconnect();
    WiFi.end();
  #endif
}

// Wait for the next reading. On ESP32 this deep sleeps and never returns:
// the chip reboots into setup() on wake.
void sleepUntilNextCycle() {
  #if defined(ESP32)
    // Subtract time spent awake so readings stay on a steady cadence
    unsigned long awakeMs = millis();
    unsigned long intervalMs = publishInterval * 1000;
    unsigned long sleepMs = awakeMs < intervalMs ? intervalMs - awakeMs : 1000;
    Serial.print("Awake for ");
    Serial.print(awakeMs);
    Serial.println(" ms, deep sleeping...");
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
    esp_deep_sleep_start();
  #else
    Serial.println("Sleeping...");
    delay(publishInterval * 1000);
  #endif
}

// The DHT22 is bit-banged with microsecond timing and the library doesn't block
// interrupts, so this runs before WiFi is up. Returns the library error code (0 = OK).
// Error codes: 1-3 timing (1 = no response), 4 = checksum, 255 = values out of range.
uint8_t readDHT(float &h, float &t) {
  uint8_t err = DHT22::OK;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0) delay(2100);  // sensor allows one read per 2 s
    h = dht.getHumidity();         // does the actual read
    t = dht.getTemperature();      // reuses that read (library rate-limits to one per 2.1 s)
    err = dht.getLastError();
    // Library returns -1 (humidity) / -273 (temp) on a failed read, not NAN
    if (err == DHT22::OK && h >= 0.0 && h <= 100.0 && t >= -40.0 && t <= 80.0) return DHT22::OK;
    if (err == DHT22::OK) err = 255;
    Serial.print("DHT22 read error, code = ");
    Serial.println(err);
    // No response at all usually means no DHT22 is wired: don't spend 2 s retrying
    if (err == DHT22::ERR_TIMING_80) break;
  }
  return err;
}

// Lists the addresses that answer on the I2C bus, e.g. "0x44 0x76" (SHT4x + BME280).
// Sent with each reading so wiring can be checked without the serial monitor.
String scanI2C() {
  String found = "";
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found.length()) found += " ";
      found += "0x" + String(addr, HEX);
    }
  }
  return found;
}

// One full cycle: read sensors, connect, publish, disconnect
void runCycle() {
  // --- Read temp/humidity sensor: SHT4x if present, otherwise DHT22 (before WiFi, see readDHT) ---
  float h = NAN, t = NAN;
  const char *thName = NULL;  // sensor that supplied t/h; NULL = it failed
  uint8_t dhtErr = DHT22::OK;
  if (shtFound) {
    sensors_event_t hEvent, tEvent;
    if (sht4.getEvent(&hEvent, &tEvent)) {
      h = constrain(hEvent.relative_humidity, 0.0, 100.0);  // library doesn't clamp
      t = tEvent.temperature;
      thName = "sht4x";
    } else {
      Serial.println("SHT4x read error");
    }
  } else {
    dhtErr = readDHT(h, t);
    if (dhtErr == DHT22::OK) thName = "dht22";
  }
  bool thOk = thName != NULL;

  // --- Read pressure sensor (pressure always; temp/humidity as DHT22 fallback where available) ---
  float p = NAN;
  float bmeTemp = NAN, bmeHumidity = NAN;
  bool bmeOk = false;
  const char *bmeName = "none";
  if (bmeType == BME_680) {
    bmeName = "bme680";
    if (bme680.performReading()) {
      p           = bme680.pressure / 100.0F;  // Pa -> hPa
      bmeTemp     = bme680.temperature;
      bmeHumidity = bme680.humidity;
      bmeOk = true;
    } else {
      Serial.println("BME680 read error");
    }
  } else if (bmeType == BME_280) {
    bmeName = "bme280";
    float rawP = bme280.readPressure();
    float rawT = bme280.readTemperature();
    float rawH = bme280.readHumidity();
    if (!isnan(rawP) && !isnan(rawT)) {
      p       = rawP / 100.0F;  // Pa -> hPa
      bmeTemp = rawT;
      bmeHumidity = rawH;  // library returns NAN if unavailable; NAN is left out of the JSON
      bmeOk = true;
    } else {
      Serial.println("BME280 read error");
    }
  } else if (bmeType == BME_BMP180) {
    bmeName = "bmp180";
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
  float t_out = thOk ? t : bmeTemp;
  float h_out = thOk ? h : bmeHumidity;
  if (!thOk && bmeOk) {
    if (bmeType == BME_BMP180)
      Serial.println("Temp/humidity sensor unavailable: using BMP180 for temperature only (no humidity fallback)");
    else
      Serial.println("Temp/humidity sensor unavailable: using BME sensor for temp/humidity");
  }

  // Skip the cycle (and the WiFi connect) only if all sensors failed
  if (!thOk && !bmeOk) {
    Serial.println("All sensors failed, skipping publish");
    return;
  }

  // --- Connect WiFi ---
  if (!connectWiFi()) {
    Serial.println("WiFi failed, waiting...");
    disconnectWiFi();
    maybeRestartAfterFailure();
    return;
  }
  Serial.print("WiFi connected! IP: ");
  Serial.println(IPAddress(WiFi.localIP()));  // WiFi101 returns a raw uint32_t

  // MAC is the collector ID; read it once WiFi is up (no separate connect needed)
  if (macStr[0] == '\0') {
    WiFi.macAddress(mac);
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println(macStr);
    // Fixed MQTT client ID. The library default is "Arduino-<millis>", which repeats
    // across deep-sleep wakes (millis is ~250 at every connect)
    char clientId[20];
    sprintf(clientId, "wskit-%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    mqttClient.setId(clientId);
  }

  // --- Connect MQTT ---
  mqttClient.setUsernamePassword(mqttUser, mqttPass);
  Serial.println("Connecting to MQTT...");
  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT failed, code = ");
    Serial.println(mqttClient.connectError());
    disconnectWiFi();
    maybeRestartAfterFailure();
    return;
  }
  Serial.println("MQTT connected");

  // --- Build JSON message ---
  String msg = "{";
  msg += "\"id\": \"" + String(macStr) + "\"";
  if (!isnan(t_out)) msg += ",\"temperature\": " + String(t_out, 1);
  if (!isnan(h_out)) msg += ",\"humidity\": "    + String(h_out, 1);
  if (bmeOk)         msg += ",\"pressure\": "    + String(p, 1);
  // Diagnostics (not stored; visible in the mqtt-subscriber logs)
  msg += ",\"source\": \"" + String(thOk ? thName : bmeName) + "\"";
  if (dhtErr != DHT22::OK) msg += ",\"dht_error\": " + String(dhtErr);
  msg += ",\"rssi\": " + String(WiFi.RSSI());
  msg += ",\"i2c\": \"" + scanI2C() + "\"";
  msg += "}";

  // --- Publish ---
  // QoS 1: endMessage() waits for the broker's PUBACK, so we only sleep once the
  // reading has arrived. With QoS 0 the radio was usually off before it left.
  mqttClient.beginMessage(topic, false, 1);
  mqttClient.print(msg);
  if (!mqttClient.endMessage()) {
    Serial.println("Publish not acknowledged by broker");
    mqttClient.stop();
    disconnectWiFi();
    maybeRestartAfterFailure();
    return;
  }

  Serial.print("Published: ");
  Serial.println(msg);

  // Publish succeeded, reset the failure counter
  networkFailures = 0;

  // --- Disconnect to save power ---
  mqttClient.stop();
  delay(20);  // let the MQTT DISCONNECT leave before the radio goes off
  disconnectWiFi();

  // --- Blink LED once to show a successful publish (after WiFi is off, so it's cheap) ---
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);
}

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
  // SHT4x measures on demand and sleeps in between (~1 uA); defaults are high precision, heater off
  if (sht4.begin()) {
    shtFound = true;
    Serial.println("SHT4x found");
  }

  // Boot indicator, only on power-up/reset (not on every wake from deep sleep)
  #if defined(ESP32)
    bool coldBoot = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
  #else
    bool coldBoot = true;
  #endif
  if (coldBoot) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LEDPIN, HIGH);
      delay(200);
      digitalWrite(LEDPIN, LOW);
      delay(200);
    }
  }

  #if defined(ESP32)
    // Every wake from deep sleep starts here, so the whole cycle runs from setup()
    runCycle();
    sleepUntilNextCycle();  // does not return
  #endif
}

void loop() {
  // Only reached on MKR1000, which can't deep sleep cleanly (WINC1500 won't
  // reconnect reliably after waking), so it stays awake between readings.
  runCycle();
  sleepUntilNextCycle();
}
