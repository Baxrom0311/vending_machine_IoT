#include "uart_receiver.h"
#include "../shared/uart_protocol.h"
#include "config.h"
#include "debug.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"

// ============================================
// CONFIGURATION
// ============================================
#define CONNECTION_TIMEOUT_MS 15000
static constexpr unsigned long PAYMENT_MIN_INTERVAL_MS = 350UL;
static constexpr int PAYMENT_MIN_AMOUNT = 100;
static constexpr int PAYMENT_MAX_AMOUNT = 1000000;
static constexpr uint16_t UART_RX_BYTE_BUDGET = 128;
static constexpr unsigned long UART_RX_TIME_BUDGET_MS = 3UL;

// ============================================
// VARIABLES
// ============================================
static unsigned long lastMessageMs = 0;
static bool paymentEspConnected = false;
static uint32_t recentPaymentSeq[64] = {0};
static uint8_t recentPaymentSeqIdx = 0;
static unsigned long lastAcceptedPaymentMs = 0;
static unsigned long lastCashCfgSendMs = 0;
static int lastCashPulseValue = -1;
static unsigned long lastCashGapMs = 0;
static char rxFrame[UART_MSG_BUFFER_SIZE] = {0};
static uint8_t rxFrameLen = 0;
static bool rxInFrame = false;

static bool isDuplicatePaymentSeq(uint32_t seq) {
  if (seq == 0) {
    return false;
  }
  for (uint8_t i = 0;
       i < (sizeof(recentPaymentSeq) / sizeof(recentPaymentSeq[0])); i++) {
    if (recentPaymentSeq[i] == seq) {
      return true;
    }
  }
  recentPaymentSeq[recentPaymentSeqIdx++ % (sizeof(recentPaymentSeq) /
                                            sizeof(recentPaymentSeq[0]))] = seq;
  return false;
}

static bool parsePaymentDataStrict(const char *data, int *amount,
                                   uint32_t *seq) {
  if (!data || !amount || !seq) {
    return false;
  }

  const char *comma = strchr(data, ',');
  if (comma == nullptr) {
    return false;
  }

  char *endAmount = nullptr;
  const long parsedAmount = strtol(data, &endAmount, 10);
  if (endAmount != comma) {
    return false;
  }

  char *endSeq = nullptr;
  const unsigned long parsedSeq = strtoul(comma + 1, &endSeq, 10);
  if (*endSeq != '\0') {
    return false;
  }

  if (parsedAmount < PAYMENT_MIN_AMOUNT || parsedAmount > PAYMENT_MAX_AMOUNT) {
    return false;
  }

  if (parsedSeq == 0) {
    return false;
  }

  *amount = static_cast<int>(parsedAmount);
  *seq = static_cast<uint32_t>(parsedSeq);
  return true;
}

// ============================================
// INITIALIZATION
// ============================================
void initUartReceiver() {
  pinMode(UART_RX_PIN, INPUT_PULLUP); // Helps when wire is disconnected/floating
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial2.setTimeout(50);

  // Flush stale data from UART buffer (from before boot/reflash)
  delay(100); // Wait for any pending bytes
  while (Serial2.available()) {
    Serial2.read();
  }

  // Clear duplicate tracking array
  memset(recentPaymentSeq, 0, sizeof(recentPaymentSeq));
  recentPaymentSeqIdx = 0;
  lastAcceptedPaymentMs = 0;
  rxFrameLen = 0;
  rxInFrame = false;

  DEBUG_PRINT("✓ UART Receiver initialized (RX:");
  DEBUG_PRINT(UART_RX_PIN);
  DEBUG_PRINT(", TX:");
  DEBUG_PRINT(UART_TX_PIN);
  DEBUG_PRINTLN(")");
}

// ============================================
// SEND ACK
// ============================================
void sendAck(uint32_t seq) {
  char buffer[64];
  char data[16];
  snprintf(data, sizeof(data), "%lu", static_cast<unsigned long>(seq));
  buildMessage(buffer, CMD_ACK, data);
  Serial2.print(buffer);
}

// ============================================
// SEND STATUS
// ============================================
void sendStatusToPaymentEsp(const char *state, long bal) {
  char buffer[64];
  char data[32];

  snprintf(data, sizeof(data), "%s,%ld", state, bal);
  buildMessage(buffer, CMD_STATUS, data);
  Serial2.print(buffer);
}

