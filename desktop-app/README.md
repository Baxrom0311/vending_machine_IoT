# eWater Device Manager (Desktop App)

Electron-based tool to manage eWater devices:
- **Serial Setup** (USB): configure + flash firmware
- **Online Setup** (MQTT): remote config + OTA

## Install / Run

```bash
cd desktop-app
npm install
npm start
```

## Serial Setup (USB)

- **Connect** to the ESP32 port, then **Load from Device** (`GET_CONFIG`).
- Edit settings, then **Save to Device** (sends `SET_*` commands + `SAVE_CONFIG`).
- **Firmware Flash**:
  - **App Only** writes at `0x10000` (safe for normal `firmware.bin`).
  - **Full Firmware** writes at `0x0000` (only use with `full_firmware_*.bin` that includes bootloader + partitions).

## Online Setup (MQTT)

- Connect to your broker, select a device from **Live Devices** (heartbeats).
- **Apply Config** publishes to `vending/<ID>/config/in`.
- **OTA Update** starts a local HTTP server and publishes `vending/<ID>/ota/in`.

### Signing / Replay protection

If the device has **Require Signed MQTT** enabled:
- Set **API Secret** via **Serial Setup**.
- In **Online Setup**, enter the same API secret so the app can add `sig`.
- The app automatically includes `ts` + `nonce` for replay protection.

## Build

```bash
npm run build:mac
npm run build:win
```

