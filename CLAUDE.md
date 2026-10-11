# wskit — Claude Context

DIY weather station system. Distributed sensor nodes publish readings over MQTT to a self-hosted, containerized hub that stores them in MySQL and serves a live web dashboard.

Primary goals going forward: software robustness, documentation cleanup, hardware schematic (CAD), and a 3D-printed Stephenson screen enclosure in PETG for outdoor deployment.

---

## Architecture

```
[ESP32 / MKR1000]                     [Server (Docker Compose, network_mode: host)]
  BME280 and/or DHT22                  ┌──────────────────────────────────────────┐
  reads temp/humidity/pressure         │  mqtt-server: Mosquitto (port 1883)      │
  → publishes JSON via WiFi/MQTT ─────►│  mqtt-subscriber (Python)                │
  (ESP32 deep sleeps between reads)    │    → adds UTC server timestamp           │
                                       │    → inserts into MySQL                  │
                                       │  mysql: MySQL 8.0 (volume mysql-data)    │
                                       │  web/app.js (Express + Socket.IO, 8080)  │
                                       │    → polls MySQL every 5 s for new rows  │
                                       │    → pushes them to browsers via Socket.IO│
                                       │    → REST API for collectors/readings    │
                                       └──────────────────────────────────────────┘
                                                        │
                                           Browser: live dashboard + map,
                                           History page, Settings page
```

**MQTT message (collector → broker):**
```json
{ "id": "38:18:2B:8A:1D:B0", "temperature": 22.1, "humidity": 47.4, "pressure": 1003.6 }
```
`temperature` (°C), `humidity` (%) and `pressure` (hPa) are each omitted when unavailable. The firmware also sends diagnostics that the subscriber logs but doesn't store: `source` (which sensor supplied temp/humidity: `dht22`, `bme280`, …), `dht_error` (DHT22 error code, only when it failed) and `rssi` (dBm).

Collectors publish with **QoS 1** and client ID `wskit-<MAC>`; the ESP32 only sleeps once the broker has acknowledged the message. (With QoS 0 the radio was usually switched off before the message left: ~90% of readings were lost.)

**Subscriber** adds `"time"` (UTC ISO 8601), auto-registers unknown collector IDs in `collectors`, and inserts a row into `sensor_readings`.

MQTT topic: `General` (flat, no namespacing — all collectors share one topic)

---

## Directory Map

```
collector-esp32/          Active Arduino firmware for ESP32 + MKR1000
collector-opi-deprecated/ Orange Pi Python collector — DEPRECATED, do not extend
mqtt-server/              Mosquitto broker (Dockerfile, config, entrypoint that generates the password file from .env)
mqtt-subscriber/          Python bridge: MQTT → MySQL
web/                      Node.js Express + Socket.IO dashboard and REST API
hardware/                 Enclosure CAD drafts (OpenSCAD Stephenson screen, WIP)
img/                      README images (DHT22 wiring diagram, outdated architecture diagram)
schema.sql                MySQL schema (collectors, app_settings, sensor_readings); auto-loaded by the mysql container
docker-compose.yml        Orchestrates mqtt-server, mqtt-subscriber, web, mysql
env_sample                Template for .env (copy and fill before deploying)
server-setup.sh           Interactive server setup: installs Docker, writes .env, configures containerized or external MySQL
```

---

## Key Files

| File | Purpose |
|------|---------|
| `collector-esp32/collector.ino` | Firmware — WiFi, MQTT, sensor auto-detect (BME680/BME280/BMP180) + DHT22, ESP32 deep sleep, conditional compile for ESP32 vs MKR1000 |
| `mqtt-subscriber/subscribe-client.py` | MQTT consumer → UTC timestamp → MySQL insert |
| `web/app.js` | Express server: page routes, MySQL polling → Socket.IO live feed, REST API |
| `web/index.html` | Dashboard — live collector cards + Leaflet map (satellite view), primary collector |
| `web/history.html` | Readings table, filterable by collector, time range and limit |
| `web/settings.html` | Collector metadata (name, location, lat/lng), primary collector; Weather Underground section is a disabled placeholder |
| `web/public/js/header.js` | Shared header/nav |
| `web/public/css/style.css` | Dark theme; breakpoints at 800px and 900px |
| `schema.sql` | Tables: `collectors` (id = MAC), `app_settings` (key/value), `sensor_readings` |
| `docker-compose.yml` | All four containers; all use `network_mode: host`; mysql has a healthcheck others wait on |
| `mqtt-server/mosquitto.conf` | Broker config — port 1883, `allow_anonymous false` |
| `mqtt-server/entrypoint.sh` | Creates `password_file` at container start from `MQTT_*` vars in .env |
| `hardware/stevenson_screen_draft_claude.scad` | Draft Stephenson screen enclosure (OpenSCAD) |

