#include <unity.h>
#include "logic/ota_trial.h"

void setUp() {}
void tearDown() {}

void test_not_on_trial() {
    OtaTrialState s{0, 100, 0, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::NotOnTrial, otaTrialVerdict(s));
}

void test_running_mismatch_reverts() {
    // flashed build 101, but we booted 100 -> revert already happened or
    // the swap didn't take; re-assert the good slot.
    OtaTrialState s{101, 100, 1, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_fetch_success_confirms() {
    OtaTrialState s{101, 101, 5, 2, true};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::ConfirmGood, otaTrialVerdict(s));
}

void test_fetch_fail_limit_reverts() {
    OtaTrialState s{101, 101, 1, OTA_TRIAL_MAX_FETCH_FAILS, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_boot_limit_reverts() {
    OtaTrialState s{101, 101, OTA_TRIAL_MAX_BOOTS, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_within_budget_continues() {
    OtaTrialState s{101, 101, 1, 1, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Continue, otaTrialVerdict(s));
}

void test_confirm_beats_limits() {
    OtaTrialState s{101, 101, OTA_TRIAL_MAX_BOOTS,
                    OTA_TRIAL_MAX_FETCH_FAILS, true};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::ConfirmGood, otaTrialVerdict(s));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_not_on_trial);
    RUN_TEST(test_running_mismatch_reverts);
    RUN_TEST(test_fetch_success_confirms);
    RUN_TEST(test_fetch_fail_limit_reverts);
    RUN_TEST(test_boot_limit_reverts);
    RUN_TEST(test_within_budget_continues);
    RUN_TEST(test_confirm_beats_limits);
    return UNITY_END();
}
