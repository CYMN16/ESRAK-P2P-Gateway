# P2P beacon module

# TODO
- Use AP instead of STA Wi-Fi mode

- Create an interface for the web server

- Format the P2P data received

# Objectives

## Utilize both cores with freeRTOS 

### `First Core`: 

Web server for beacon interface

- Connect to internet using Wi-Fi

- Web interface for beacon

- Open server for interface

### `Second Core`:


Communication with RAK3172 for P2P LoRa communication/receiving LoRa packets

- Serial communication between ESP32 and RAK3172

- Write AT commands via Serial to RAK3172

- Read output from RAK3172 Serial