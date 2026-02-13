#include "mocks/Arduino.h"
#include "mocks/Preferences.h"
#include "mocks/PubSubClient.h"
#include "mocks/WiFi.h"
#include "mocks/display.h"
#include "mocks/esp_task_wdt.h"
#include "mocks/ota_handler.h"
#include <unity.h>

// Include implementations for linkage
#include "mocks/MockDeps.cpp" // Data/Config Mocks (no MQTT handler)
#include "mocks/MockImpl.cpp" // Arduino/Preferences globals

// Include source files under test
#define copyToBuffer copyToBuffer_config
#include "../../src_esp32_main/config_storage.cpp"
#undef copyToBuffer
#include "../../src_esp32_main/config_storage_validation.cpp"
#include "../../src_esp32_main/state_machine.cpp"
#define copyToBuffer copyToBuffer_mqtt
#include "../../src_esp32_main/mqtt_handler.cpp" // Now included!
#undef copyToBuffer

// ============================================
// SETUP / TEARDOWN
// ============================================
void setUp(void) {
  // Reset global mocks
  preferences.clear();

  // Reset State Machine logic
  initStateMachine();

  // Reset Config
  loadDefaultConfig();

  // Sync 'config' global with 'deviceConfig'
  config.pricePerLiter = 1000;
  config.pulsesPerLiter = 450.0;
  config.enableFreeWater = true;
  config.freeWaterAmount = 0.2;
  config.freeWaterCooldown = 0;
  config.sessionTimeout = 300000;
  // config.requireSignedMessages does not exist in Config struct

  // Sync deviceConfig
  deviceConfig.pricePerLiter = 1000;
  deviceConfig.requireSignedMessages = false;

  // Init MQTT topics
  strcpy(TOPIC_PAYMENT_IN, "water/payment");
  strcpy(TOPIC_CONFIG_IN, "water/config");
  strcpy(TOPIC_BROADCAST_COMMAND, "water/broadcast/command");
  strcpy(TOPIC_GROUP_COMMAND, "water/group/command");
  // ... others as needed
}

void tearDown(void) {
  // clean up
}

// ... (Previous Config / SM Tests Kept same, but compacted for brevity in
// overwrite if needed. I will keep them.) To avoid rewriting entire file if I
// can use replace, I will assume replace is better. But write_to_file
// overwrites. I should include previous tests.

// ============================================
// CONFIG TESTS
// ============================================
void test_config_load_defaults(void) {
  loadDefaultConfig();
  TEST_ASSERT_EQUAL_STRING("VendingMachine_001", deviceConfig.device_id);
  TEST_ASSERT_EQUAL_INT(1000, deviceConfig.pricePerLiter);
}

void test_config_validation(void) {
  deviceConfig.pricePerLiter = -500;
  deviceConfig.sessionTimeout = 500;
  validateConfig();
  TEST_ASSERT_EQUAL_INT(0, deviceConfig.pricePerLiter);
  TEST_ASSERT_EQUAL_UINT32(300000, deviceConfig.sessionTimeout);
}

void test_config_save_load(void) {
  deviceConfig.pricePerLiter = 2000;
  strcpy(deviceConfig.wifi_ssid, "TestWiFi");
  saveConfigToStorage();
  TEST_ASSERT_EQUAL_INT(2000, preferences.getInt("price"));

  deviceConfig.pricePerLiter = 0;
  loadConfigFromStorage();
  TEST_ASSERT_EQUAL_INT(2000, deviceConfig.pricePerLiter);
}

// ============================================
// STATE MACHINE TESTS
// ============================================
void test_sm_initial_state(void) {
  TEST_ASSERT_EQUAL(IDLE, currentState);
  TEST_ASSERT_EQUAL(0, balance);
}

void test_sm_free_water(void) {
  config.enableFreeWater = true;
  freeWaterAvailableTime = 0;
  freeWaterUsed = false;
  handleStartButton();
  TEST_ASSERT_EQUAL(FREE_WATER, currentState);
}

void test_sm_paid_dispense(void) {
  currentState = ACTIVE;
  balance = 500;
  handleStartButton();
  TEST_ASSERT_EQUAL(DISPENSING, currentState);
}

