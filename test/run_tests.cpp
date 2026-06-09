#include "mocks/Arduino.h"
#include "mocks/Preferences.h"
#include "mocks/display.h"
#include "mocks/esp_task_wdt.h"
#include "../../src_esp32_main/settings.h"
#include <unity.h>

// Include implementations for linkage
#include "mocks/MockDeps.cpp" // Data/config mocks
#include "mocks/MockImpl.cpp" // Arduino/Preferences globals

// Include source files under test
#define copyToBuffer copyToBuffer_config
#include "../../src_esp32_main/config_storage.cpp"
#undef copyToBuffer
#include "../../src_esp32_main/config_storage_validation.cpp"
#include "../../src_esp32_main/state_machine.cpp"
#include "../../src_esp32_main/local_events.cpp"

// ============================================
// SETUP / TEARDOWN
// ============================================
void setUp(void) {
  // Reset global mocks
  preferences.clear();
  setDispenseOutputsActive(false);

  // Reset State Machine logic
  initStateMachine();

  // Reset Config
  loadDefaultConfig();

  // Sync 'config' global with 'deviceConfig'
  config.pricePerLiter = 1000;
  config.sessionTimeout = 180000;
  // Sync deviceConfig
  deviceConfig.pricePerLiter = 1000;
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
  TEST_ASSERT_EQUAL_STRING(DEFAULT_DEVICE_ID, deviceConfig.device_id);
  TEST_ASSERT_EQUAL_INT(DEFAULT_PRICE_PER_LITER, deviceConfig.pricePerLiter);
}

void test_config_validation(void) {
  deviceConfig.pricePerLiter = -500;
  deviceConfig.sessionTimeout = 500;
  validateConfig();
  TEST_ASSERT_EQUAL_INT(DEFAULT_PRICE_PER_LITER, deviceConfig.pricePerLiter);
  TEST_ASSERT_EQUAL_UINT32(DEFAULT_SESSION_TIMEOUT_MS,
                           deviceConfig.sessionTimeout);
}

void test_config_save_load(void) {
  deviceConfig.pricePerLiter = 2000;
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

void test_sm_paid_dispense(void) {
  currentState = ACTIVE;
  balance = 500;
  handleStartButton();
  TEST_ASSERT_EQUAL(DISPENSING, currentState);
  TEST_ASSERT_TRUE(isRelayOn());
  TEST_ASSERT_TRUE(isRelay2On());
}

void test_sm_pause_logic(void) {
  currentState = DISPENSING;
  balance = 1000;
  setDispenseOutputsActive(true);
  handlePauseButton();
  TEST_ASSERT_EQUAL(PAUSED, currentState);
  TEST_ASSERT_EQUAL(1000, balance);
  TEST_ASSERT_FALSE(isRelayOn());
  TEST_ASSERT_FALSE(isRelay2On());
}

void test_sm_timeout_clears_balance(void) {
  currentState = PAUSED;
  balance = 3000;
  setDispenseOutputsActive(true);
  handleSessionTimeout();
  TEST_ASSERT_EQUAL(IDLE, currentState);
  TEST_ASSERT_EQUAL(0, balance);
  TEST_ASSERT_FALSE(isRelayOn());
  TEST_ASSERT_FALSE(isRelay2On());
}

// ============================================
// INTEGRATION TESTS
// ============================================
void test_integration_local_payment(void) {
  currentState = IDLE;
  balance = 0;

  processPayment(5000, "cash_uart", nullptr, nullptr);

  TEST_ASSERT_EQUAL(5000, balance);
  TEST_ASSERT_EQUAL(ACTIVE, currentState);
}

void test_integration_local_zero_payment_fail(void) {
  currentState = IDLE;
  balance = 0;

  processPayment(0, "cash_uart", nullptr, nullptr);

  TEST_ASSERT_EQUAL(0, balance);
  TEST_ASSERT_EQUAL(IDLE, currentState);
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
  RUN_TEST(test_sm_paid_dispense);
  RUN_TEST(test_sm_pause_logic);
  RUN_TEST(test_sm_timeout_clears_balance);

  // Integration
  RUN_TEST(test_integration_local_payment);
  RUN_TEST(test_integration_local_zero_payment_fail);

  UNITY_END();
  return 0;
}
