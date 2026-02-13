#include "uart_sender.h"
#include "../shared/uart_protocol.h"
#include "hardware.h"

// ============================================
// CONFIGURATION
// ============================================
#define HEARTBEAT_INTERVAL_MS 10000
#define ACK_TIMEOUT_MS 500 // Reduced from 500ms to minimize blocking
#define MAX_RETRIES 3
#define OFFLINE_BUFFER_SIZE 10

// ============================================
// VARIABLES
// ============================================
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastAckMs = 0;
static bool mainEspConnected = false;

struct PaymentTx {
  int amount;
  uint32_t seq;
};

// Offline buffer - store payments when Main ESP is offline
static PaymentTx offlineBuffer[OFFLINE_BUFFER_SIZE];
static int offlineBufferCount = 0;
static uint32_t nextPaymentSeq = 0; // Will be randomized in initUartSender()

static bool trySendPaymentTx(const PaymentTx &tx) {
  char buffer[64];
  char data[32];

  snprintf(data, sizeof(data), "%d,%lu", tx.amount,
           static_cast<unsigned long>(tx.seq));
  buildMessage(buffer, CMD_PAYMENT, data);

  Serial.print("📤 Sending: ");
  Serial.print(buffer);

  // Try to send with retries
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    Serial2.print(buffer);

    // Wait for ACK
    unsigned long startMs = millis();
    while (millis() - startMs < ACK_TIMEOUT_MS) {
      if (!Serial2.available()) {
        delay(10);
        continue;
      }

      char response[64];
      int len = Serial2.readBytesUntil('\n', response, sizeof(response) - 1);
      response[len] = '\0';

      char cmd[16], respData[32];
      if (!parseMessage(response, cmd, respData)) {
        continue;
      }

      if (strcmp(cmd, CMD_ACK) == 0) {
        const unsigned long ackSeq = strtoul(respData, nullptr, 10);
        lastAckMs = millis();
        mainEspConnected = true;

        if (ackSeq == tx.seq) {
          Serial.println("✓ ACK received");
          return true;
        }

        // ACK for something else (e.g. heartbeat or other message). Keep
        // waiting.
        continue;
      }

      if (strcmp(cmd, CMD_STATUS) == 0) {
        lastAckMs = millis();
        mainEspConnected = true;
        Serial.print("📥 Status: ");
        Serial.println(respData);
      }
    }

    Serial.print("⚠️ No ACK, retry ");
    Serial.println(retry + 1);
  }

  mainEspConnected = false;
  return false;
}

static bool enqueuePaymentTx(const PaymentTx &tx) {
  if (offlineBufferCount >= OFFLINE_BUFFER_SIZE) {
    return false;
  }
  offlineBuffer[offlineBufferCount++] = tx;
  return true;
}

// ============================================
// INITIALIZATION
// ============================================
void initUartSender() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // Flush any stale data in UART buffer
  while (Serial2.available()) {
    Serial2.read();
  }

  // Randomize starting seq to prevent collisions after restart
  // Main ESP tracks recent seq numbers - if we always start at 1,
  // payments get rejected as "duplicates" after restart
  nextPaymentSeq = (micros() & 0xFFFF) + 100; // Range: 100 - 65635

  Serial.print("✓ UART initialized (TX:");
  Serial.print(UART_TX_PIN);
  Serial.print(", RX:");
  Serial.print(UART_RX_PIN);
  Serial.print(") seq_start=");
  Serial.println(nextPaymentSeq);
}

// ============================================
// SEND PAYMENT
// ============================================
bool sendPayment(int amount) {
  if (amount <= 0) {
    return true;
  }

  PaymentTx tx{amount, nextPaymentSeq++};

  // Try immediate send. If it fails, queue exactly once.
  if (trySendPaymentTx(tx)) {
    return true;
  }

  Serial.println("❌ Main ESP offline, buffering payment");
  if (!enqueuePaymentTx(tx)) {
    Serial.println("⚠️ Offline buffer full!");
    return false;
  }

  return true;
}

// ============================================
// SEND HEARTBEAT
// ============================================
void sendHeartbeat() {
  unsigned long now = millis();

  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
    return;
  }
  lastHeartbeatMs = now;

  char buffer[64];
  char data[16];

  snprintf(data, sizeof(data), "%lu", now / 1000); // Uptime in seconds
  buildMessage(buffer, CMD_HEARTBEAT, data);

  Serial2.print(buffer);

  // Check if we got ACK recently
  if (now - lastAckMs > HEARTBEAT_INTERVAL_MS * 3) {
    mainEspConnected = false;
  }
}

// ============================================
// PROCESS INCOMING MESSAGES
// ============================================
void processUartReceive() {
  while (Serial2.available()) {
    char buffer[64];
    int len = Serial2.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';

    char cmd[16], data[32];
    if (parseMessage(buffer, cmd, data)) {
      if (strcmp(cmd, CMD_ACK) == 0) {
        lastAckMs = millis();
        mainEspConnected = true;
      } else if (strcmp(cmd, CMD_STATUS) == 0) {
        // Main ESP sent status update
        lastAckMs = millis();
        mainEspConnected = true;
        Serial.print("📥 Status: ");
        Serial.println(data);
      }
    }
  }

  // Try to flush offline buffer if connected
  if (mainEspConnected && offlineBufferCount > 0) {
    Serial.print("📤 Flushing offline buffer (");
    Serial.print(offlineBufferCount);
    Serial.println(" payments)");

    int sentCount = 0;
    for (int i = 0; i < offlineBufferCount; i++) {
      if (!trySendPaymentTx(offlineBuffer[i])) {
        break;
      }
      sentCount++;
    }

    if (sentCount > 0) {
      const int remaining = offlineBufferCount - sentCount;
      if (remaining > 0) {
        memmove(offlineBuffer, &offlineBuffer[sentCount],
                remaining * sizeof(PaymentTx));
      }
      offlineBufferCount = remaining;
    }
  }
}

// ============================================
// STATUS
// ============================================
bool isMainEspConnected() { return mainEspConnected; }
