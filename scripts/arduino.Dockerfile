# Arduino build environment for the Heltec WiFi LoRa 32 V3 farm nodes:
# Espressif esp32 core + Heltec's "Heltec ESP32 Dev-Boards" library (LoRaWan_APP.h,
# HT_SSD1306Wire.h, Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE)).
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl python3 python3-serial \
    && rm -rf /var/lib/apt/lists/*
ARG ARDUINO_CLI_VERSION=1.1.1
ARG ESP32_CORE_VERSION=3.0.7
RUN curl -fsSL "https://downloads.arduino.cc/arduino-cli/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
    | tar -xz -C /usr/local/bin arduino-cli
ENV ARDUINO_DIRECTORIES_DATA=/opt/arduino ARDUINO_DIRECTORIES_USER=/opt/arduino/user \
    ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS=https://espressif.github.io/arduino-esp32/package_esp32_index.json
RUN arduino-cli core update-index && arduino-cli core install "esp32:esp32@${ESP32_CORE_VERSION}" \
    && arduino-cli lib install "Heltec ESP32 Dev-Boards" "PubSubClient" "DHT sensor library" "Adafruit Unified Sensor" \
    && chmod -R a+rwX /opt/arduino
WORKDIR /src
