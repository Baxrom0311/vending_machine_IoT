#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================
// UART CONFIGURATION
// ============================================
#define UART_BAUD 9600
// UART pins are defined per-controller in:
// - `src_esp32_main/hardware.h`
// - `src_esp32_payment/hardware.h`

// ============================================
// MESSAGE FORMAT
// ============================================
// Format: $CMD,DATA*CHECKSUM\n
// Checksum: XOR of all bytes between $ and *
//
// Examples:
//   $PAY,5000,123*A3\n  - Payment of 5000 with seq=123
//   $HB,1*48\n          - Heartbeat
//   $ACK,123*41\n       - ACK for seq=123 (or 0 for heartbeat)

// ============================================
// COMMANDS: Payment ESP → Main ESP
// ============================================
#define CMD_PAYMENT "PAY"  // $PAY,amount,seq*CS - Cash payment received
#define CMD_HEARTBEAT "HB" // $HB,uptime*CS  - Heartbeat every 5s

// ============================================
// COMMANDS: Main ESP → Payment ESP
// ============================================
#define CMD_ACK                                                                \
  "ACK" // $ACK,seq*CS    - Command acknowledged (seq=0 for heartbeat)
#define CMD_STATUS "STS" // $STS,state*CS  - Current system state

// ============================================
// PROTOCOL LIMITS
// ============================================
#define UART_MSG_BUFFER_SIZE 64
#define UART_MAX_CMD_LEN 10
#define UART_MAX_DATA_LEN 32

// ============================================
// UTILITY FUNCTIONS
// ============================================

// Calculate XOR checksum
inline uint8_t calculateChecksum(const char *data, int len) {
  uint8_t cs = 0;
  for (int i = 0; i < len; i++) {
    cs ^= data[i];
  }
  return cs;
}

// Build message with checksum
// Buffer must be at least UART_MSG_BUFFER_SIZE bytes
inline int buildMessage(char *buffer, const char *cmd, const char *data) {
  // Format: $CMD,DATA*XX\n
  int len = snprintf(buffer, UART_MSG_BUFFER_SIZE - 3, "$%s,%s*", cmd, data);
  if (len < 0 || len >= UART_MSG_BUFFER_SIZE - 3) {
    buffer[0] = '\0';
    return 0; // Message too long
  }

  // Calculate checksum (between $ and *)
  uint8_t cs = calculateChecksum(buffer + 1, len - 2);

  // Append checksum as hex
  snprintf(buffer + len, 4, "%02X\n", cs);

  return len + 3; // +2 for checksum hex + 1 for newline
}

// Parse incoming message
// Returns true if valid, fills cmd and data buffers
inline bool parseMessage(const char *msg, char *cmd, char *data) {
  // Check format: $CMD,DATA*XX
  if (msg[0] != '$')
    return false;

  // Find comma
  const char *comma = strchr(msg, ',');
  if (!comma)
    return false;

  // Find asterisk
  const char *asterisk = strchr(comma, '*');
  if (!asterisk)
    return false;

  // Extract command
  int cmdLen = comma - msg - 1;
  if (cmdLen <= 0 || cmdLen > UART_MAX_CMD_LEN)
    return false;
  strncpy(cmd, msg + 1, cmdLen);
  cmd[cmdLen] = '\0';

  // Extract data
  int dataLen = asterisk - comma - 1;
  if (dataLen < 0 || dataLen > UART_MAX_DATA_LEN)
    return false;
  strncpy(data, comma + 1, dataLen);
  data[dataLen] = '\0';

  // Verify checksum
  uint8_t expectedCs = calculateChecksum(msg + 1, asterisk - msg - 1);
  uint8_t receivedCs = (uint8_t)strtol(asterisk + 1, NULL, 16);

  return (expectedCs == receivedCs);
}

#endif
