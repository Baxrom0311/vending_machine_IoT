# MQTT API Reference

Firmware target: `esp32_main` (`v2.4.0-main` line).

This document is aligned with the current implementation in:
- `src_esp32_main/mqtt_handler.cpp`
- `src_esp32_main/main.cpp`
- `src_esp32_main/config.cpp`
- `src_esp32_main/sensors.cpp`
- `src_esp32_main/diagnostics.cpp`

## Connection Profile

| Setting | Value |
| :--- | :--- |
| Protocol | MQTT v3.1.1 over TCP |
| Broker | `deviceConfig.mqtt_broker` |
| Port | `deviceConfig.mqtt_port` (default `1883`) |
| Client ID | `deviceConfig.device_id` |
| Username/Password | Optional (`mqtt_username`, `mqtt_password`) |
| KeepAlive | 60s |
| Socket timeout | 8s |
| MQTT buffer size | 512 bytes |

## Topic Matrix

`<DEVICE_ID>` below means `deviceConfig.device_id`.

| Topic | Direction | Enabled by default | Notes |
| :--- | :--- | :--- | :--- |
| `vending/<DEVICE_ID>/payment/in` | Cloud -> Device | Yes | Main payment entrypoint |
| `vending/<DEVICE_ID>/config/in` | Cloud -> Device | No | Compile-time disabled in current firmware profile |
| `vending/broadcast/config` | Cloud -> Device | No | Compile-time disabled |
| `vending/broadcast/command` | Cloud -> Device | No | Compile-time disabled |
| `vending/group/<GROUP_ID>/config` | Cloud -> Device | No | Compile-time disabled |
| `vending/group/<GROUP_ID>/command` | Cloud -> Device | No | Compile-time disabled |
| `vending/<DEVICE_ID>/status/out` | Device -> Cloud | Yes | Retained status snapshot |
| `vending/<DEVICE_ID>/heartbeat` | Device -> Cloud | Yes | Online health pulse |
| `vending/<DEVICE_ID>/log/out` | Device -> Cloud | Yes | Operational logs/events |
| `vending/<DEVICE_ID>/tds/out` | Device -> Cloud | Yes | TDS sample publish |
| `vending/<DEVICE_ID>/diagnostics` | Device -> Cloud | On diagnostic publish | Health report |
| `vending/<DEVICE_ID>/telemetry` | Device -> Cloud | Reserved | Topic generated, currently not published |
| `vending/<DEVICE_ID>/alerts` | Device -> Cloud | Reserved | Topic generated, currently not published |

To enable inbound config/fleet topics, set `kMqttInboundConfigEnabled` and/or
`kMqttInboundFleetEnabled` in `src_esp32_main/mqtt_handler.cpp`, then rebuild and reflash.

## Inbound Payloads

### 1) Payment In (`vending/<DEVICE_ID>/payment/in`)

Required field:
- `amount` (`int`, `1..1000000`)

Optional fields:
- `source` (`string`)
- `transaction_id` (`string`)
- `nonce` (`string`, used as fallback transaction id)
- `user_id` (`string`)
- `ts` (`uint64`, required when signed mode is enabled)
- `sig` or `auth.sig` (required when signed mode is enabled)

Example:
```json
{
  "amount": 5000,
  "source": "app",
  "transaction_id": "TXN_0001",
  "user_id": "user_42",
  "ts": 1700000000000,
  "sig": "hex_hmac_sha256"
}
```

### 2) Config In (`vending/<DEVICE_ID>/config/in`) - Disabled by default

Only works if inbound config is enabled in firmware build. When enabled:
- partial updates are supported
- network keys (`wifi*`, `mqtt*`) apply only when `allowRemoteNetworkConfig=true`
- unsupported/restricted keys are ignored with log
- `apply: "restart"` saves and reboots
- network apply has rollback if not healthy within 30s

