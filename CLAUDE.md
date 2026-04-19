# temperature-data-collector — Claude Context

DIY weather station system. Distributed sensor nodes publish readings over MQTT to a self-hosted, containerized hub that streams live data to a web dashboard.

Primary goals going forward: software robustness, documentation cleanup, hardware schematic (CAD), and a 3D-printed Stephenson screen enclosure in PETG for outdoor deployment.

---

## Architecture

```
[ESP32 / MKR1000]                    [Server (Docker Compose)]
  DHT22 sensor                        ┌─────────────────────────────────┐
  reads temp + humidity               │  Mosquitto MQTT  (port 1883)    │
  → publishes JSON via WiFi/MQTT ────►│  mqtt-subscriber (Python)        │
                                      │    → adds server timestamp       │
                                      │    → publishes to Redis          │
                                      │  Redis  (channel: DHT-data)      │
                                      │  web/app.js  (Express + Socket.IO│
                                      │    port 8080)                    │
                                      └─────────────────────────────────┘
                                                  │
                                          Browser dashboard
                                       (live scrolling log)
```

**MQTT message (collector → broker):**
```json
{ "id": "MAC_ADDRESS", "temperature": 23.5, "humidity": 45.2, "pressure": 0 }
```

**After subscriber enrichment (→ Redis):**
```json
{ "id": "...", "temperature": 23.5, "humidity": 45.2, "pressure": 0, "time": "2025-04-19T14:30:45+00:00" }
```

MQTT topic: `General` (flat, no namespacing — all collectors share one topic)

---

## Directory Map

```
collector/             Orange Pi Python collector — DEPRECATED, do not extend
collector-esp32/       Active Arduino firmware for ESP32 + MKR1000
mqtt-server/           Mosquitto broker (Dockerfile + config + password files)
mqtt-subscriber/       Python bridge: MQTT → Redis (+ stubbed MySQL export)
redis/                 Redis container (Dockerfile + minimal config)
web/                   Node.js Express + Socket.IO live dashboard
sensordump.sql         MySQL schema (dht11_data table) — stubbed, not active
docker-compose.yml     Orchestrates all four server-side services
env_sample             Template for .env (copy and fill before deploying)
server-setup.sh        Partial server setup script (marked WIP)
dht-circuit.jpg        DHT22 breadboard wiring diagram (raster, not editable)
temperature-data-collector-1.jpg  Container architecture diagram
```

---

## Key Files

| File | Purpose |
|------|---------|
| `collector-esp32/collector.ino` | Main firmware — WiFi, MQTT, DHT22 reading, conditional compile for ESP32 vs MKR1000 |
| `mqtt-subscriber/subscribe-client.py` | MQTT consumer → timestamps → Redis pub; MySQL insert stubbed |
| `web/app.js` | Express server; subscribes to Redis, pushes to clients via Socket.IO |
| `web/index.html` | Dashboard UI — Socket.IO client, dark/light mode, live data log |
| `web/public/css/style.css` | Styling; light/dark themes, mobile breakpoint at 800px |
| `collector/collector.py` | Deprecated Orange Pi collector (pyA20 GPIO) |
| `collector/dht.py` | Low-level DHT bitbang implementation for Orange Pi |
| `docker-compose.yml` | All four containers; all use `network_mode: host` |
| `mqtt-server/mosquitto.conf` | Broker config — port 1883, auth required |
| `mqtt-server/password_file` | Hashed MQTT credentials (publisher + subscriber users) |

---

## Hardware Support Matrix

### Boards

| Board | Status | Notes |
|-------|--------|-------|
| ESP32 (Espressif) | **Active** | GPIO 2 (LED), GPIO 4 (DHT22); WiFi.h |
| Arduino MKR1000 | **Active** | A5 (LED), A6 (DHT22); WiFi101.h; `#if defined(ARDUINO_SAMD_MKR1000)` |
| Orange Pi Zero | **Deprecated** | pyA20 library unmaintained; GPIO bitbanging fragile |

### Sensors

| Sensor | Status | Notes |
|--------|--------|-------|
| DHT22 (RHT03) | **Active** | Temp: −40–80°C ±0.5°C; Humidity: 0–100% ±2%; 0.5 Hz max poll rate |
| DHT11 | **Deprecated** | Lower accuracy; Orange Pi only |
| BME280 (Bosch) | **Planned** | Pressure + altitude; not yet coded anywhere |

### Circuit (from dht-circuit.jpg)
- DHT22 data pin → MCU GPIO with 10kΩ pull-up to VCC
- Optional status LED: MCU GPIO → 220Ω → LED → GND
- Power: 3.3V or 5V depending on board

---

## Configuration

### .env (copy from env_sample)
```
MQTT_PUB_USER / MQTT_PUB_PASS   — credentials for collectors
MQTT_SUB_USER / MQTT_SUB_PASS   — credentials for mqtt-subscriber
USE_MYSQL=False                  — set True to attempt MySQL writes (stub still incomplete)
DB_HOST / DB_NAME / DB_USER / DB_PASS / MYSQLDB_ROOT_PASSWORD
```

