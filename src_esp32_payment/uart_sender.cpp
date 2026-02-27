#include "uart_sender.h"
#include "../shared/uart_protocol.h"
#include "cash_handler.h"
#include "hardware.h"

#ifndef ENABLE_DEBUG_LOGS
#define ENABLE_DEBUG_LOGS 1
#endif

#if ENABLE_DEBUG_LOGS
#define PAY_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define PAY_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define PAY_LOG_PRINT(...)
#define PAY_LOG_PRINTLN(...)
#endif

// ============================================
// CONFIGURATION
// ============================================
#define HEARTBEAT_INTERVAL_MS 10000
#define ACK_TIMEOUT_MS 500 // Reduced from 500ms to minimize blocking
#define MAX_RETRIES 3
#define OFFLINE_BUFFER_SIZE 10
static constexpr uint16_t UART_RX_BYTE_BUDGET = 128;
static constexpr unsigned long UART_RX_TIME_BUDGET_MS = 3UL;

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

static void applyCashConfig(const char *data) {
  int value = 0;
  unsigned long gap = 0;
  const char *comma = strchr(data, ',');
  if (comma != nullptr) {
    value = atoi(data);
    gap = strtoul(comma + 1, nullptr, 10);
  } else {
    value = atoi(data);
  }

  if (value > 0) {
    setCashPulseValue(value);
  }
  if (gap > 0) {
    setCashPulseGapMs(gap);
  }
}

static bool processIncomingMessage(const char *message, uint32_t expectedAckSeq,
                                   bool *ackMatched) {
  char cmd[16], data[32];
  if (!parseMessage(message, cmd, data)) {
    return false;
  }

  if (strcmp(cmd, CMD_ACK) == 0) {
    const unsigned long ackSeq = strtoul(data, nullptr, 10);
    lastAckMs = millis();
    mainEspConnected = true;
    if (ackMatched != nullptr && ackSeq == expectedAckSeq) {
      *ackMatched = true;
    }
    return true;
  }

  if (strcmp(cmd, CMD_STATUS) == 0) {
    lastAckMs = millis();
    mainEspConnected = true;
    PAY_LOG_PRINT("📥 Status: ");
    PAY_LOG_PRINTLN(data);
    return true;
  }

  if (strcmp(cmd, CMD_CASHCFG) == 0) {
    applyCashConfig(data);
    return true;
  }

  return true;
}

static bool trySendPaymentTx(const PaymentTx &tx) {
  char buffer[64];
  char data[32];

  snprintf(data, sizeof(data), "%d,%lu", tx.amount,
           static_cast<unsigned long>(tx.seq));
  buildMessage(buffer, CMD_PAYMENT, data);

  PAY_LOG_PRINT("📤 Sending: ");
  PAY_LOG_PRINT(buffer);

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
      if (len <= 0) {
        continue;
      }

      bool ackMatched = false;
      if (!processIncomingMessage(response, tx.seq, &ackMatched)) {
        continue;
      }
      if (ackMatched) {
        PAY_LOG_PRINTLN("✓ ACK received");
        return true;
      }
    }

    PAY_LOG_PRINT("⚠️ No ACK, retry ");
    PAY_LOG_PRINTLN(retry + 1);
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
  Serial2.setTimeout(50);

  // Flush any stale data in UART buffer
  while (Serial2.available()) {
    Serial2.read();
  }

  // Randomize starting seq to prevent collisions after restart
  // Main ESP tracks recent seq numbers - if we always start at 1,
  // payments get rejected as "duplicates" after restart
  nextPaymentSeq = (micros() & 0xFFFF) + 100; // Range: 100 - 65635

  PAY_LOG_PRINT("✓ UART initialized (TX:");
  PAY_LOG_PRINT(UART_TX_PIN);
  PAY_LOG_PRINT(", RX:");
  PAY_LOG_PRINT(UART_RX_PIN);
  PAY_LOG_PRINT(") seq_start=");
  PAY_LOG_PRINTLN(nextPaymentSeq);
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

  PAY_LOG_PRINTLN("❌ Main ESP offline, buffering payment");
  if (!enqueuePaymentTx(tx)) {
    PAY_LOG_PRINTLN("⚠️ Offline buffer full!");
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
  const unsigned long readStartMs = millis();
  uint16_t processedBytes = 0;

  while (Serial2.available()) {
    if (processedBytes >= UART_RX_BYTE_BUDGET ||
        (millis() - readStartMs) >= UART_RX_TIME_BUDGET_MS) {
      break;
    }

    char buffer[64];
    int len = Serial2.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    if (len > 0) {
      processedBytes += static_cast<uint16_t>(len);
    }
    buffer[len] = '\0';
    if (len <= 0) {
      continue;
    }

    processIncomingMessage(buffer, 0, nullptr);
  }

  // Try to flush offline buffer if connected
  if (mainEspConnected && offlineBufferCount > 0) {
    PAY_LOG_PRINT("📤 Flushing offline buffer (");
    PAY_LOG_PRINT(offlineBufferCount);
    PAY_LOG_PRINTLN(" payments)");

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
