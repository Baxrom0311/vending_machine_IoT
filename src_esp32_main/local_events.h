#ifndef LOCAL_EVENTS_H
#define LOCAL_EVENTS_H

#include <Arduino.h>

void processPayment(int amount, const char *source = nullptr,
                    const char *txnId = nullptr,
                    const char *userId = nullptr);
void publishStatus();
void publishLog(const char *event, const char *message);

#endif
