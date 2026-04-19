# temperature-data-collector
An open-source DIY weather station, consisting of lightweight collectors and a Dockerized sensor Hub (MQTT + web dashboard). Currently collects temperature and humidity data, while barometric pressure and air quality are on the roadmap.

This is an all-in-one project which includes: collectors for various hardware platforms, an MQTT system (server, subscribers, publishers), MySQL database, a web server to monitor live data, and an API to query historical data.

There are two components: Collector, and Server.

### Collector

- Arduino sensor collector

OR

- Orange Pi sensor collector (deprecated)

### Server ("Hub")

- MQTT server

- MQTT subscriber

- Node.js web server

- MySQL server

![Container diagram](temperature-data-collector-1.jpg)


## Hardware

### Supported Boards

- [Arduino MKR1000](https://store-usa.arduino.cc/collections/boards/products/arduino-mkr1000-wifi-with-headers-mounted)

- [ESP32](https://www.espressif.com/en/products/socs/esp32)

- [Orange Pi (Zero)](https://a.co/d/6ztEWGC) (deprecated)


### Sensor

- [DHT22 sensor](https://www.adafruit.com/product/385)

	- DHT11s were supported in the Orange Pi version, but will not be supported going forward.

- [BME280 sensor](https://www.adafruit.com/product/2652)

	- For barometric pressure. Not supported yet, but will be shortly! Probably will replace the DHT22.


### Miscellaneous

- Breadboard

- Jumper wires

- 10k ohm resistor (pull-up)

- Optional: Status LED and 220 ohm resistor


### Enclosures

Coming soon


### DHT circuit diagram

![DHT circuit](dht-circuit.jpg)

[Image source](https://osoyoo.com/2017/07/19/arduino-lesson-dht11-sensor/)


## How it runs

Collector code is in the `collector` or `collector-esp32` directory.

Server is everything else (web, mysql, etc) and can be run via docker-compose (or each component standalone).

Collectors send data via MQTT to the server. The MQTT subscriber writes to MySQL. The web server reads from MySQL for live display. There are API endpoints to retrieve historical readings and to view/edit collectors.


## Getting started

Server:

1. Clone and enter this repository on your server

2. If you have a firewall, two TCP ports need to be opened: 1883 for receiving collector messages, and 8080 for the web server.

3. Copy and fill in the environment file:
   ```bash
   cp env_sample .env
   ```
   At minimum, set the four MQTT passwords and the three MySQL credentials:
   ```
   MQTT_PUB_PASS=your_publisher_password
   MQTT_SUB_PASS=your_subscriber_password
   DB_USER=your_db_user
   DB_PASS=your_db_password
   MYSQLDB_ROOT_PASSWORD=your_root_password
   ```

4. Run `docker-compose up --build`

5. Server is ready. Dashboard: `http://server_ip:8080`

MySQL is included by default — the `sensor_readings` table is created automatically from `sensordump.sql` on first boot.


### Using an external MySQL instance

1. Comment out the `mysql` service in `docker-compose.yml` and remove its `depends_on` entry from `mqtt-subscriber`
2. Set `DB_HOST` to your external MySQL host in `.env`
3. Create the schema on your external instance:
   ```bash
   mysql -h your_host -u your_user -p -e "CREATE DATABASE IF NOT EXISTS sensordata;"
   mysql -h your_host -u your_user -p sensordata < sensordump.sql
   ```

---

Collector (ESP32 / Arduino MKR1000):
1. Build the circuit in the "Circuit diagram" section above, with a 10k ohm resistor, DHT sensor, and your board. Have the data pin going to the proper GPIO port.

2. In the [Arduino IDE](https://www.arduino.cc/en/software/), choose your board, and install these prerequisite libraries from the Library Manager:
- ArduinoMqttClient ([source](https://github.com/arduino-libraries/ArduinoMqttClient))
- DHT22 ([source](https://github.com/dvarrel/DHT22))
- WiFi101 (if using Arduino MKR1000) ([source](https://docs.arduino.cc/libraries/wifi101))

3. Fill in your credentials at the top of `collector-esp32/collector.ino` (lines 18–23):
   ```cpp
   char ssid[] = "";           // your WiFi network name
   char pass[] = "";           // your WiFi password

   const char broker[] = "";   // your server's IP address
   const char mqttUser[] = ""; // MQTT_PUB_USER from .env  (default: publisher)
   const char mqttPass[] = ""; // MQTT_PUB_PASS from .env
   ```

4. Flash the code in `collector-esp32/collector.ino` to your board.


Collector (Orange Pi):

1. Build the circuit in the "Circuit diagram" section above, with a 10k ohm resistor, DHT sensor, and Orange Pi. Have the data pin going to the proper GPIO port (the collector defaults to PA6)

2. Connect the Orange Pi to a network. WiFi makes the device more useful, you can use a wifi config tool such as nmtui. For first-time setup, or if your device doesn't have nmtui installed yet, connect via Ethernet first.

3. On the Orange Pi, clone this repository and run collector/collector.py. Edit the script if you would like to change the sensor model, GPIO port, sample rate, etc.

4. You should get temperature and humidity values as output, and the data will be published via MQTT for the listening server.


## Feature Wishlist

For V1:

- Server: finish credential management (.env files, and their usage in docker-compose)

- Server: finish setup script

- Server: MySQL export functionality in MQTT subscriber

- Collector: Model files for casing / outdoor placement

- Collector: Credential management

- Collector: Barometric pressure sensor

- Collector: Batteries

- Collector: Phase out / fully deprecate Orange Pi version

- Branding


For V2:

- Collector: Solar

- Collector: AQI sensors

- Server: Modular "hub" to make platform more accessible