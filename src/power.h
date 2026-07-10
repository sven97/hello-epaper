#pragma once
#include <Arduino.h>

// Battery voltage in millivolts via the S3's eFuse-calibrated ADC.
int32_t readBatteryMv();

// Rough state-of-charge estimate from a Li-ion discharge curve.
int batteryPercent(int32_t mv);

// LED feedback (active-LOW LED on LED_PIN).
void blinkLed(int times, int onOffMs = 150);

// True when a USB *host* (not a charger) is attached — used for developer
// mode (short sleeps so the serial port reappears frequently).
bool usbHostPresent();

// Full deep sleep: panel to low power, enable-line GPIOs latched low,
// timer + any-button wake armed. Never returns.
void goToSleep();

// goToSleep(), unless a USB host is attached (dev mode) — then returns
// and loop() runs with live button polling and the port always up.
void maybeSleep();

// Held timer wake: panel was never touched and the GPIO holds from the
// previous sleep are still latched — just re-arm and go. Never returns.
void quickSleep();
