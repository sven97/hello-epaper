#include "settings.h"
#include "config.h"
#include "state.h"

Settings settings;

// NVS keys (<=15 chars): sleepSecs imgUrl quietEn quietSh quietEh tzAuto
// devName rot. The operative UTC offset stays in the pre-existing "tzOff"
// key (written by auto-detect or by the portal in manual mode).
void loadSettings() {
    settings.sleepSecs = prefs.getUInt("sleepSecs", DEFAULT_SLEEP_SECONDS);
    settings.imageUrl = prefs.getString("imgUrl", DEFAULT_IMAGE_URL);
    settings.quietEnabled = prefs.getBool("quietEn", false);
    settings.quietStartHour = prefs.getUChar("quietSh", 23);
    settings.quietEndHour = prefs.getUChar("quietEh", 7);
    settings.tzAuto = prefs.getBool("tzAuto", true);
    settings.name = prefs.getString("devName", DEFAULT_DEVICE_NAME);
    settings.rotation = prefs.getUChar("rot", DEFAULT_ROTATION);
}

void saveSettings() {
    prefs.putUInt("sleepSecs", settings.sleepSecs);
    prefs.putString("imgUrl", settings.imageUrl);
    prefs.putBool("quietEn", settings.quietEnabled);
    prefs.putUChar("quietSh", settings.quietStartHour);
    prefs.putUChar("quietEh", settings.quietEndHour);
    prefs.putBool("tzAuto", settings.tzAuto);
    prefs.putString("devName", settings.name);
    prefs.putUChar("rot", settings.rotation);
}
