FROM debian

EXPOSE 1883

RUN apt-get update && apt-get --yes install mosquitto

RUN mkdir -p /var/run/mosquitto && chown mosquitto: /var/run/mosquitto

COPY mosquitto.conf /etc/mosquitto/conf.d/mosquitto.conf
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["mosquitto", "-c", "/etc/mosquitto/conf.d/mosquitto.conf"]
