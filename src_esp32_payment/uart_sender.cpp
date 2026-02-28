#include "uart_sender.h"
#include "../shared/uart_protocol.h"
#include "cash_handler.h"
#include "hardware.h"
#include <Preferences.h>
#include <cstring>

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
#define ACK_TIMEOUT_MS 300
#define MAX_RETRIES 2
#define OFFLINE_BUFFER_SIZE 10
static constexpr uint16_t UART_RX_BYTE_BUDGET = 128;
static constexpr unsigned long UART_RX_TIME_BUDGET_MS = 3UL;
static constexpr unsigned long UART_FRAME_TIMEOUT_MS = 150UL;
static constexpr const char *TX_PREFS_NAMESPACE = "ewpay";
static constexpr unsigned long TX_STATE_FLUSH_MS = 5000UL;
static constexpr uint8_t TX_STATE_MAX_MUTATIONS = 8;
static constexpr unsigned long TX_RETRY_GAP_MS = 150UL;
static constexpr unsigned long TX_OFFLINE_COOLDOWN_MS = 2000UL;

// ============================================
// VARIABLES
// ============================================
static unsigned long lastHeartbeatMs = 0;
static unsigned long lastAckMs = 0;
static bool mainEspConnected = false;
static Preferences txPreferences;

struct PaymentTx {
  int amount;
  uint32_t seq;
};

// Offline buffer - store payments when Main ESP is offline
static PaymentTx offlineBuffer[OFFLINE_BUFFER_SIZE];
static int offlineBufferCount = 0;
static uint32_t nextPaymentSeq = 0; // Loaded/randomized in init

// Deferred persistence state (reduces flash wear)
static bool txStateDirty = false;
static unsigned long txStateDirtySinceMs = 0;
static uint8_t txStateMutations = 0;

// Non-blocking TX state
static bool txActive = false;
static bool txAwaitingAck = false;
static PaymentTx txCurrent{0, 0};
static uint8_t txRetryCount = 0;
static unsigned long txAckDeadlineMs = 0;
static unsigned long txNextAttemptMs = 0;

// Incremental UART RX parser
static char rxFrame[UART_MSG_BUFFER_SIZE] = {0};
static uint8_t rxFrameLen = 0;
static bool rxInFrame = false;
static unsigned long rxFrameStartMs = 0;

static void markTxStateDirty() {
  if (!txStateDirty) {
    txStateDirty = true;
    txStateDirtySinceMs = millis();
    txStateMutations = 0;
  }
  if (txStateMutations < 255) {
    txStateMutations++;
  }
}

static bool persistTxStateNow() {
  if (!txPreferences.begin(TX_PREFS_NAMESPACE, false)) {
    PAY_LOG_PRINTLN("⚠️ TX state save failed (NVS open)");
    return false;
  }

  bool ok = true;
  if (txPreferences.putULong("next_seq", nextPaymentSeq) != sizeof(uint32_t)) {
    ok = false;
  }
  if (txPreferences.putUChar("buf_count", static_cast<uint8_t>(offlineBufferCount)) !=
      sizeof(uint8_t)) {
    ok = false;
  }
  const size_t saved =
      txPreferences.putBytes("buf_data", offlineBuffer, sizeof(offlineBuffer));
  if (saved != sizeof(offlineBuffer)) {
    ok = false;
  }
  txPreferences.end();

  if (!ok) {
    PAY_LOG_PRINTLN("⚠️ TX state save incomplete");
    return false;
  }

  txStateDirty = false;
  txStateDirtySinceMs = 0;
  txStateMutations = 0;
  return true;
}

static void flushTxStateIfNeeded(bool forceFlush) {
  if (!txStateDirty) {
    return;
  }

  const unsigned long now = millis();
  if (!forceFlush && txStateMutations < TX_STATE_MAX_MUTATIONS &&
      (now - txStateDirtySinceMs) < TX_STATE_FLUSH_MS) {
    return;
  }

  persistTxStateNow();
}

static void loadTxState() {
  memset(offlineBuffer, 0, sizeof(offlineBuffer));
  offlineBufferCount = 0;
  nextPaymentSeq = 0;

  if (!txPreferences.begin(TX_PREFS_NAMESPACE, true)) {
    PAY_LOG_PRINTLN("⚠️ TX state load skipped (NVS open failed)");
    return;
  }

  nextPaymentSeq = txPreferences.getULong("next_seq", 0);
  offlineBufferCount = txPreferences.getUChar("buf_count", 0);
  if (offlineBufferCount > OFFLINE_BUFFER_SIZE) {
    offlineBufferCount = 0;
  }

  const size_t loaded =
      txPreferences.getBytes("buf_data", offlineBuffer, sizeof(offlineBuffer));
  if (loaded != sizeof(offlineBuffer)) {
    memset(offlineBuffer, 0, sizeof(offlineBuffer));
    offlineBufferCount = 0;
  }

  txPreferences.end();
}

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

static bool enqueuePaymentTx(const PaymentTx &tx) {
  if (offlineBufferCount >= OFFLINE_BUFFER_SIZE) {
    return false;
  }
  offlineBuffer[offlineBufferCount++] = tx;
  markTxStateDirty();
  return true;
}

static void popFrontPaymentTx() {
  if (offlineBufferCount <= 0) {
    return;
  }

  const int remaining = offlineBufferCount - 1;
  if (remaining > 0) {
    memmove(offlineBuffer, &offlineBuffer[1], remaining * sizeof(PaymentTx));
  }
  offlineBufferCount = remaining;
  markTxStateDirty();
}

