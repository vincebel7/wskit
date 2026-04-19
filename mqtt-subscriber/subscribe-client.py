"""
mqtt-subscriber.py
Author: vincebel7
Purpose: Subscribes to the MQTT server and redirects messages to other services, such as Redis
"""

import datetime
import paho.mqtt.client as mqtt
import time
import redis
import json
import os
import pathlib
import sqlalchemy as db
from sqlalchemy import text
from dotenv import load_dotenv

file_path = pathlib.Path(__file__).parent.resolve()
load_dotenv(str(file_path) + '/../.env')

USE_MYSQL = os.getenv("USE_MYSQL", "false").lower() == "true"
MYSQL_HOST = str(os.getenv("DB_HOST"))
MYSQL_DB = str(os.getenv("DB_NAME"))
MYSQL_USER = str(os.getenv("DB_USER"))
MYSQL_PASS = str(os.getenv("DB_PASS"))
MYSQL_PORT = 3306

MQTT_BROKER = "localhost"
MQTT_SUB_USER = os.getenv("MQTT_SUB_USER")
MQTT_SUB_PASS = os.getenv("MQTT_SUB_PASS")
MQTT_PORT = 1883

REDIS_HOST = "localhost"
REDIS_DB = 0
REDIS_CHANNEL = "DHT-data"
REDIS_PORT = 6379

global Connected
Connected = False

def on_connect(mqtt_client, userdata, flags, rc):
    if rc == 0:
        print("Subscriber connected to MQTT server")
        Connected = True
        mqtt_client.subscribe("General")
    else:
        print("Connection failed")


def on_subscribe(mqtt_client, userdata, message, idk):
    print("Subscribed to MQTT server")


def on_message(mqtt_client, userdata, message_str):
    payload = message_str.payload.decode('utf8')
    jsonload = json.loads(payload)

    # Add a server-side timestamp
    jsonload['time'] = datetime.datetime.now(datetime.timezone.utc).isoformat()

    jsondump = json.dumps(jsonload)
    print("Data received: " + jsondump)
    redis_client.publish(REDIS_CHANNEL, str(jsondump))
    if USE_MYSQL:
        insert_mysql(jsonload)


def insert_mysql(data):
    try:
        with engine.begin() as conn:
            conn.execute(text(
                "INSERT IGNORE INTO collectors (id) VALUES (:id)"
            ), {"id": data["id"]})
            conn.execute(text("""
                INSERT INTO sensor_readings
                    (collector_id, recorded_at, temperature, humidity, pressure)
                VALUES
                    (:collector_id, :recorded_at, :temperature, :humidity, :pressure)
            """), {
                "collector_id": data["id"],
                "recorded_at":  data["time"],
                "temperature":  data.get("temperature"),
                "humidity":     data.get("humidity"),
                "pressure":     data.get("pressure", 0),
            })
    except Exception as e:
        print(f"MySQL insert failed: {e}")


# MySQL connection
if USE_MYSQL:
    print("Connecting to MySQL...")
    mysql_conn_string = f"mysql://{MYSQL_USER}:{MYSQL_PASS}@{MYSQL_HOST}:{MYSQL_PORT}/{MYSQL_DB}"
    engine = db.create_engine(mysql_conn_string)
    engine.connect().close()  # validate credentials at startup
    print("MySQL connection OK")

# Redis connection
redis_client = redis.StrictRedis(REDIS_HOST, REDIS_PORT, REDIS_DB)

# MQTT connection
mqtt_client = mqtt.Client("Temperature-Humidity-Subscriber-1")
mqtt_client.username_pw_set(username=MQTT_SUB_USER, password=MQTT_SUB_PASS)

mqtt_client.on_connect = on_connect
mqtt_client.on_subscribe = on_subscribe
mqtt_client.on_message = on_message

mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)

mqtt_client.loop_start()

while Connected != True:
    time.sleep(0.1)

try:
    while True:
        time.sleep(1)

except KeyboardInterrupt:
    print("Exiting...")
    mqtt_client.disconnect()
    mqtt_client.loop_stop()
