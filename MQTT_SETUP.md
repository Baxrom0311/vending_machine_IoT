# AWS Mosquitto MQTT Broker - Setup Guide

## 📡 Your Broker Info

**Hostname:** `ec2-3-72-68-85.eu-central-1.compute.amazonaws.com`  
**IP:** `172.31.32.48` (internal)  
**Port:** `1883` (MQTT)  
**Authentication:** Disabled (default)

---

## ✅ Broker Status

```bash
● mosquitto.service - Mosquitto MQTT Broker
     Active: active (running) ✓
```

---

## 🧪 Testing Commands

### From AWS Server (SSH)

```bash
# Subscribe to all vending machine topics
mosquitto_sub -h localhost -t "vending/#" -v

# Subscribe to specific device
mosquitto_sub -h localhost -t "vending/device_001/#" -v

# Send test payment
mosquitto_pub -h localhost \
  -t "vending/device_001/payment/in" \
  -m '{"amount":5000,"source":"cash","transaction_id":"TEST_123"}'

# Update config
mosquitto_pub -h localhost \
  -t "vending/device_001/config/in" \
  -m '{"pricePerLiter":1200}'
```

### From Your Mac (Remote)

```bash
# Subscribe (from your laptop)
mosquitto_sub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com \
  -p 1883 \
  -t "vending/#" \
  -v

# Publish (from your laptop)
mosquitto_pub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com \
  -p 1883 \
  -t "vending/device_001/payment/in" \
  -m '{"amount":3000,"source":"payme"}'
```

---

## 🔐 Security Setup (Recommended for Production)

### 1. Enable Authentication

```bash
# SSH to AWS
ssh -i mosquitto-key.pem ubuntu@ec2-3-72-68-85.eu-central-1.compute.amazonaws.com

# Create password file
sudo mosquitto_passwd -c /etc/mosquitto/passwd vending_user
# Enter password when prompted

# Update config
sudo nano /etc/mosquitto/mosquitto.conf
```

Add these lines:
```
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Restart:
```bash
sudo systemctl restart mosquitto
```

Update ESP32 `config.cpp`:
```cpp
const char* MQTT_USERNAME = "vending_user";
const char* MQTT_PASSWORD = "your_password";
```

### 2. Enable TLS/SSL (Optional)

```bash
# Generate certificates
sudo apt install certbot
sudo certbot certonly --standalone -d your-domain.com

# Configure in mosquitto.conf
listener 8883
cafile /etc/letsencrypt/live/your-domain.com/chain.pem
certfile /etc/letsencrypt/live/your-domain.com/cert.pem
keyfile /etc/letsencrypt/live/your-domain.com/privkey.pem
```

Update ESP32:
```cpp
const int MQTT_PORT = 8883;
// Add WiFiClientSecure instead of WiFiClient
```

---

## 🔥 Firewall Configuration

**IMPORTANT:** Make sure port 1883 is open in AWS Security Group

### Check Current Rules:

AWS Console → EC2 → Security Groups → Your Instance

### Required Inbound Rules:

| Type | Protocol | Port | Source | Description |
|------|----------|------|--------|-------------|
| Custom TCP | TCP | 1883 | 0.0.0.0/0 | MQTT |
| SSH | TCP | 22 | Your IP | Admin access |

### Add Rule via AWS CLI:

```bash
aws ec2 authorize-security-group-ingress \
  --group-id sg-xxxxx \
  --protocol tcp \
  --port 1883 \
  --cidr 0.0.0.0/0
```

---

## 📊 Monitoring

### View Logs

```bash
# Real-time log monitoring
sudo journalctl -u mosquitto -f

# Last 100 lines
sudo journalctl -u mosquitto -n 100

# Specific time range
sudo journalctl -u mosquitto --since "1 hour ago"
```

### Check Connections

```bash
# Show active connections
sudo netstat -tulnp | grep 1883

# Show mosquitto process
ps aux | grep mosquitto
```

---

## 🧪 ESP32 Testing Workflow

### 1. Start Monitoring on AWS

```bash
ssh -i mosquitto-key.pem ubuntu@ec2-3-72-68-85.eu-central-1.compute.amazonaws.com
mosquitto_sub -h localhost -t "vending/device_001/#" -v
```

### 2. Flash ESP32

```bash
cd /Users/baxrom/Documents/PlatformIO/Projects/eWater
pio run --target upload
pio device monitor
```

### 3. Watch for Connection

You should see on AWS:
```
vending/device_001/log/out {"device_id":"VendingMachine_001","timestamp":"...","event":"MQTT","message":"Connected"}
vending/device_001/heartbeat {"timestamp":"...","uptime":0}
```

### 4. Send Test Payment

From AWS or your Mac:
```bash
mosquitto_pub -h localhost \
  -t "vending/device_001/payment/in" \
  -m '{"amount":5000,"source":"cash","transaction_id":"TXN_001"}'
```

Watch ESP32 serial monitor for:
```
Payment received: 5000 from cash
Transaction ID: TXN_001
```

And AWS subscriber should see:
```
vending/device_001/status/out {"device_id":"...","state":1,"balance":5000,...}
vending/device_001/log/out {"event":"PAYMENT","message":"5000|cash|TXN_001"}
```

---

## 🐛 Troubleshooting

### ESP32 Can't Connect

1. **Check WiFi:**
   ```cpp
   // In config.cpp - update these
   const char* WIFI_SSID = "YourWiFi";
   const char* WIFI_PASSWORD = "YourPassword";
   ```

2. **Verify broker address:**
   ```bash
   ping ec2-3-72-68-85.eu-central-1.compute.amazonaws.com
   ```

3. **Check firewall:** Port 1883 must be open

4. **Test from computer first:**
   ```bash
   mosquitto_pub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com \
     -t "test" -m "hello"
   ```

### Mosquitto Not Running

```bash
sudo systemctl status mosquitto
sudo systemctl restart mosquitto
sudo systemctl enable mosquitto  # Start on boot
```

### Permission Denied

```bash
sudo chown -R mosquitto:mosquitto /var/log/mosquitto
sudo chown -R mosquitto:mosquitto /run/mosquitto
```

---

## 📈 Production Checklist

- [ ] Enable authentication (username/password)
- [ ] Enable TLS/SSL encryption
- [ ] Set up proper firewall rules
- [ ] Configure log rotation
- [ ] Set up monitoring/alerts
- [ ] Backup configuration
- [ ] Document credentials securely
- [ ] Set up automatic updates
- [ ] Configure persistence (message retention)

### Persistence Configuration

Add to `/etc/mosquitto/mosquitto.conf`:
```
persistence true
persistence_location /var/lib/mosquitto/
autosave_interval 1800
```

---

## 🎯 Next Steps

1. **Test locally first:**
   ```bash
   mosquitto_sub -h ec2-3-72-68-85.eu-central-1.compute.amazonaws.com -t "vending/#" -v
   ```

2. **Flash ESP32:**
   ```bash
   pio run --target upload
   ```

3. **Monitor both sides:**
   - AWS: `mosquitto_sub -h localhost -t "#" -v`
   - ESP32: `pio device monitor`

4. **Send test messages** and verify bidirectional communication

---

**Broker Ready:** ✅  
**ESP32 Config Updated:** ✅  
**Ready to Flash:** ✅