Common accepted operational keys:
- `pricePerLiter` / `price_per_liter`
- `sessionTimeout` / `session_timeout` (seconds or ms)
- `freeWaterCooldown` / `free_water_cooldown` (seconds or ms)
- `freeWaterAmount` / `free_water_amount`
- `tdsThreshold` / `tds_threshold`
- `enableFreeWater`
- `heartbeatInterval` (5000..3600000 ms)
- `apply` (`"now"` or `"restart"`)

### 3) Broadcast / Group Config & Command - Disabled by default

When fleet inbound is enabled:
- Config supports selected keys (currently `pricePerLiter`, `tdsThreshold`)
- Command supports:
  - `updateTdsThreshold`
  - `emergencyShutdown`

## Outbound Payloads

### 1) Heartbeat (`vending/<DEVICE_ID>/heartbeat`)
```json
{
  "status": "online",
  "uptime": 12345,
  "ip": "192.168.1.50",
  "rssi": -61,
  "ssid": "OfficeWiFi",
  "firmware_version": "2.4.0-main",
  "free_heap": 189432
}
```

### 2) Status (`vending/<DEVICE_ID>/status/out`)
```json
{
  "device_id": "VendingMachine_001",
  "state": "DISPENSING",
  "balance": 2500,
  "last_dispense": 1.42,
  "tds": 96,
  "free_water_available": false
}
```

### 3) Log (`vending/<DEVICE_ID>/log/out`)
```json
{
  "device_id": "VendingMachine_001",
  "event": "PAYMENT",
  "message": "5000|app|TXN_0001|user_42"
}
```

### 4) TDS (`vending/<DEVICE_ID>/tds/out`)
```json
{
  "device_id": "VendingMachine_001",
  "tds": 102
}
```

### 5) Diagnostics (`vending/<DEVICE_ID>/diagnostics`)
```json
{
  "timestamp": 123456,
  "components": {
    "flowSensor": true,
    "tdsSensor": true,
    "cashAcceptor": true,
    "relay": true,
    "display": true,
    "wifi": true,
    "mqtt": true
  },
  "failureCount": 0,
  "failedComponents": []
}
```

## Security: Signing and Replay

If `requireSignedMessages=false`:
- Signature is not required.

If `requireSignedMessages=true`:
- `api_secret` must be set on device.
- Signature: HMAC-SHA256 hex.
- Signature field can be either:
  - `sig`
  - `auth.sig`

Replay checks:
- Payment: rejects duplicate `transaction_id`/`nonce` seen in current boot (RAM cache size 8).
- Config/Command: persistent nonce+timestamp hash cache in NVS (ring size 16 per context).

Signing guidance:
- Signed config/command canonicalization supports both camelCase and snake_case aliases.
- Avoid sending both variants of the same field in one message.

## QoS and Retain

- Library in use: `PubSubClient`.
- Publish QoS: QoS 0 (library default).
- Retained:
  - `status/out`: retained `true`
  - `log/out`, `heartbeat`, `tds/out`, `diagnostics`: retained `false`

## Reconnect and Recovery Behavior

- Reconnect backoff sequence: `5s`, `10s`, `20s`, `60s`, `120s`, `300s` (capped).
- Reconnect attempts only run when WiFi is connected.
- Main loop avoids reconnect attempts during active dispensing (`IDLE`-only reconnect attempts).
- If WiFi is connected but MQTT remains unhealthy for 15 minutes in `IDLE`, firmware restarts WiFi/MQTT stack (with 10-minute recovery cooldown).

## Quick Test Commands

Replace `<BROKER>` and `<DEVICE_ID>` with real values.

```bash
mosquitto_sub -h <BROKER> -t "vending/<DEVICE_ID>/#" -v
```

```bash
mosquitto_pub -h <BROKER> \
  -t "vending/<DEVICE_ID>/payment/in" \
  -m '{"amount":5000,"source":"app","transaction_id":"TXN_TEST_01"}'
```
