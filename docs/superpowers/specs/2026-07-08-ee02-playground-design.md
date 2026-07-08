# EE02 Playground — Design

**Date:** 2026-07-08
**Status:** Approved

## Purpose

A learning-first custom firmware for the Seeed Studio XIAO ePaper Display
Board EE02 (XIAO ESP32-S3 Plus + 13.3" Spectra 6 E-Ink panel). The goal is to
learn the hardware — display, buttons, Wi-Fi, battery, deep sleep — through a
sequence of small, individually verifiable milestones. There is no fixed end
product; a future photo-frame or dashboard firmware can grow out of this repo,
but that is explicitly out of scope here.

## Toolchain

- **Build system:** PlatformIO CLI (`pio run`, `pio run -t upload`,
  `pio device monitor`). No Arduino IDE.
- **Framework:** Arduino (C++ `setup()`/`loop()` model).
- **Board:** `seeed_xiao_esp32s3`, with `platformio.ini` overrides for the
  XIAO ESP32-S3 **Plus** variant: 16 MB flash, 8 MB PSRAM, PSRAM enabled.
  PSRAM is mandatory — Seeed_GFX requires it for panels ≥ 10.3".
- **Display library:** `Seeed_GFX` (Seeed-Studio/Seeed_Arduino_LCD), declared
  in `lib_deps`. It ships drivers for large ePaper panels including the
  13.3" E6 (Spectra 6) class and is explicitly PlatformIO-compatible.
- **Display config:** the EE02-specific driver/pin configuration (generated
  once via Seeed's online config tool) is checked in as a header so the repo
  builds without external setup steps.

## Firmware structure

- Single `src/main.cpp` to start; extract small helper headers only when a
  milestone genuinely needs one. No premature architecture.
- Serial logging at 115200 baud throughout, so the serial monitor always
  shows what the board is doing.

## Milestones

Each milestone must build, flash over USB-C, and produce an observable result
before the next one starts.

1. **Hello display.** Initialize the panel; draw text and shapes in all six
   Spectra colors (black, white, red, yellow, green, blue); perform a full
   refresh. *Success:* rendered output visible on the panel. This milestone
   also resolves the two known unknowns (board def overrides for the Plus,
   correct Seeed_GFX config for the EE02 panel).
2. **Buttons & LED.** The three user buttons (refresh / previous / next)
   switch between a few demo screens; onboard LED gives feedback. Teaches
   GPIO input, debouncing, and full- vs. partial-refresh behavior on Spectra 6.
   *Success:* pressing each button visibly changes the screen.
3. **Wi-Fi + fetch.** Connect to the LAN, HTTP-GET an image, decode and
   render it. Wi-Fi credentials live in an untracked `secrets.h` (a
   `secrets.h.example` is committed). *Success:* an image fetched over the
   network appears on the panel.
4. **Deep sleep & battery.** Read battery voltage via ADC, display it, then
   enter deep sleep with timer and button wake sources. *Success:* measurable
   sleep current behavior (device wakes on schedule and on button press) and
   a plausible battery reading on screen.

## Error handling

Learning-repo level: fail loudly on serial, retry where cheap (Wi-Fi connect,
HTTP fetch), and show a simple error state on the panel when a milestone-3+
network operation fails. No watchdog/OTA/recovery machinery.

## Verification

On-hardware verification only. Each milestone's success criterion is
observable on the panel or the serial monitor. No unit-test scaffolding —
YAGNI for a learning repo.

## Out of scope

- Photo-frame integration with the existing `myframe` server (natural follow-on
  after milestone 3, but a separate project/spec).
- SenseCraft HMI, ESPHome, ESP-IDF.
- OTA updates, power-optimization tuning beyond basic deep sleep.
