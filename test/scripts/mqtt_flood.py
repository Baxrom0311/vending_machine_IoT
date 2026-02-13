import paho.mqtt.client as mqtt
import time
import json
import random
import threading
import argparse

# Configuration
BROKER = "localhost" # Default, user can override
PORT = 1883
TOPIC_PAYMENT = "water/payment"
TOPIC_CONFIG = "water/config"
DEVICE_ID = "VendingMachine_001"

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")

def send_payment(client, count):
    for i in range(count):
        amount = random.randint(100, 5000)
        payload = {
            "amount": amount,
            "source": "load_test",
            "transaction_id": f"txn_{int(time.time())}_{i}",
            "ts": int(time.time() * 1000)
        }
        client.publish(TOPIC_PAYMENT, json.dumps(payload))
        # No sleep to simulate flood
        if i % 100 == 0:
            print(f"Sent {i} payments...")

def send_config(client, count):
    for i in range(count):
        payload = {
            "deviceId": DEVICE_ID,
            "pricePerLiter": random.randint(500, 2000),
            "ts": int(time.time() * 1000)
        }
        client.publish(TOPIC_CONFIG, json.dumps(payload))
        if i % 100 == 0:
            print(f"Sent {i} config updates...")

def main():
    parser = argparse.ArgumentParser(description='MQTT Load Tester for eWater')
    parser.add_argument('--broker', default='localhost', help='MQTT Broker Address')
    parser.add_argument('--port', type=int, default=1883, help='MQTT Broker Port')
    parser.add_argument('--count', type=int, default=1000, help='Number of messages to send')
    parser.add_argument('--type', choices=['payment', 'config', 'mixed'], default='payment', help='Type of load')
    
    args = parser.parse_args()
    
    client = mqtt.Client()
    client.on_connect = on_connect
    
    print(f"Connecting to {args.broker}:{args.port}...")
    try:
        client.connect(args.broker, args.port, 60)
    except Exception as e:
        print(f"Failed to connect: {e}")
        return

    client.loop_start()
    
    start_time = time.time()
    
    if args.type == 'payment':
        send_payment(client, args.count)
    elif args.type == 'config':
        send_config(client, args.count)
    elif args.type == 'mixed':
        t1 = threading.Thread(target=send_payment, args=(client, args.count // 2))
        t2 = threading.Thread(target=send_config, args=(client, args.count // 2))
        t1.start()
        t2.start()
        t1.join()
        t2.join()
        
    duration = time.time() - start_time
    print(f"Sent {args.count} messages in {duration:.2f} seconds ({args.count/duration:.2f} msg/s)")
    
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
