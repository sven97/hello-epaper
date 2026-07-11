#pragma once
#include <Arduino.h>

// Battery voltage in millivolts via the S3's eFuse-calibrated ADC.
int32_t readBatteryMv();

// Rough state-of-charge estimate from a Li-ion discharge curve.
int batteryPercent(int32_t mv);

// LED policy (active-LOW pin): Solid = awake and idle, Heartbeat = busy
// (fetching / dithering / refreshing the panel — proof it isn't hung),
// Off = about to sleep. A tiny FreeRTOS task owns the pin; ack blinks
// (blinkLed) temporarily take it over.
enum class LedMode : uint8_t { Off, Solid, Heartbeat };
void startLedTask(); // call once per boot before the first setLed()
void setLed(LedMode mode);

// LED feedback (active-LOW LED on LED_PIN).
void blinkLed(int times, int onOffMs = 150);

// True when a USB *host* (not a charger) is attached — used for developer
// mode (short sleeps so the serial port reappears frequently).
bool usbHostPresent();

// Seconds the next sleep will actually last: settings.sleepSecs, extended
// past the quiet-hours window when the wake would land inside it (only
// when the clock is NTP-sane — never trust a 1970 clock).
uint32_t plannedSleepSecs();

// Full deep sleep: panel to low power, enable-line GPIOs latched low,
// timer + any-button wake armed. Quiet-hours aware. Never returns.
void goToSleep();

// goToSleep(), unless a USB host is attached (dev mode) — then returns
// and loop() runs with live button polling and the port always up.
void maybeSleep();

// Fast re-arm without touching the panel or GPIO holds (held wakes and
// timer wakes landing inside the quiet window). Never returns.
void quickSleep(uint32_t secs);

// Debounced falling-edge press: true once per physical press (blocks
// until release). Shared by the dev-mode loop and the portal loop.
bool buttonPressed(uint8_t pin);
