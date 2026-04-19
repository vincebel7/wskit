-- temperature-data-collector schema
-- Database: sensordata

CREATE TABLE IF NOT EXISTS `sensor_readings` (
  `id`           INT           NOT NULL AUTO_INCREMENT,
  `collector_id` VARCHAR(50)   DEFAULT NULL,   -- MAC address of the collector unit
  `date`         TIMESTAMP     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `sensor`       VARCHAR(50)   DEFAULT NULL,   -- e.g. "DHT22"
  `location`     VARCHAR(255)  DEFAULT NULL,   -- optional human-readable label
  `temperature`  DECIMAL(6,2)  DEFAULT NULL,   -- degrees Celsius
  `humidity`     DECIMAL(5,2)  DEFAULT NULL,   -- percent relative humidity
  `pressure`     DECIMAL(8,2)  DEFAULT NULL,   -- hPa (reserved for BME280)
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
