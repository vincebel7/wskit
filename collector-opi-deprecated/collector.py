import dht
import time
import datetime
import json
import os
import pathlib
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from pyA20.gpio import gpio
from pyA20.gpio import port

file_path = pathlib.Path(__file__).parent.resolve()
load_dotenv(str(file_path) + '/../.env')

MQTT_BROKER = "192.168.5.235"
MQTT_PUB_USER = os.getenv("MQTT_SUB_USER")
MQTT_PUB_PASS = os.getenv("MQTT_SUB_PASS")
MQTT_PORT = 1883

#initialize GPIO
PIN = port.PA6
gpio.init()

#instance = dht.DHTSensor(pin=PIN2, sensor="DHT11")
instance = dht.DHTSensor(pin=PIN, sensor="DHT22")

client = mqtt.Client("Temperature-Humidity-Publisher-1")
client.username_pw_set(username=MQTT_PUB_USER, password=MQTT_PUB_PASS)
client.connect(MQTT_BROKER, MQTT_PORT, 60)

def build_message(response):
    temperature = str(response.temperature)
    humidity = str(response.humidity)
    time = datetime.datetime.now().isoformat()

    # dictionary
    msg = {
        "time": time,
        "temperature": temperature,
        "humidity": humidity,
    }

    return msg

# read data using pin port.PA6
def read_sensor():
    response = instance.read_pin()

    if response.error_code == 0:
        msg = build_message(response)
        print("Temperature: " + msg.get("temperature"))
        print("Humidity: " + msg.get("humidity"))

        # Can't publish dict, so convert to string and convert back on subscriber
        stringed_msg = str(json.dumps(msg))
        client.publish("General", stringed_msg)
    else:
        print("Error: %d" % response.error_code)

while True:
    read_sensor()
    time.sleep(2) #2 seconds, maximum poll rate for DHT22 is 0.5 Hz