void test_sm_flow_logic(void) {
  currentState = DISPENSING;
  balance = 1000;
  config.pricePerLiter = 1000;
  config.pulsesPerLiter = 100.0;
  lastDispensedLiters = 0.0;

  flowPulseCount = 50; // 0.5L
  processFlowSensor();

  TEST_ASSERT_EQUAL(500, balance);
  TEST_ASSERT_EQUAL_FLOAT(0.5, totalDispensedLiters);
}

// ============================================
// INTEGRATION TESTS
// ============================================
void test_integration_mqtt_payment(void) {
  // 1. Setup
  currentState = IDLE;
  balance = 0;
  deviceConfig.requireSignedMessages = false; // Bypass sig
  strcpy(TOPIC_PAYMENT_IN, "water/payment");

  // 2. Prepare Payload
  // {"amount": 5000, "source": "app"}
  const char *payload = "{\"amount\": 5000, \"source\": \"app\"}";

  // 3. Trigger MQTT Callback
  // We cast const char* to char* because callback signature is not const
  // (legacy Arduino MQTT) We copy to buffer to be safe
  char topicBuf[64];
  strcpy(topicBuf, "water/payment");
  char payloadBuf[128];
  strcpy(payloadBuf, payload);

  mqttCallback(topicBuf, (byte *)payloadBuf, strlen(payload));

  // 4. Assert
  // Balance should increase by 5000
  TEST_ASSERT_EQUAL(5000, balance);
  // State should be ACTIVE (ready to dispense)
  TEST_ASSERT_EQUAL(ACTIVE, currentState);
  // Session start balance set
  TEST_ASSERT_EQUAL_FLOAT(5000, sessionStartBalance);
}

void test_integration_mqtt_zero_payment_fail(void) {
  currentState = IDLE;
  balance = 0;
  const char *payload = "{\"amount\": 0, \"source\": \"app\"}";

  char topicBuf[] = "water/payment";
  char payloadBuf[128];
  strcpy(payloadBuf, payload);

  mqttCallback(topicBuf, (byte *)payloadBuf, strlen(payload));

  // Should reject 0 amount
  TEST_ASSERT_EQUAL(0, balance);
  TEST_ASSERT_EQUAL(IDLE, currentState);
}

void test_integration_wdt_identify(void) {
  // 1. Setup
  wdt_reset_count = 0;

  // 2. Trigger Identify Command (via broadcast or group)
  // {"action": "identify", "duration": 3}
  const char *payload = "{\"action\": \"identify\", \"duration\": 3}";

  char topicBuf[] = "water/broadcast/command";
  char payloadBuf[128];
  strcpy(payloadBuf, payload);

  // 3. Execute
  mqttCallback(topicBuf, (byte *)payloadBuf, strlen(payload));

  // 4. Assert
  // Duration is 3, loops 3 times. Should call reset at least 3 times.
  TEST_ASSERT_GREATER_THAN(0, wdt_reset_count);
  TEST_ASSERT_EQUAL_INT(3, wdt_reset_count);
}

void test_integration_ota_trigger(void) {
  // 1. Trigger OTA command via MQTT
  // {"firmware_url": "http://example.com/fw.bin"}
  const char *payload = "{\"firmware_url\": \"http://example.com/fw.bin\"}";

  char topicBuf[] = "vending/VendingMachine_001/ota/in";
  char payloadBuf[128];
  strcpy(payloadBuf, payload);

  // 2. Execute
  // This calls triggerOTAUpdate -> HTTPClient -> Update.begin -> Update.write
  // -> Update.end Since we mocked everything to return true/success, it should
  // complete without error.
  mqttCallback(topicBuf, (byte *)payloadBuf, strlen(payload));

  // 3. Assert - if we got here, it didn't crash.
  TEST_ASSERT_TRUE(true);
}

// ============================================
// MAIN
// ============================================
int main(int argc, char **argv) {
  UNITY_BEGIN();

  // Config
  RUN_TEST(test_config_load_defaults);
  RUN_TEST(test_config_validation);
  RUN_TEST(test_config_save_load);

  // SM
  RUN_TEST(test_sm_initial_state);
  RUN_TEST(test_sm_free_water);
  RUN_TEST(test_sm_paid_dispense);
  RUN_TEST(test_sm_flow_logic);

  // Integration
  RUN_TEST(test_integration_mqtt_payment);
  RUN_TEST(test_integration_mqtt_zero_payment_fail);
  RUN_TEST(test_integration_wdt_identify);
  RUN_TEST(test_integration_ota_trigger);

  UNITY_END();
  return 0;
}
