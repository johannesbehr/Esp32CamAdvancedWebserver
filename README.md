# Esp32CamAdvancedWebserver  

**Esp32CamAdvancedWebserver** is an Arduino-based project designed for the **AI Thinker ESP32-CAM** board.  
It combines the functionality of several popular libraries into a single, powerful application — providing a feature-rich webserver for camera streaming, file management, system control, and GPIO/servo interaction.  

---

## ✨ Features  

- 📷 **Camera Webserver**  
  Live video stream and snapshots directly from the ESP32-CAM.  

- 📂 **WebDAV File Server (AsyncWebdav)**  
  - Full SD card file management over WebDAV.  
  - Compatible with Windows, macOS, and Linux network drive mounting.  

- 🌐 **Web Interface (ESPAsyncWebServer)**  
  - Access the camera stream.  
  - Manage system settings.  
  - Perform OTA firmware updates.  
  - Browse, upload, edit, and delete files on the SD card.  

- ⚡ **SCPI-Linq Integration**  
  - Control ESP32 GPIOs and servos directly from JavaScript in the browser.  
  - Provides a simple command interface for hardware interaction.  

---

## 🛠 Requirements  

- **Board:** AI Thinker ESP32-CAM  
- **Arduino IDE** (or PlatformIO) with **ESP32 board support** installed  
- **Libraries:**  
  - [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)  
  - [AsyncWebdav](https://github.com/your-library-link) *(if public)*  
  - [SCPI-Linq](https://github.com/your-library-link) *(if public)*  
  - [ArduinoJson](https://arduinojson.org/)  
  - Camera driver (built into ESP32 Arduino core)  

---

## 🚀 Getting Started  

1. Clone this repository:  
   ```bash
   git clone https://github.com/yourusername/Esp32CamAdvancedWebserver.git
   ```  

2. Open the project in Arduino IDE or PlatformIO.  

3. Select **AI Thinker ESP32-CAM** as the target board.  

4. Configure your **WiFi credentials** in the project (or via SD card configuration, if enabled).  

5. Upload the sketch to your ESP32-CAM.  

6. Open the serial monitor to find the assigned IP address.  

7. Access the web interface in your browser:  
   ```
   http://<your-esp32-ip>/
   ```  

---

## 📡 Web Interface Overview  

- **Camera Tab** → Live video stream & snapshots  
- **System Tab** → Device info, reboot, firmware update  
- **Files Tab** → Browse, upload, edit, delete SD card files (WebDAV also available)  
- **SCPI Tab** → Send SCPI-style commands via JavaScript for GPIO/servo control  

---

## ⚙️ Configuration  

- WiFi and system configuration can be stored on the SD card in a JSON file.  
- Multiple WiFi networks (up to 5) can be configured; the ESP32 will automatically connect to the best available network.  
- Supports both **DHCP** and **static IP** settings.  

---

## 📖 License  

This project is released under the **MIT License**.  
You are free to use, modify, and distribute it under the terms of the license.  
