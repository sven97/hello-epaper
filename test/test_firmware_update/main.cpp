#include <unity.h>
#include "logic/firmware_update.h"

void setUp() {}
void tearDown() {}

static OtaGate ok() {
    // enabled, trusted build 100, clean, no trial, healthy battery
    return OtaGate{true, 100, false, false, 80};
}

void test_happy_path_checks_and_installs() {
    TEST_ASSERT_TRUE(shouldCheckForUpdate(ok()));
    TEST_ASSERT_TRUE(shouldInstallUpdate(ok(), 101));
}

void test_not_newer_does_not_install() {
    TEST_ASSERT_FALSE(shouldInstallUpdate(ok(), 100));
    TEST_ASSERT_FALSE(shouldInstallUpdate(ok(), 99));
}

void test_disabled() {
    OtaGate g = ok(); g.enabled = false;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
    TEST_ASSERT_FALSE(shouldInstallUpdate(g, 101));
}

void test_untrusted_build_zero() {
    OtaGate g = ok(); g.deviceBuild = 0;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_dirty_build() {
    OtaGate g = ok(); g.deviceDirty = true;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_trial_pending() {
    OtaGate g = ok(); g.trialPending = true;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_low_battery() {
    OtaGate g = ok(); g.batteryPct = OTA_MIN_BATTERY_PCT - 1;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
    g.batteryPct = OTA_MIN_BATTERY_PCT;
    TEST_ASSERT_TRUE(shouldCheckForUpdate(g));
}

void test_manual_ignores_enabled_toggle() {
    OtaGate g = ok(); g.enabled = false;
    TEST_ASSERT_TRUE(canManualUpdate(g));       // the toggle doesn't gate a manual check
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g)); // ...but the auto path still needs it
}

void test_manual_still_honours_other_gates() {
    OtaGate g = ok(); g.enabled = false;
    g.deviceDirty = true;  TEST_ASSERT_FALSE(canManualUpdate(g)); g.deviceDirty = false;
    g.trialPending = true; TEST_ASSERT_FALSE(canManualUpdate(g)); g.trialPending = false;
    g.deviceBuild = 0;     TEST_ASSERT_FALSE(canManualUpdate(g)); g.deviceBuild = 100;
    g.batteryPct = OTA_MIN_BATTERY_PCT - 1; TEST_ASSERT_FALSE(canManualUpdate(g));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_checks_and_installs);
    RUN_TEST(test_not_newer_does_not_install);
    RUN_TEST(test_disabled);
    RUN_TEST(test_untrusted_build_zero);
    RUN_TEST(test_dirty_build);
    RUN_TEST(test_trial_pending);
    RUN_TEST(test_low_battery);
    RUN_TEST(test_manual_ignores_enabled_toggle);
    RUN_TEST(test_manual_still_honours_other_gates);
    return UNITY_END();
}
