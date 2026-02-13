# 📡 MQTT API Reference

**eWater Firmware v2.4.0**  <!-- Updated to match current firmware -->

This document provides a comprehensive reference for the MQTT interface used by eWater devices.

---

## 🔐 Authentication & Security

### 1. Broker Connection
*   **Protocol**: MQTT v3.1.1 (TCP)
*   **ClientId**: `<DEVICE_ID>` (device uses `deviceConfig.device_id`)
*   **Username/Password**: As configured in device settings.
*   **KeepAlive**: 60 seconds

### 2. Message Signing (HMAC-SHA256)
If `requireSignedMessages` is enabled on the device, critical commands (**Payment**, **Config**, **OTA**, **Broadcast**) MUST be signed.

**Replay Protection (ts/nonce)**
- **Payment**: require `ts` + `transaction_id` (or `nonce`)
- **Config / OTA / Broadcast**: require `ts` + `nonce` (or `transaction_id`)
- Device rejects reused IDs to prevent replay attacks.

**Algorithm**:
1.  Canonicalize the JSON payload (or use specific fields).
2.  Compute `HMAC-SHA256(payload, api_secret)`.
3.  Append signature to payload as `"sig"` or `"auth": {"sig": "..."}`.

**Python Example**:
```python
import hmac, hashlib, json

secret = b"my_secure_api_secret"
payload = '{"amount":5000,"ts":1700000000000,"device_id":"dev01"}'
signature = hmac.new(secret, payload.encode(), hashlib.sha256).hexdigest()

# Send this
final_payload = {
    "amount": 5000,
    "ts": 1700000000000,
    "device_id": "dev01",
    "sig": signature
}
```

---

## 📥 Subscribe Topics (Device Listens)

### 1. Payment (`vending/<ID>/payment/in`)
Authorize a dispense operation.
*   **Payload**:
    ```json
    {
      "amount": 5000,            // (Required) Amount in currency
      "source": "app",           // "app", "qr", "card"
      "transaction_id": "tx123", // Unique ID for de-duplication
      "nonce": "tx123",          // Alias for transaction_id (optional)
      "user_id": "user_01",      // Optional user tracking
      "ts": 1700000000000,       // Timestamp (Required if signing)
      "sig": "abcdef1234..."     // Signature
    }
    ```

### 2. Configuration (`vending/<ID>/config/in`)
Update device settings.
*   **Payload (Partial updates supported)**:
    ```json
    {
      "pricePerLiter": 1200,
      "wifiSsid": "NewWiFi",
      "wifiPassword": "pass",
      "sessionTimeout": 300,
      "enableFreeWater": true,
      "apply": "now",            // "now" or "restart"
      "nonce": "cfg_001",        // Required if signing (replay protection)
      "ts": 1700000000000,
      "sig": "..."
    }
    ```

### 3. OTA Update (`vending/<ID>/ota/in`)
Trigger remote firmware update.
*   **Payload**:
    ```json
    {
      "firmware_url": "http://server.com/fw/v1.0.0.bin", // HTTP only supported
      "nonce": "ota_001",        // Required if signing (replay protection)
      "ts": 1700000000000,
      "sig": "..."
    }
    ```

---

## 📤 Publish Topics (Device Sends)

### 1. Heartbeat (`vending/<ID>/heartbeat`)
Sent periodically to indicate online status.
*   **Payload**:
    ```json
    {
      "status": "online",
      "ip": "192.168.1.100",
      "rssi": -60,
      "ssid": "WiFi_Name",
      "uptime": 3600,
      "firmware_version": "2.4.0-main",
      "free_heap": 12345
    }
    ```

### 2. Status (`vending/<ID>/status/out`)
Sent on state change (e.g., Idle -> Dispensing).
*   **Payload**:
    ```json
    {
      "state": "DISPENSING", // IDLE, ACTIVE, DISPENSING, PAUSED, FREE_WATER
      "balance": 2500,
      "last_dispense": 1.5,  // Liters
      "tds": 85
    }
    ```

### 3. Logs (`vending/<ID>/log/out`)
Debug and verification logs.
*   **Payload**: JSON object.
    ```json
    {
      "device_id": "VendingMachine_001",
      "event": "CONFIG",
      "message": "Updated from backend"
    }
    ```
    Example events: `PAYMENT`, `CONFIG`, `FLEET`, `OTA`, `ERROR`, `ALERT`.

### 4. Telemetry (`vending/<ID>/telemetry`)
Periodic telemetry data for analytics.
*   **Payload**:
    ```json
    {
      "total_dispensed": 12050.5,
      "total_revenue": 5000000,
      "session_count": 50,
      "uptime": 86400
    }
    ```

---

## 📢 Fleet Connectivity (Broadcast & Group)

Commands sent to these topics affect multiple devices.

### 1. Broadcast Config (`vending/broadcast/config`)
Update settings for **ALL** devices.
*   **Payload**: Same as standard Config.
*   **Common Use**: Update price per liter for entire fleet.

### 2. Broadcast Command (`vending/broadcast/command`)
Execute actions on **ALL** devices.
*   **Payload**:
    ```json
    {
      "action": "updatePrice",    // Actions: "updatePrice", "updateTdsThreshold", "identify", "emergencyShutdown"
      "pricePerLiter": 1500,
      "nonce": "cmd_001",         // Required if signing (replay protection)
      "ts": 1700000000000,        // Required if signing
      "sig": "..."
    }
    ```

### 3. Group Config/Command (`vending/group/<GROUP_ID>/...`)
Same as Broadcast, but targets only devices with matching `groupId` (e.g., "building_A").

---

## 📋 Full Configuration Map

| Key | Type | Description |
| :--- | :--- | :--- |
| `pricePerLiter` | `int` | Cost per liter (e.g., 1000). |
| `pulsesPerLiter` | `float` | Flow sensor calibration (default 450.0). |
| `sessionTimeout` | `int` | Session timeout (seconds or ms; firmware normalizes). |
| `freeWaterCooldown` | `int` | Cooldown before free water is available again (seconds or ms). |
| `freeWaterAmount` | `float` | Amount for free dispense (Liters). |
| `enableFreeWater` | `bool` | Enable "Free Water" mode. |
| `tdsThreshold` | `int` | PPM limit for warning. |
| `tdsTemperatureC` | `float` | Temperature used for TDS compensation. |
| `tdsCalibrationFactor` | `float` | Calibration multiplier for TDS. |
| `relayActiveHigh` | `bool` | Relay polarity (true=Active HIGH, false=Active LOW). |
| `cashPulseValue` | `int` | So’m per cash pulse (default 1000). |
| `cashPulseGapMs` | `int` | Gap to close a pulse burst (ms). |
| `paymentCheckInterval` | `int` | Internal interval (ms). |
| `displayUpdateInterval` | `int` | LCD refresh interval (ms). |
| `tdsCheckInterval` | `int` | TDS sampling interval (ms). |
| `heartbeatInterval` | `int` | Heartbeat publish interval (ms). |
| `wifiSsid` | `string` | WiFi Network Name. |
| `wifiPassword` | `string` | WiFi Password. |
| `mqttBroker` | `string` | MQTT Broker Host/IP. |
| `mqttPort` | `int` | MQTT Broker Port (default 1883). |
| `mqttUsername` | `string` | MQTT username (optional). |
| `mqttPassword` | `string` | MQTT password (optional). |
| `deviceId` | `string` | Unique Device ID. |
| `groupId` | `string` | Fleet group id (affects group topics). |

> Note: `requireSignedMessages` and `api_secret` are set via **Serial config** (not via MQTT config update).
