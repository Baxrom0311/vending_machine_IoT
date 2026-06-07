#include "local_events.h"
#include "config_storage.h"
#include "debug.h"
#include "sensors.h"
#include "state_machine.h"
#include <climits>
#include <cstdio>

static const char *stateName(SystemState state) {
  switch (state) {
  case IDLE:
    return "IDLE";
  case ACTIVE:
    return "ACTIVE";
  case DISPENSING:
    return "DISPENSING";
  case PAUSED:
    return "PAUSED";
  default:
    return "UNKNOWN";
  }
}

void processPayment(int amount, const char *source, const char *txnId,
                    const char *userId) {
  if (amount <= 0) {
    publishLog("ERROR", "Invalid payment: negative or zero amount");
    return;
  }
  if (amount > 1000000) {
    publishLog("ERROR", "Invalid payment: amount exceeds limit");
    return;
  }

  const char *safeSource = (source && source[0]) ? source : "local";

  DEBUG_PRINT("Payment received: ");
  DEBUG_PRINT(amount);
  DEBUG_PRINT(" from ");
  DEBUG_PRINTLN(safeSource);

  if (txnId && txnId[0]) {
    DEBUG_PRINT("Transaction ID: ");
    DEBUG_PRINTLN(txnId);
  }

  if (amount > 0 && balance > LONG_MAX - amount) {
    balance = LONG_MAX;
    publishLog("ERROR", "Balance overflow capped");
  } else {
    balance += amount;
  }

  if (balance > 0) {
    if (currentState == IDLE) {
      currentState = ACTIVE;
      totalDispensedLiters = 0.0f;
      sessionStartBalance = balance;
    } else if (currentState == DISPENSING) {
      DEBUG_PRINTLN("Additional payment during DISPENSING");
    } else if (currentState == PAUSED) {
      DEBUG_PRINTLN("Payment during PAUSED - balance increased");
    }
  }

  resetSessionTimer();

  char paymentLog[192];
  int offset =
      snprintf(paymentLog, sizeof(paymentLog), "%d|%s", amount, safeSource);

  if (txnId && txnId[0] && offset > 0 &&
      offset < static_cast<int>(sizeof(paymentLog))) {
    int written = snprintf(paymentLog + offset, sizeof(paymentLog) - offset,
                           "|%s", txnId);
    if (written > 0) {
      offset += written;
    }
  }
  if (userId && userId[0] && offset > 0 &&
      offset < static_cast<int>(sizeof(paymentLog))) {
    snprintf(paymentLog + offset, sizeof(paymentLog) - offset, "|%s", userId);
  }

  publishLog("PAYMENT", paymentLog);
  publishStatus();
}

void publishStatus() {
  DEBUG_PRINT("STATUS device=");
  DEBUG_PRINT(deviceConfig.device_id);
  DEBUG_PRINT(" state=");
  DEBUG_PRINT(stateName(currentState));
  DEBUG_PRINT(" balance=");
  DEBUG_PRINT(balance);
  DEBUG_PRINT(" dispensed=");
  DEBUG_PRINT(totalDispensedLiters, 2);
  DEBUG_PRINT(" tds=");
  DEBUG_PRINTLN(tdsPPM);
}

void publishLog(const char *event, const char *message) {
  DEBUG_PRINT("LOG ");
  DEBUG_PRINT(event ? event : "EVENT");
  DEBUG_PRINT(": ");
  DEBUG_PRINTLN(message ? message : "");
}
