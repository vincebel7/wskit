-- temperature-data-collector schema
-- Database: sensordata

CREATE TABLE IF NOT EXISTS `collectors` (
  `id`          VARCHAR(17)   NOT NULL,         -- MAC address (xx:xx:xx:xx:xx:xx)
  `name`        VARCHAR(64)   DEFAULT NULL,      -- human-readable label, e.g. "Back Porch"
  `device_type` VARCHAR(32)   DEFAULT NULL,      -- e.g. "ESP32", "MKR1000"
  `sensor_type` VARCHAR(64)   DEFAULT NULL,      -- e.g. "DHT22", "DHT22+BME280"
  `location`    VARCHAR(128)  DEFAULT NULL,
  `created_at`  TIMESTAMP     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS `sensor_readings` (
  `id`           INT           NOT NULL AUTO_INCREMENT,
  `collector_id` VARCHAR(17)   NOT NULL,
  `recorded_at`  TIMESTAMP     NOT NULL,
  `temperature`  DECIMAL(6,2)  DEFAULT NULL,     -- degrees Celsius
  `humidity`     DECIMAL(5,2)  DEFAULT NULL,      -- percent relative humidity
  `pressure`     DECIMAL(8,2)  DEFAULT NULL,      -- hPa (BME280)
  PRIMARY KEY (`id`),
  FOREIGN KEY (`collector_id`) REFERENCES `collectors` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
