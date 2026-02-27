# MQTT Setup Guide (Current Firmware)

This guide matches current `esp32_main` firmware behavior.

## 1. Broker Information

Current default broker in project config:
- Host: `ec2-3-72-68-85.eu-central-1.compute.amazonaws.com`
- Port: `1883`
- Auth: optional (`mqtt_username` / `mqtt_password`)

Check broker service on server:
```bash
sudo systemctl status mosquitto
sudo journalctl -u mosquitto -f
```

## 2. Configure Device MQTT Credentials

Do not hardcode broker/user/pass in source files.
Set via Serial config (Main ESP32):

```text
SET_WIFI:<ssid>:<password>
SET_MQTT:<broker>:<port>
SET_MQTT_AUTH:<user>:<pass>
SET_DEVICE_ID:<device_id>
APPLY_CONFIG
SAVE_CONFIG
GET_STATUS
```

Default device id in storage is usually `VendingMachine_001`.
All topics are generated from this device id.

## 3. Current Inbound Profile (Important)

By default firmware subscribes only to:
- `vending/<DEVICE_ID>/payment/in`

`config/in`, `broadcast/*`, and `group/*` inbound control topics are compile-time disabled in the current profile.
To enable them, change `kMqttInboundConfigEnabled` / `kMqttInboundFleetEnabled`
in `src_esp32_main/mqtt_handler.cpp` and reflash firmware.

## 4. Quick End-to-End Test

### 4.1 Subscribe
```bash
mosquitto_sub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com \
  -p 1883 \
  -t "vending/VendingMachine_001/#" \
  -v
```

### 4.2 Publish test payment
```bash
mosquitto_pub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com \
  -p 1883 \
  -t "vending/VendingMachine_001/payment/in" \
  -m '{"amount":5000,"source":"app","transaction_id":"TXN_TEST_001"}'
```

### 4.3 Expected outgoing topics
- `vending/VendingMachine_001/log/out`
- `vending/VendingMachine_001/status/out`
- `vending/VendingMachine_001/heartbeat`
- `vending/VendingMachine_001/tds/out`

## 5. Signed Message Mode (Optional)

If you enable signed mode:
```text
SET_API_SECRET:<secret>
SET_REQUIRE_SIGNED:1
SAVE_CONFIG
```

Then payment payload must include valid signature and replay fields:
- `sig` (or `auth.sig`)
- `ts`
- `transaction_id` (or `nonce`)

Example payload shape:
```json
{
  "amount": 5000,
  "transaction_id": "TXN_SIGNED_001",
  "ts": 1700000000000,
  "sig": "hex_hmac_sha256"
}
```

## 6. Production Broker Hardening

### 6.1 Enable username/password
```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd vending_user
```

`/etc/mosquitto/mosquitto.conf`:
```conf
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Restart:
```bash
sudo systemctl restart mosquitto
```

### 6.2 Firewall
Open MQTT port in security group/firewall:
- TCP `1883` (or `8883` if TLS enabled)

### 6.3 Optional TLS
Use `listener 8883` in Mosquitto and certificate chain.
Current firmware uses `WiFiClient` (non-TLS) by default; TLS requires firmware-side secure client migration.

## 7. Runtime Behavior You Should Know

- KeepAlive: 60s
- Socket timeout: 8s
- Reconnect backoff: 5s -> 10s -> 20s -> 60s -> 120s -> 300s
- Reconnect attempts run only when WiFi is connected
- Main loop avoids reconnect attempts during active dispensing
- If WiFi is up but MQTT stays unhealthy for long time (IDLE state), firmware performs network self-recovery

## 8. Troubleshooting

### No MQTT connection
1. Verify `SET_MQTT` host/port and `SET_MQTT_AUTH`.
2. Verify broker reachability from same network.
3. Verify firewall/security group rules.
4. Check device serial logs and broker logs together.

### Payment publish sent but device does nothing
1. Check topic includes exact `device_id`.
2. Ensure JSON contains `amount` as integer.
3. If signed mode is on, ensure `sig` + `ts` + `transaction_id/nonce` are present and valid.

### You sent `config/in` but nothing changes
That topic is disabled in current inbound profile by default.

## 9. Reference

- Full topic/payload contract: `docs/MQTT_API.md`
