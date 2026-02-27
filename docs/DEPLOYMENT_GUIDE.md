# 🚀 Deployment Guide (Dual ESP32)

This guide covers building and flashing **both** firmwares:

* **ESP32 #2 — Main Controller** (`esp32_main`): WiFi/MQTT, display, relay, sensors
* **ESP32 #1 — Payment Controller** (`esp32_payment`): cash acceptor pulses, UART sender

---

## 1. Build (Release)

Both environments already use `-Os` and dead-code elimination in `platformio.ini`.

```bash
pio run -t clean
pio run -e esp32_main
pio run -e esp32_payment
```

Build outputs:
* `/.pio/build/esp32_main/firmware.bin`
* `/.pio/build/esp32_payment/firmware.bin`

> Production tip: set `ENABLE_DEBUG_LOGS=0` in `[env:esp32_main]` to reduce serial noise.

---

## 2. Generate “full flash” images (optional)

This creates a single file that includes **bootloader + partitions + firmware**.

```bash
python3 scripts/merge_firmware.py --env esp32_main
python3 scripts/merge_firmware.py --env esp32_payment
```

Outputs:
* `scripts/build/full_firmware_esp32_main.bin`
* `scripts/build/full_firmware_esp32_payment.bin`

---

## 3. Flashing

### Option A: PlatformIO (recommended)
```bash
pio run -e esp32_main -t upload
pio run -e esp32_payment -t upload
```

### Option B: Esptool CLI (factory / automation)

Main Controller:
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000 .pio/build/esp32_main/bootloader.bin \
  0x8000 .pio/build/esp32_main/partitions.bin \
  0x10000 .pio/build/esp32_main/firmware.bin
```

Payment Controller:
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000 .pio/build/esp32_payment/bootloader.bin \
  0x8000 .pio/build/esp32_payment/partitions.bin \
  0x10000 .pio/build/esp32_payment/firmware.bin
```

---

## 4. Post-Deployment Verification

1. **Power On**
   - Main: LCD shows boot/status; Serial prints firmware version.
   - Payment: LED blinks ready; Serial prints “Waiting for cash…”.
2. **UART link** (3.3V TTL)
   - Wiring: TX↔RX crossed + common GND.
   - Insert cash → Main should log `UART Payment received ... (seq=...)`.
3. **Flow sensor**
   - Start dispensing → pulses reduce balance and total liters increase.
4. **MQTT**
   - Main publishes `vending/<DEVICE_ID>/heartbeat`, `status/out`, `log/out`, `tds/out`.
   - Current inbound profile accepts `vending/<DEVICE_ID>/payment/in` by default.
