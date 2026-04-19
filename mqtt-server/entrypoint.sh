#!/bin/sh
set -e

echo "Generating MQTT password file..."
mosquitto_passwd -c -b /etc/mosquitto/conf.d/password_file "$MQTT_SUB_USER" "$MQTT_SUB_PASS"
mosquitto_passwd    -b /etc/mosquitto/conf.d/password_file "$MQTT_PUB_USER" "$MQTT_PUB_PASS"
chown mosquitto:mosquitto /etc/mosquitto/conf.d/password_file

exec "$@"
