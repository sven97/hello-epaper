#include "power.h"
#include "config.h"
#include "display.h"
#include "driver/gpio.h"
#include "soc/usb_serial_jtag_reg.h"

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
int batteryPercent(int32_t mv) {
    struct Point { int16_t mv; uint8_t pct; };
    static const Point CURVE[] = {
        {4200, 100}, {4100, 94}, {4000, 85}, {3900, 74}, {3800, 62},
        {3700, 48},  {3600, 29}, {3500, 13}, {3400, 6},  {3300, 3},
        {3200, 1},   {3000, 0},
    };
    const int N = sizeof(CURVE) / sizeof(CURVE[0]);
    if (mv >= CURVE[0].mv) return 100;
    if (mv <= CURVE[N - 1].mv) return 0;
    for (int i = 1; i < N; i++) {
        if (mv >= CURVE[i].mv) {
            return CURVE[i].pct +
                   (int)(mv - CURVE[i].mv) *
                       (CURVE[i - 1].pct - CURVE[i].pct) /
                       (CURVE[i - 1].mv - CURVE[i].mv);
        }
    }
    return 0;
}

void blinkLed(int times, int onOffMs) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(onOffMs);
        digitalWrite(LED_PIN, HIGH);
        delay(onOffMs);
    }
}

void goToSleep() {
    uint64_t secs = SLEEP_SECONDS;
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

void quickSleep() {
    Serial.printf("nothing to do — back to sleep %llu s\n", SLEEP_SECONDS);
    Serial.flush();
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_sleep_enable_ext1_wakeup(BUTTON_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}
