
scp .pio\build\esp32dev/firmware.bin hass.lan:/var/www/html/garagelock
ssh hass.lan "mosquitto_pub -t garagelock/updates -m http://hass.lan/garagelock/firmware.bin"
ssh hass.lan "mosquitto_pub -t gl/ud -m http://hass.lan/garagelock/firmware.bin"