### REST API (`web/app.js`)
| Method + path | Purpose |
|---|---|
| `GET /api/collectors` | All collectors with their latest reading |
| `GET /api/collectors/:id` | One collector |
| `PATCH /api/collectors/:id` | Update `name`, `device_type`, `sensor_type`, `location`, `lat`, `lng` |
| `GET /api/readings?collector_id=&from=&to=&limit=` | Readings, newest first (limit default 100, max 1000) |
| `GET /api/collectors/:id/readings?from=&to=&limit=` | Same, for one collector |
| `GET /api/settings` / `PUT /api/settings` | Key/value app settings (e.g. `primary_collector_id`) |
| `GET /api/config` | Client config (currently `tz`) |

---

## Hardware Support Matrix

### Boards

| Board | Status | Notes |
|-------|--------|-------|
| ESP32 (Espressif) | **Active** | GPIO 2 (LED), GPIO 4 (DHT22), I2C SDA 21 / SCL 22; WiFi.h; deep sleeps between readings |
| Arduino MKR1000 | **Active** | A5 (LED), A6 (DHT22), I2C SDA 11 / SCL 12; WiFi101.h; `#if defined(ARDUINO_SAMD_MKR1000)`; stays awake (`delay()`) — the WINC1500 doesn't reconnect reliably after sleep, so not suitable for batteries |
| Orange Pi Zero | **Deprecated** | pyA20 library unmaintained; GPIO bitbanging fragile |

### Sensors

| Sensor | Status | Notes |
|--------|--------|-------|
| BME280 (Bosch) | **Active** | Temp, humidity, pressure over I2C (0x76/0x77) |
| DHT22 (RHT03) | **Active** | Temp: −40–80°C ±0.5°C; Humidity: 0–100% ±2%; 0.5 Hz max poll rate |
| BME680 / BMP180 | Supported | Auto-detected in place of a BME280 (probe order: BME680 → BME280 → BMP180); BMP180 has no humidity |
| DHT11 | **Deprecated** | Lower accuracy; Orange Pi only |

Sensor priority: DHT22 temp/humidity win when the read is valid; the pressure sensor always supplies pressure and is the fallback for temp/humidity. The `dvarrel/DHT22` library returns −1 / −273 on a failed read (not NAN), so validity is `getLastError()` plus a range check.

The DHT22 is bit-banged (≈27 µs vs 70 µs pulses) and the library doesn't disable interrupts, so the firmware reads it **before** starting WiFi and retries once after 2.1 s (not when the sensor gives no response at all, i.e. none is wired). Reading it while WiFi is active, or from userspace Python as the Orange Pi collector did, produces frequent timing/checksum errors.

### Circuit
- BME280: VIN → 3.3V, GND, SDA/SCL to the board's I2C pins (above)
- DHT22 data pin → MCU GPIO with 10kΩ pull-up to VCC (see `img/dht-circuit.jpg`)
- Optional status LED: MCU GPIO → 220Ω → LED → GND

### Battery power (ESP32, optional)
Adafruit PowerBoost 1000C (5V out → ESP32 VIN, load-sharing charge over its micro-USB) fed by 2× Samsung 30Q 18650 (3000 mAh, 15A) in parallel. Documented in README.md. See "Battery life" below.

---

## Configuration

### .env (copy from env_sample)
```
TZ                               — IANA timezone (e.g. America/New_York); web serves it to clients via /api/config
MQTT_PUB_USER / MQTT_PUB_PASS    — collector credentials (also hardcoded in collector.ino)
MQTT_SUB_USER / MQTT_SUB_PASS    — mqtt-subscriber credentials
DB_HOST / DB_NAME / DB_USER / DB_PASS / MYSQLDB_ROOT_PASSWORD
```

### Hardcoded values that need editing before firmware compile (collector.ino)
```cpp
char ssid[] = "";          // WiFi SSID
char pass[] = "";          // WiFi password
const char broker[] = "";  // MQTT broker IP
const char mqttUser[] = ""; const char mqttPass[] = "";
const unsigned long publishInterval = 60;  // seconds between readings (ESP32 deep sleeps in between)

// Optional static IP (ESP32 only). Leave staticIP at 0.0.0.0 to use DHCP.
IPAddress staticIP(0, 0, 0, 0);     // e.g. 192.168.3.13 — reserve it on the router or pick one outside the DHCP pool
IPAddress gateway(0, 0, 0, 0);      // router IP, e.g. 192.168.3.1 (required when staticIP is set)
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(0, 0, 0, 0);          // 0.0.0.0 = use the gateway
```
There is no OTA provisioning — credentials require recompile and reflash.

