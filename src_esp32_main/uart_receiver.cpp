#include "uart_receiver.h"
#include "../shared/uart_protocol.h"
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
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // Flush stale data from UART buffer (from before boot/reflash)
  delay(100); // Wait for any pending bytes
  while (Serial2.available()) {
    Serial2.read();
  }

  // Clear duplicate tracking array
  memset(recentPaymentSeq, 0, sizeof(recentPaymentSeq));
  recentPaymentSeqIdx = 0;

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

// ============================================
// PROCESS INCOMING MESSAGES
// ============================================
void processUartReceiver() {
  while (Serial2.available()) {
    char buffer[64];
    int len = Serial2.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';

    // DEBUG: Print raw received data
    if (len > 0) {
      Serial.print("📩 Rx RAW [");
      Serial.print(len);
      Serial.print("]: ");
      Serial.println(buffer);
    }

    char cmd[16], data[32];
    if (parseMessage(buffer, cmd, data)) {
      lastMessageMs = millis();
      paymentEspConnected = true;

      Serial.print("📋 Parsed CMD=");
      Serial.print(cmd);
      Serial.print(" DATA=");
      Serial.println(data);

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
    } else if (len > 0) {
      Serial.print("❌ Parse FAILED for: ");
      Serial.println(buffer);
    }
  }

  // Check connection timeout
  if (millis() - lastMessageMs > CONNECTION_TIMEOUT_MS) {
    paymentEspConnected = false;
  }
}

// ============================================
// STATUS
// ============================================
bool isPaymentEspConnected() { return paymentEspConnected; }