static void beginActiveTxIfNeeded(unsigned long now) {
  if (txActive || offlineBufferCount <= 0) {
    return;
  }
  txCurrent = offlineBuffer[0];
  txRetryCount = 0;
  txAwaitingAck = false;
  txAckDeadlineMs = 0;
  txNextAttemptMs = now;
  txActive = true;
}

static void sendActiveAttempt(unsigned long now) {
  char buffer[64];
  char data[32];
  snprintf(data, sizeof(data), "%d,%lu", txCurrent.amount,
           static_cast<unsigned long>(txCurrent.seq));
  buildMessage(buffer, CMD_PAYMENT, data);

  PAY_LOG_PRINT("📤 Sending: ");
  PAY_LOG_PRINT(buffer);

  Serial2.print(buffer);
  txAwaitingAck = true;
  txAckDeadlineMs = now + ACK_TIMEOUT_MS;
}

static void processTxStateMachine() {
  const unsigned long now = millis();

  // If disconnected, wait for heartbeat ACK before trying queue again.
  if (!mainEspConnected && !txAwaitingAck) {
    txActive = false;
    if (now >= txNextAttemptMs) {
      txNextAttemptMs = now + TX_OFFLINE_COOLDOWN_MS;
    }
    return;
  }

  beginActiveTxIfNeeded(now);

  if (!txActive) {
    return;
  }

  if (txAwaitingAck) {
    if ((long)(now - txAckDeadlineMs) >= 0) {
      txAwaitingAck = false;
      if (txRetryCount < MAX_RETRIES) {
        txRetryCount++;
        txNextAttemptMs = now + TX_RETRY_GAP_MS;
        PAY_LOG_PRINT("⚠️ No ACK, retry ");
        PAY_LOG_PRINTLN(txRetryCount);
      } else {
        PAY_LOG_PRINTLN("❌ Payment TX retries exhausted, waiting reconnect");
        txActive = false;
        mainEspConnected = false;
        txNextAttemptMs = now + TX_OFFLINE_COOLDOWN_MS;
      }
    }
    return;
  }

  if ((long)(now - txNextAttemptMs) >= 0) {
    sendActiveAttempt(now);
  }
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

  loadTxState();
  if (nextPaymentSeq == 0) {
    // Randomize starting seq on first boot.
    nextPaymentSeq = (micros() & 0xFFFF) + 100; // Range: 100 - 65635
    markTxStateDirty();
    flushTxStateIfNeeded(true);
  }

  txActive = false;
  txAwaitingAck = false;
  txRetryCount = 0;
  txAckDeadlineMs = 0;
  txNextAttemptMs = 0;

  rxFrameLen = 0;
  rxInFrame = false;
  rxFrameStartMs = 0;

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

  uint32_t seq = nextPaymentSeq;
  if (seq == 0) {
    seq = 1;
  }

  PaymentTx tx{amount, seq};

  if (!enqueuePaymentTx(tx)) {
    return false;
  }

  nextPaymentSeq = seq + 1;
  if (nextPaymentSeq == 0) {
    nextPaymentSeq = 1;
  }
  markTxStateDirty();

  // If connected and idle, try to start immediately (non-blocking).
  if (mainEspConnected && !txActive) {
    txNextAttemptMs = 0;
  }

  flushTxStateIfNeeded(false);
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
// PROCESS INCOMING MESSAGES + TX STATE MACHINE
// ============================================
void processUartReceive() {
  const unsigned long now = millis();

  if (rxInFrame && (now - rxFrameStartMs) > UART_FRAME_TIMEOUT_MS) {
    rxInFrame = false;
    rxFrameLen = 0;
    rxFrameStartMs = 0;
  }

  const unsigned long readStartMs = millis();
  uint16_t processedBytes = 0;

  while (Serial2.available()) {
    if (processedBytes >= UART_RX_BYTE_BUDGET ||
        (millis() - readStartMs) >= UART_RX_TIME_BUDGET_MS) {
      break;
    }

    const char ch = static_cast<char>(Serial2.read());
    processedBytes++;

    if (!rxInFrame) {
      if (ch == '$') {
        rxInFrame = true;
        rxFrameLen = 0;
        rxFrameStartMs = millis();
        rxFrame[rxFrameLen++] = ch;
      }
      continue;
    }

    if (ch == '\r') {
      continue;
    }

    if (rxFrameLen >= (UART_MSG_BUFFER_SIZE - 1)) {
      rxInFrame = false;
      rxFrameLen = 0;
      rxFrameStartMs = 0;
      continue;
    }

    rxFrame[rxFrameLen++] = ch;

    if (ch != '\n') {
      continue;
    }

    rxFrame[rxFrameLen - 1] = '\0';
    rxInFrame = false;
    rxFrameStartMs = 0;

    bool ackMatched = false;
    const uint32_t expectedSeq = txActive ? txCurrent.seq : 0;
    if (processIncomingMessage(rxFrame, expectedSeq, &ackMatched) && ackMatched &&
        txActive) {
      PAY_LOG_PRINTLN("✓ ACK received");
      popFrontPaymentTx();
      txActive = false;
      txAwaitingAck = false;
      txRetryCount = 0;
      txAckDeadlineMs = 0;
      txNextAttemptMs = millis() + TX_RETRY_GAP_MS;
    }

    rxFrameLen = 0;
  }

  if (now - lastAckMs > HEARTBEAT_INTERVAL_MS * 3) {
    mainEspConnected = false;
  }

  processTxStateMachine();
  flushTxStateIfNeeded(false);
}

// ============================================
// STATUS
// ============================================
bool isMainEspConnected() { return mainEspConnected; }
