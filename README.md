# wskit

wskit is an open-source, DIY **weather station kit** — lightweight sensor collectors and a self-hosted, Dockerized hub that streams live temperature and humidity data to a web dashboard. Barometric pressure and air quality are on the roadmap.

## How it works

A **Collector** (ESP32 or Arduino MKR1000) reads a DHT22 sensor and publishes JSON readings over WiFi via MQTT. The **Server** (Docker Compose) receives those readings, stores them in MySQL, and serves a live dashboard.

![Container diagram](temperature-data-collector-1.jpg)

---

## Getting Started

### Server

1. Clone this repository onto your server.

2. Open TCP ports on your firewall if needed:
   - `1883` — MQTT (collector → broker)
   - `8080` — Web dashboard

3. Run the setup script:
   ```bash
   bash server-setup.sh
   ```
   This will install Docker if needed, prompt for credentials, configure MySQL, and start all services.

4. Open the dashboard at `http://server_ip:8080`.

---

### Collector (ESP32 / Arduino MKR1000)

1. Build the circuit shown in the [DHT circuit diagram](#dht-circuit-diagram) below, with a 10kΩ pull-up resistor and DHT22 sensor.

2. In the [Arduino IDE](https://www.arduino.cc/en/software/), select your board and install these libraries from the Library Manager:
   - [ArduinoMqttClient](https://github.com/arduino-libraries/ArduinoMqttClient)
   - [DHT22](https://github.com/dvarrel/DHT22)
   - [WiFi101](https://docs.arduino.cc/libraries/wifi101) *(MKR1000 only)*

3. Fill in your credentials at the top of `collector-esp32/collector.ino` (lines 18–23):
   ```cpp
   char ssid[] = "";            // your WiFi network name
   char pass[] = "";            // your WiFi password

   const char broker[] = "";    // your server's IP address
   const char mqttUser[] = "";  // MQTT_PUB_USER from .env  (default: publisher)
   const char mqttPass[] = "";  // MQTT_PUB_PASS from .env
   ```

4. Flash `collector-esp32/collector.ino` to your board.

<details>
<summary>Orange Pi collector (deprecated)</summary>

1. Build the circuit with a 10kΩ resistor and DHT sensor. The collector defaults to GPIO pin PA6.
2. Connect the Orange Pi to a network (use `nmtui` for WiFi; Ethernet for first-time setup).
3. Clone this repository on the Orange Pi and run `collector/collector.py`.

</details>

---

## Hardware

### Boards

| Board | Status | Notes |
|-------|--------|-------|
| [ESP32](https://www.espressif.com/en/products/socs/esp32) | **Active** | GPIO 4 (DHT22), GPIO 2 (LED) |
| [Arduino MKR1000](https://store-usa.arduino.cc/collections/boards/products/arduino-mkr1000-wifi-with-headers-mounted) | **Active** | A6 (DHT22), A5 (LED) |
| [Orange Pi Zero](https://a.co/d/6ztEWGC) | Deprecated | PA6 (DHT22); pyA20 library unmaintained |

### Sensors

| Sensor | Status | Notes |
|--------|--------|-------|
| [DHT22](https://www.adafruit.com/product/385) | **Active** | Temp: −40–80°C ±0.5°C; Humidity: 0–100% ±2% |
| [BME280](https://www.adafruit.com/product/2652) | Planned | Barometric pressure; not yet coded |

### Miscellaneous parts

- Breadboard + jumper wires
- 10kΩ resistor (pull-up for DHT22 data line)
- Optional: status LED + 220Ω resistor

### Enclosures

Coming soon: a 3D-printable Stephenson screen design in PETG for outdoor placement.

### DHT circuit diagram

![DHT circuit](dht-circuit.jpg)

[Image source](https://osoyoo.com/2017/07/19/arduino-lesson-dht11-sensor/)

---

## Feature Roadmap

**v1.0**
- Collector: Barometric pressure sensor (BME280)
- Collector: WiFi credential management (no more hardcoded values)
- Collector: Batteries + model files for casing / outdoor placement
- Deprecate / remove Orange Pi collector

**v2.0**
- Collector: Solar power
- Collector: AQI sensors
- Server: Graph/chart generation
- Server: Modular hub architecture (multiple locations)