### Hardcoded values that need editing before firmware compile (collector.ino)
```cpp
char ssid[] = "";          // WiFi SSID
char pass[] = "";          // WiFi password
const char broker[] = "";  // MQTT broker IP
const char mqttUser[] = ""; const char mqttPass[] = "";
const unsigned long publishInterval = 10;  // seconds between readings
```
There is no OTA provisioning — credentials require recompile and reflash.

### Timezone
Hardcoded `TZ=America/Detroit` in both `mqtt-subscriber/Dockerfile` and `web/Dockerfile`. Should become an env var.

### Firewall ports to open on server
- 1883 TCP — MQTT
- 8080 TCP — Web dashboard

---

## Known Issues & Technical Debt

### Bugs
- **Frontend memory leak** (`web/index.html`): `<span>` elements appended to `#databox` indefinitely; browser will eventually slow with many collectors running for days. Needs a max-item cap or virtual scroll.
- **`polldb()` dead code** (`web/index.html:51–58`): async function called but never resolves; left over from a MySQL polling approach. Remove it.
- **DHT22 temperature overflow fallback** (`collector/dht.py:182–185`): `if c > 125: c = mybytes[2]` — undefined behavior on edge values; only matters for the deprecated Orange Pi path but worth noting.

### Technical Debt
- **MySQL export not implemented**: `insert_mysql()` in `mqtt-subscriber/subscribe-client.py` is a stub. Schema exists in `sensordump.sql` (table named `dht11_data` — anachronistic). `USE_MYSQL` env var has no effect.
- **No data persistence**: Redis is ephemeral. Restarting containers loses all history. No historical query capability.
- **No REST API**: Only Socket.IO real-time push. No endpoint to fetch historical data or query by time range.
- **No web authentication**: Dashboard is open to anyone who can reach port 8080.
- **No MQTT topic namespacing**: All collectors publish to `General`. No location or unit separation at the broker level.
- **No tests**: `pytest` is in requirements; `.pytest_cache` present; no test files exist.
- **Hardcoded credentials in firmware**: No WiFi provisioning (e.g., WiFiManager) or EEPROM-based config.
- **`server-setup.sh` is incomplete**: MySQL sections are commented out and marked TODO.
- **Schema naming mismatch**: MySQL table is `dht11_data` but active sensor is DHT22.
- **`app-mysql.js`**: Legacy file in `web/`; not used, not wired into Docker. Reference only — can be deleted once MySQL is properly implemented.

---

## Roadmap

### V1 (Near-term operationalization)
- [ ] MySQL persistence — implement `insert_mysql()` properly; rename table
- [ ] BME280 support in firmware (replaces DHT22 for pressure data)
- [ ] WiFi provisioning for ESP32 (WiFiManager library) — no more hardcoded credentials
- [ ] Historical data: REST API + frontend chart (e.g., Chart.js) for time-range queries
- [ ] Timezone as env var (not hardcoded in Dockerfiles)
- [ ] Frontend databox cap — remove old entries to prevent memory growth
- [ ] MQTT topic namespacing by location/collector ID
- [ ] Remove `collector/` (Orange Pi) or clearly gate it behind a deprecation warning
- [ ] 3D-printed enclosure: Stephenson screen design in PETG for outdoor placement
- [ ] Schematic: proper CAD circuit diagram (KiCad or similar) replacing raster JPG
- [ ] Remove dead code: `polldb()` in index.html, `app-mysql.js`

### V2 (Future)
- [ ] Solar power design for collector nodes
- [ ] AQI / particulate matter sensor support
- [ ] Modular hub architecture (multiple independent locations)
- [ ] OTA firmware updates
- [ ] Web auth (at minimum HTTP basic auth in front of dashboard)
- [ ] Branding / product identity

---

## Development Conventions

- **JSON data interchange** throughout — all MQTT payloads and Redis messages are JSON strings.
- **Conditional compilation** in firmware: `#if defined(ARDUINO_SAMD_MKR1000)` gates MKR1000-specific pin assignments; everything else assumes ESP32.
- **Collector ID** = MAC address (switched from IP in commit `af63481`). Used as a device identifier in the dashboard.
- **Temperature display** = Fahrenheit on frontend (converted from °C). Stored internally as °C.
- **All four server services** run in Docker with `network_mode: host` — no Docker bridge network. Services communicate via localhost.
- **No linting or formatter** configured for any language in this repo.

---

## Quick Start

```bash
# Server
cp env_sample .env
# fill in MQTT credentials in .env
docker-compose up --build

# Dashboard
open http://<server-ip>:8080

# Firmware
# 1. Open collector-esp32/collector.ino in Arduino IDE
# 2. Fill in ssid, pass, broker, mqttUser, mqttPass
# 3. Install libraries: ArduinoMqttClient, DHT sensor library, WiFi (or WiFi101)
# 4. Select board (ESP32 Dev Module or MKR1000) and flash

# Logs
docker-compose logs -f
```