Firmware libraries: ArduinoMqttClient, DHT22 (dvarrel), Adafruit BME280, Adafruit BME680, Adafruit BMP085 (all three Adafruit libs are needed to compile), WiFi101 (MKR1000 only).

### Battery life (ESP32)
The ESP32 deep sleeps between readings; each wake runs `setup()` → `runCycle()` → deep sleep. The AP's BSSID/channel and the network failure counter live in `RTC_DATA_ATTR` memory, so wakes skip the WiFi scan. Setting a **static IP is the biggest optional battery improvement**: it skips DHCP, which measured at >1 s per wake. On a DevKit with a 60 s interval, wake time dropped from ~1.5–2.0 s (DHCP) to ~0.5 s (static IP). The remaining drain on that hardware is mostly the board's always-on overhead (regulator, LEDs, PowerBoost), not the firmware.

### Timezone
`TZ` in .env. Readings are stored in UTC (the subscriber generates timestamps explicitly in UTC); the frontend converts using `/api/config`.

### Firewall ports to open on server
- 1883 TCP — MQTT
- 8080 TCP — Web dashboard

---

## Known Issues & Technical Debt

### Bugs
- **Rows stored before the pressure fix may have `pressure = 0`** from collectors without a pressure sensor (the subscriber now stores NULL). Clean up with `UPDATE sensor_readings SET pressure = NULL WHERE pressure = 0;`.
- **Serial Monitor garbles the first wake after flashing** (ESP32): repeated 32-byte chunks of output. It's an Arduino IDE monitor artifact, not the firmware; later wakes print cleanly.

### Technical Debt
- **No web authentication**: Dashboard *and* the write endpoints (`PATCH /api/collectors/:id`, `PUT /api/settings`) are open to anyone who can reach port 8080.
- **No MQTT topic namespacing**: All collectors publish to `General`.
- **No tests**: none for any component.
- **Hardcoded credentials in firmware**: No WiFi provisioning (e.g., WiFiManager) or NVS-based config.
- **Live feed polls MySQL** every 5 s rather than being pushed from the subscriber.
- **BME280 runs in normal mode**: Adafruit's `begin()` default keeps it measuring continuously (~0.6 mA, slight self-heating), including while the ESP32 sleeps. Forced mode would fix both.
- **Weather Underground upload**: UI placeholder in Settings, not implemented.

---

## Roadmap

See README.md "Feature Roadmap" for the user-facing checklist. Additional engineering items:

### V1
- [ ] WiFi provisioning for ESP32 (WiFiManager library) — no more hardcoded credentials
- [ ] Historical chart on the History page (e.g., Chart.js); data already available via `/api/readings`
- [ ] MQTT topic namespacing by location/collector ID
- [ ] Remove `collector-opi-deprecated/`
- [ ] 3D-printed enclosure: Stephenson screen design in PETG for outdoor placement (draft in `hardware/`)
- [ ] Schematic: proper CAD circuit diagram (KiCad or similar) replacing raster JPG
- [ ] BME280 forced mode

### V2 (Future)
- [ ] Solar power design for collector nodes
- [ ] AQI / particulate matter sensor support
- [ ] Modular hub architecture (multiple independent locations)
- [ ] OTA firmware updates
- [ ] Web auth (at minimum HTTP basic auth in front of dashboard and API)
- [ ] Weather Underground upload
- [ ] Branding / product identity

---

## Development Conventions

- **JSON data interchange** for all MQTT payloads; Socket.IO pushes MySQL rows as JSON strings.
- **Conditional compilation** in firmware: `#if defined(ARDUINO_SAMD_MKR1000)` gates MKR1000-specific code; everything else assumes ESP32. Sleep, fast reconnect and static IP are ESP32-only.
- **Collector ID** = MAC address (switched from IP in commit `af63481`). Primary key of `collectors`.
- **Units**: stored as °C / % / hPa; the frontend displays temperature in °F.
- **All four server services** run in Docker with `network_mode: host` — no Docker bridge network. Services communicate via localhost.
- **No linting or formatter** configured for any language in this repo.
- **Arduino sketch folder**: arduino-cli requires the folder name to match the `.ino` name, so `collector-esp32/collector.ino` can't be compiled in place; copy it into a matching sketch folder.

---

## Quick Start

```bash
# Server
bash server-setup.sh        # or: cp env_sample .env, fill it in, then:
docker compose up --build -d

# Dashboard
open http://<server-ip>:8080

# Firmware
# 1. Open collector-esp32/collector.ino in Arduino IDE
# 2. Fill in ssid, pass, broker, mqttUser, mqttPass (optionally staticIP + gateway)
# 3. Install libraries (see Configuration above)
# 4. Select board (ESP32 Dev Module or MKR1000) and the USB port (/dev/ttyUSB0 for CP2102 boards), then upload

# Logs
docker compose logs -f
```
