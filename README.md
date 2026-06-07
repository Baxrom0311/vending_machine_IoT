# eWater - Local Water Vending Firmware

Dual-ESP32 local firmware for a cash-based water vending machine.

- **ESP32 #2 Main Controller**: relay/valve, flow sensor, TDS sensor, TFT display, buttons, optional WiFi status, UART receiver.
- **ESP32 #1 Payment Controller**: cash acceptor pulse reader and UART sender.

The project is local-first. Cash pulses are counted on the Payment ESP32, sent to the Main ESP32 over UART, and credited once using sequence IDs and ACKs.

## Features

- Flow-sensor based dispensing with balance deduction.
- Cash acceptor integration through the Payment ESP32.
- UART payment protocol with duplicate protection.
- TFT status display for price, balance, remaining water and payment link state.
- Serial command configuration through the ESP32 USB serial monitor.
- Hardware watchdog and safe-mode handling.

## Structure

```bash
eWater/
├── src_esp32_main/     # Main Controller firmware
├── src_esp32_payment/  # Payment Controller firmware
├── shared/             # Shared UART protocol
├── scripts/            # Firmware merge helpers
├── test/               # Native unit tests
└── platformio.ini
```

## Build

```bash
pio run -e esp32_main
pio run -e esp32_payment
```

## Flash

```bash
pio run -e esp32_main -t upload
pio run -e esp32_payment -t upload
```

## Monitor

```bash
pio device monitor -e esp32_main
```

Live payment logs with automatic USB serial port detection:

```bash
scripts/monitor_logs.sh esp32_payment
```

The script also saves the same output into `logs/`.

Useful serial commands:

- `GET_CONFIG`
- `SET_DEVICE_ID:name`
- `SET_PRICE:amount`
- `SET_CASH_PULSE:value`
- `SET_CASH_GAP:ms`
- `SET_PULSES_PER_LITER:value`
- `SET_RELAY_ACTIVE:1|0`
- `SAVE_CONFIG`
- `APPLY_CONFIG`
- `GET_STATUS`
- `RESTART`

## Tests

```bash
pio test -e native_test
```

## Hardware Notes

- UART wiring: Payment TX to Main RX, Payment RX to Main TX, and common GND.
- NV9USB+ must be configured to Pulse mode.
- NV9USB+ power: +12V to validator power, GND common with both ESP32 boards.
- NV9USB+ pulse output: Vend pulse/open-collector output to Payment ESP32 GPIO32 with a 3.3V pull-up or level shifter.
- Do not connect any 12V signal directly to ESP32 GPIO32.
- NV9USB+ inhibit/enable lines must be in the accept-enabled state, otherwise bills will be rejected before any pulse reaches ESP32.
- Relay polarity defaults to active HIGH. Use `SET_RELAY_ACTIVE:0` then `SAVE_CONFIG` for active LOW relay modules.
- `START` begins dispensing only when balance is greater than zero.
