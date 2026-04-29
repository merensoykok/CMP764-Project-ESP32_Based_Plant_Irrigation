# Project 

This project consists of an ESP32 device and a Flask-based backend server.

## ESP32 Installation

To process and run the ESP32, you need to install libraries from the Arduino IDE's Library Manager:

- **Adafruit_Unified_Sensor** (by Adafruit)
- **ArduinoJson** (by Benoit Blanchon)
- **DHT_sensor_library** (by Adafruit)
- **WiFi101** (by Arduino)


## Backend 

  ```bash
    cd backend
    python -m venv .venv
    .venv\Scripts\activate
    pip install Flask
    python main.py
   ```