void sendCashConfigToPaymentEsp(int pulseValue, unsigned long gapMs) {
  char buffer[64];
  char data[32];

  snprintf(data, sizeof(data), "%d,%lu", pulseValue,
           static_cast<unsigned long>(gapMs));
  buildMessage(buffer, CMD_CASHCFG, data);
  Serial2.print(buffer);
}

static void maybeSendCashConfig() {
  if (!paymentEspConnected) {
    return;
  }

  const unsigned long now = millis();
  const int pulseValue = config.cashPulseValue;
  const unsigned long gapMs = config.cashPulseGapMs;

  const bool changed =
      (pulseValue != lastCashPulseValue) || (gapMs != lastCashGapMs);

  if (!changed && (now - lastCashCfgSendMs) < 30000) {
    return;
  }

  sendCashConfigToPaymentEsp(pulseValue, gapMs);
  lastCashCfgSendMs = now;
  lastCashPulseValue = pulseValue;
  lastCashGapMs = gapMs;
}

// ============================================
// PROCESS INCOMING MESSAGES
// ============================================
void processUartReceiver() {
  const unsigned long readStartMs = millis();
  uint16_t processedBytes = 0;

  while (Serial2.available()) {
    if (processedBytes >= UART_RX_BYTE_BUDGET ||
        (millis() - readStartMs) >= UART_RX_TIME_BUDGET_MS) {
      break;
    }

    const char ch = static_cast<char>(Serial2.read());
    processedBytes++;

    // Frame sync: accept only "$...\\n" packets, drop all other noise bytes.
    if (!rxInFrame) {
      if (ch == '$') {
        rxInFrame = true;
        rxFrameLen = 0;
        rxFrame[rxFrameLen++] = ch;
      }
      continue;
    }

    if (ch == '\r') {
      continue;
    }

    if (rxFrameLen >= (UART_MSG_BUFFER_SIZE - 1)) {
      // Overflow/no newline: drop corrupted frame and wait for next '$'
      rxInFrame = false;
      rxFrameLen = 0;
      continue;
    }

    rxFrame[rxFrameLen++] = ch;

    if (ch != '\n') {
      continue;
    }

    rxFrame[rxFrameLen - 1] = '\0'; // strip newline
    rxInFrame = false;

    char cmd[16], data[32];
    if (parseMessage(rxFrame, cmd, data)) {
      lastMessageMs = millis();
      paymentEspConnected = true;

      if (strcmp(cmd, CMD_PAYMENT) == 0) {
        int amount = 0;
        uint32_t seq = 0;
        if (!parsePaymentDataStrict(data, &amount, &seq)) {
          DEBUG_PRINT("⚠️ Invalid PAY frame rejected: ");
          DEBUG_PRINTLN(data);
          continue;
        }

        DEBUG_PRINTLN("============================");
        DEBUG_PRINT("💵 UART Payment: ");
        DEBUG_PRINT(amount);
        DEBUG_PRINT(" so'm (seq=");
        DEBUG_PRINT(static_cast<unsigned long>(seq));
        DEBUG_PRINTLN(")");
        DEBUG_PRINT("   Balance BEFORE: ");
        DEBUG_PRINTLN(balance);

        // Send ACK immediately
        sendAck(seq);

        // Check for duplicate payment sequence
        if (isDuplicatePaymentSeq(seq)) {
          DEBUG_PRINT("⚠️ Duplicate REJECTED, seq=");
          DEBUG_PRINTLN(seq);
          continue;
        }

        const unsigned long now = millis();
        if ((now - lastAcceptedPaymentMs) < PAYMENT_MIN_INTERVAL_MS) {
          DEBUG_PRINT("⚠️ Payment rate-limited, seq=");
          DEBUG_PRINTLN(static_cast<unsigned long>(seq));
          continue;
        }

        DEBUG_PRINTLN("✅ Processing payment...");
        processPayment(amount, "cash_uart", nullptr, nullptr);
        lastAcceptedPaymentMs = now;

        DEBUG_PRINT("   Balance AFTER: ");
        DEBUG_PRINTLN(balance);
        DEBUG_PRINT("   State: ");
        DEBUG_PRINTLN(currentState);
        DEBUG_PRINTLN("============================");

      } else if (strcmp(cmd, CMD_HEARTBEAT) == 0) {
        sendAck(0);
      }
    }

    rxFrameLen = 0;
  }

  // Check connection timeout
  if (millis() - lastMessageMs > CONNECTION_TIMEOUT_MS) {
    paymentEspConnected = false;
  }

  maybeSendCashConfig();
}

// ============================================
// STATUS
// ============================================
bool isPaymentEspConnected() { return paymentEspConnected; }
