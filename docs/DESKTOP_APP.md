# 🖥️ eWater Device Manager (Desktop App)

The **Device Manager** is a cross-platform (Windows/Mac/Linux) tool to configure eWater devices via USB or Cloud.

![App Screenshot](https://via.placeholder.com/800x400?text=eWater+Device+Manager+UI)

> Note: This project has **two ESP32 firmwares**:
> - `esp32_main` (Main Controller, WiFi/MQTT/LCD/Relay)
> - `esp32_payment` (Payment Controller, cash pulses → UART)
>
> Make sure you flash the correct board with the correct `.bin`.

## 🎛️ Controller Selection (Main vs Payment)

In the left sidebar choose **Target Controller**:

- **Main ESP32 (WiFi/MQTT)**: Serial config + firmware flash + serial monitor + **Online Setup (MQTT/OTA)**.
- **Payment ESP32 (Cash Only)**: **Firmware flash + Serial monitor only** (no configuration commands).

## 📦 Installation

1.  **Requirements**: Node.js v18+ installed.
2.  **Setup**:
    ```bash
    cd desktop-app
    npm install
    ```
3.  **Run Dev Mode**:
    ```bash
    npm start
    ```
4.  **Build Binary**:
    ```bash
    npm run build:mac  # or build:win
    ```

---

## 🔌 Serial Mode (USB)
*Use this for initial setup or network recovery.*

1.  Connect ESP32 to computer via USB.
2.  Select **Port** (e.g., `/dev/ttyUSB0` or `COM3`) and click **Connect**.
3.  (Main ESP32 only) Use tabs:
    - **Basic Config**: WiFi/MQTT/Device ID → `Load Basic` / `Save Basic`
    - **Extra Config**: vending/sensors/security/intervals → `Load Extra` / `Save Extra`
    - Security flags are configured in **Extra Config**: `API Secret`, `Require Signed MQTT`, `Allow Remote Network Config`, `Group ID`.
5.  **Firmware Update**: Select `.bin` file and click "Flash Firmware".
    - Main firmware: `.pio/build/esp32_main/firmware.bin`
    - Payment firmware: `.pio/build/esp32_payment/firmware.bin`
    - Optional “full flash” images: `scripts/build/full_firmware_esp32_main.bin` / `scripts/build/full_firmware_esp32_payment.bin`

---

## ☁️ Online Mode (MQTT)
*Use this for remote management.*

> Online Setup is available only for **Main ESP32 (WiFi/MQTT)**.

1.  Enter your **MQTT Broker** credentials.
2.  Click **Connect**.
3.  Select a device from the **Live Devices** list.
4.  **Remote Config**:
    - **Basic Config** tab sends WiFi/MQTT fields (only if device allows remote network config).
    - **Extra Config** tab sends vending/sensor/interval fields.
    - If the device has `Require Signed MQTT` enabled, enter the **same API Secret** in the form so the app can sign messages.
    - The app automatically includes `ts` + `nonce` for replay protection.
    - Network fields (WiFi/MQTT broker/auth) are applied only if the device allows remote network config.
5.  **OTA Update**: 
    - Start the local OTA Server (hosts firmware file).
    - Select device and click "Send OTA Command".

---

## 🔧 Troubleshooting

| Error | Solution |
| :--- | :--- |
| **"Serial Port Busy"** | Close other apps (Arduino IDE, Serial Monitor) using the port. |
| **"MQTT Timeout"** | Check internet connection and Broker IP/Port. Firewall? |
| **"OTA Failed"** | Ensure computer and ESP32 are on the same network (unless public IP used). |
