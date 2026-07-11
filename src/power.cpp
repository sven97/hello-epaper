#include "power.h"
#include "config.h"
#include "display.h"
#include "driver/gpio.h"
#include "soc/usb_serial_jtag_reg.h"
#include "logic/battery_curve.h"
#include "logic/quiet_hours.h"
#include "settings.h"
#include <time.h>

// A USB *host* (not a charger) sends a Start-of-Frame token every 1 ms,
// which increments the USB-Serial-JTAG frame counter. Sampling it twice a
// few ms apart detects a live host regardless of whether a terminal has
// the port open — chargers and battery power leave it frozen.
bool usbHostPresent() {
    uint32_t f1 = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG);
    delay(3);
    return REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) != f1;
}

// Deep sleep unless a development host is attached — then stay awake so
// the serial port never drops and loop() keeps the buttons live.
void maybeSleep() {
    if (usbHostPresent()) {
        Serial.println("dev mode: usb host attached — staying awake");
        return; // loop() takes over
    }
    goToSleep();
}

// Battery voltage in millivolts, via the S3's eFuse-calibrated ADC
// (analogReadMilliVolts) — the naive raw/4095*3.3 conversion read 20-30 %
// low because the ADC saturates below 3.3 V at default attenuation.
int32_t readBatteryMv() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(BATTERY_EN_PIN, OUTPUT);
    digitalWrite(BATTERY_EN_PIN, HIGH);
    delay(5);
    uint32_t sumMv = 0;
    for (int i = 0; i < 10; i++) {
        sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
        delay(2);
    }
    digitalWrite(BATTERY_EN_PIN, LOW);
    return (int32_t)(sumMv / 10) * 2; // /2 divider on the board
}

// Rough state-of-charge from a typical Li-ion discharge curve (resting
// voltage). No fuel gauge on board, so this is an estimate: reads a few
// percent high while charging and low under load.
int batteryPercent(int32_t mv) { return batteryPercentFromMv((int)mv); }

static volatile LedMode ledMode = LedMode::Off;

static void ledTask(void *) {
    for (;;) {
        switch (ledMode) {
            case LedMode::Off:
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LedMode::Solid:
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LedMode::Heartbeat:
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(pdMS_TO_TICKS(120));
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(pdMS_TO_TICKS(880));
                break;
        }
    }
}

void startLedTask() { xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr); }

void setLed(LedMode m) { ledMode = m; }

void blinkLed(int times, int onOffMs) {
    LedMode prev = ledMode;
    ledMode = LedMode::Off;
    delay(120); // let the task release the pin (its longest hold is 100 ms)
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(onOffMs);
        digitalWrite(LED_PIN, HIGH);
        delay(onOffMs);
    }
    ledMode = prev;
}

static int secondsOfLocalDay(time_t now) {
    struct tm lt;
    localtime_r(&now, &lt);
    return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

uint32_t plannedSleepSecs() {
    time_t now = time(nullptr);
    if (now <= CLOCK_SANE_EPOCH) return settings.sleepSecs;
    return quietAdjustedSleep(secondsOfLocalDay(now), settings.sleepSecs,
                              settings.quietEnabled, settings.quietStartHour,
                              settings.quietEndHour);
}

void goToSleep() {
    uint64_t secs = plannedSleepSecs();
    Serial.printf("sleeping %llu s (buttons also wake)...\n", secs);
    Serial.flush();
    epaper.sleep();                    // panel low-power mode
    pinMode(EPAPER_EN_PIN, OUTPUT);    // cut panel power rail
    digitalWrite(EPAPER_EN_PIN, LOW);
    // Deep sleep floats the pads unless held: latch the enable lines low
    // so the panel and battery divider stay off while sleeping.
    gpio_hold_en((gpio_num_t)EPAPER_EN_PIN);
    gpio_hold_en((gpio_num_t)BATTERY_EN_PIN);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void quickSleep(uint32_t secs) {
    Serial.printf("nothing to do — back to sleep %u s\n", secs);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

bool buttonPressed(uint8_t pin) {
    if (digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);
    return true;
}
