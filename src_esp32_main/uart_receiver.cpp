#include "uart_receiver.h"
#include "../shared/uart_protocol.h"
#include "config.h"
#include "hardware.h"
#include "mqtt_handler.h"
#include "state_machine.h"

// ============================================
// CONFIGURATION
// ============================================
#define CONNECTION_TIMEOUT_MS 15000

// ============================================
// VARIABLES
// ============================================
static unsigned long lastMessageMs = 0;
static bool paymentEspConnected = false;
static uint32_t recentPaymentSeq[16] = {0};
static uint8_t recentPaymentSeqIdx = 0;
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
  rxFrameLen = 0;
  rxInFrame = false;

  Serial.print("✓ UART Receiver initialized (RX:");
  Serial.print(UART_RX_PIN);
  Serial.print(", TX:");
  Serial.print(UART_TX_PIN);
  Serial.println(")");
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
  while (Serial2.available()) {
    const char ch = static_cast<char>(Serial2.read());

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
        // Payment received from Payment ESP32
        int amount = 0;
        uint32_t seq = 0;
        const char *comma = strchr(data, ',');
        if (comma != nullptr) {
          amount = atoi(data);
          seq = static_cast<uint32_t>(strtoul(comma + 1, nullptr, 10));
        } else {
          // Backward compatible: $PAY,amount
          amount = atoi(data);
          seq = 0;
        }

        Serial.println("============================");
        Serial.print("💵 UART Payment: ");
        Serial.print(amount);
        Serial.print(" so'm (seq=");
        Serial.print(static_cast<unsigned long>(seq));
        Serial.println(")");
        Serial.print("   Balance BEFORE: ");
        Serial.println(balance);

        // Send ACK immediately
        sendAck(seq);

        // Check for duplicate payment sequence
        if (isDuplicatePaymentSeq(seq)) {
          Serial.print("⚠️ Duplicate REJECTED, seq=");
          Serial.println(seq);
          continue;
        }

        Serial.println("✅ Processing payment...");
        processPayment(amount, "cash_uart", nullptr, nullptr);

        Serial.print("   Balance AFTER: ");
        Serial.println(balance);
        Serial.print("   State: ");
        Serial.println(currentState);
        Serial.println("============================");

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
