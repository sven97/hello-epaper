#include <unity.h>
#include <cstring>
#include "logic/status_content.h"

void setUp() {}
void tearDown() {}

static ScreenContent normalContent() {
    ScreenContent c{};
    c.batteryPct = 84;
    strncpy(c.wifiBase, "strong", sizeof(c.wifiBase) - 1);
    strncpy(c.wifiSsid, "Studio Wi-Fi", sizeof(c.wifiSsid) - 1);
    strncpy(c.nextBase, "in 2h 15m", sizeof(c.nextBase) - 1);
    strncpy(c.settingsUrl, "http://paperframe.local", sizeof(c.settingsUrl) - 1);
    strncpy(c.lastIp, "192.168.1.42", sizeof(c.lastIp) - 1);
    strncpy(c.deviceId, "PF-A82F", sizeof(c.deviceId) - 1);
    return c;
}

static StatusData build(ScreenState s, const ScreenContent &c) {
    return buildStatusData(s, c, 1247, "Paperframe-Setup",
                           "13.3\" Spectra 6", 1200, 1600);
}

// ---- formatNextRefresh --------------------------------------------
void test_next_pinned_when_held() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), true, true, false, 7, 3600);
    TEST_ASSERT_EQUAL_STRING("Pinned", buf);
}

void test_next_relative_hours_and_minutes() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), false, false, false, 7, 3600);
    TEST_ASSERT_EQUAL_STRING("in 1h 0m", buf);
    formatNextRefresh(buf, sizeof(buf), false, false, false, 7, 5400);
    TEST_ASSERT_EQUAL_STRING("in 1h 30m", buf);
}

void test_next_relative_minutes_only() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), false, false, false, 7, 600);
    TEST_ASSERT_EQUAL_STRING("in 10m", buf);
}

void test_next_sub_minute() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), false, false, false, 7, 30);
    TEST_ASSERT_EQUAL_STRING("< 1 min", buf);
}

void test_next_paused_in_quiet_hours() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), false, true, true, 7, 999999);
    TEST_ASSERT_EQUAL_STRING("Paused until 07:00", buf);
}

void test_next_quiet_without_clock_falls_back_to_relative() {
    char buf[24];
    formatNextRefresh(buf, sizeof(buf), false, false, true, 7, 600);
    TEST_ASSERT_EQUAL_STRING("in 10m", buf);
}

// ---- deviceIdFromMac ---------------------------------------------
void test_device_id_from_mac_low_16_bits() {
    char buf[12];
    deviceIdFromMac(buf, sizeof(buf), 0x1122334455A82FULL);
    TEST_ASSERT_EQUAL_STRING("PF-A82F", buf);
    deviceIdFromMac(buf, sizeof(buf), 0x00FFULL);
    TEST_ASSERT_EQUAL_STRING("PF-00FF", buf);
}

// ---- buildStatusData: Normal -----------------------------------
void test_normal_version_line_is_build_number() {
    StatusData d = build(ScreenState::Normal, normalContent());
    TEST_ASSERT_EQUAL_STRING("Firmware build 1247", d.versionLine);
    TEST_ASSERT_EQUAL_STRING("Paperframe", d.title);
}

void test_normal_legend_and_qr_payload() {
    StatusData d = build(ScreenState::Normal, normalContent());
    TEST_ASSERT_EQUAL_STRING("Status", d.legend[0]);
    TEST_ASSERT_EQUAL_STRING("Refresh image", d.legend[1]);
    TEST_ASSERT_EQUAL_STRING("Pin image", d.legend[2]);
    TEST_ASSERT_EQUAL_STRING("http://paperframe.local", d.qrPayload);
    TEST_ASSERT_EQUAL_STRING("http://paperframe.local", d.urlPrimary);
}

void test_normal_ip_line_present_then_absent() {
    StatusData d = build(ScreenState::Normal, normalContent());
    TEST_ASSERT_EQUAL_STRING("Or http://192.168.1.42", d.urlSecondary);

    ScreenContent c = normalContent();
    c.lastIp[0] = '\0';
    d = build(ScreenState::Normal, c);
    TEST_ASSERT_EQUAL_STRING("", d.urlSecondary);
}

void test_normal_wifi_heading_is_ssid_and_label_from_base() {
    StatusData d = build(ScreenState::Normal, normalContent());
    TEST_ASSERT_EQUAL_STRING("Studio Wi-Fi", d.wifiHeading);
    TEST_ASSERT_EQUAL_STRING("Strong signal", d.wifiLabel);
    TEST_ASSERT_EQUAL_STRING("strong", d.wifiBase);
}

void test_normal_panel_spec_passthrough() {
    StatusData d = build(ScreenState::Normal, normalContent());
    TEST_ASSERT_EQUAL_STRING("13.3\" Spectra 6", d.panelDesc);
    TEST_ASSERT_EQUAL_STRING("1200 x 1600", d.resLine);
    TEST_ASSERT_EQUAL_STRING("PF-A82F", d.deviceId);
}

// ---- buildStatusData: Onboarding -----------------------------
void test_onboarding_qr_is_wifi_join_and_legend_trimmed() {
    ScreenContent c{};
    c.batteryPct = 61;
    StatusData d = build(ScreenState::Onboarding, c);
    TEST_ASSERT_EQUAL_STRING("WIFI:S:Paperframe-Setup;;", d.qrPayload);
    TEST_ASSERT_EQUAL_STRING("Status", d.legend[0]);
    TEST_ASSERT_EQUAL_STRING("", d.legend[1]);
    TEST_ASSERT_EQUAL_STRING("", d.legend[2]);
    TEST_ASSERT_EQUAL_STRING("Not connected", d.wifiHeading);
}

// ---- buildStatusData: Error --------------------------------
void test_error_legend_and_wifi_heading() {
    ScreenContent c = normalContent();
    strncpy(c.errorMsg, "image server said HTTP 404", sizeof(c.errorMsg) - 1);
    StatusData d = build(ScreenState::Error, c);
    TEST_ASSERT_EQUAL_STRING("Status", d.legend[0]);
    TEST_ASSERT_EQUAL_STRING("Retry", d.legend[1]);
    TEST_ASSERT_EQUAL_STRING("Pin image", d.legend[2]);
    TEST_ASSERT_EQUAL_STRING("Connection failed", d.wifiHeading);
    TEST_ASSERT_TRUE(strstr(d.actionLine1, "HTTP 404") != nullptr);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_next_pinned_when_held);
    RUN_TEST(test_next_relative_hours_and_minutes);
    RUN_TEST(test_next_relative_minutes_only);
    RUN_TEST(test_next_sub_minute);
    RUN_TEST(test_next_paused_in_quiet_hours);
    RUN_TEST(test_next_quiet_without_clock_falls_back_to_relative);
    RUN_TEST(test_device_id_from_mac_low_16_bits);
    RUN_TEST(test_normal_version_line_is_build_number);
    RUN_TEST(test_normal_legend_and_qr_payload);
    RUN_TEST(test_normal_ip_line_present_then_absent);
    RUN_TEST(test_normal_wifi_heading_is_ssid_and_label_from_base);
    RUN_TEST(test_normal_panel_spec_passthrough);
    RUN_TEST(test_onboarding_qr_is_wifi_join_and_legend_trimmed);
    RUN_TEST(test_error_legend_and_wifi_heading);
    return UNITY_END();
}
