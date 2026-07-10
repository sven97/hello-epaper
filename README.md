# ee02-frame

Firmware for the **Seeed Studio XIAO ePaper Display Board EE02** (XIAO
ESP32-S3 Plus) driving a **13.3″ Spectra 6 e-ink panel** (1200×1600, six
colors) as a battery-powered photo frame.

Every hour it wakes from deep sleep, fetches a random photo, dithers it to
the panel's six colors, refreshes, and goes back to sleep. Three buttons
control it; a full-screen info page reports its own state.

**Two personalities:** on battery or a charger it deep-sleeps between
refreshes (months per charge). Plugged into a computer it enters **dev
mode** — it stays awake, the serial port stays up for instant flashing,
and the buttons are polled live. A USB *host* is detected via the
USB-Serial-JTAG SOF frame counter, so chargers never trigger dev mode.

## Buttons

| Silkscreen | GPIO | Function |
|---|---|---|
| KEY1 | 2 | Toggle the full-screen **info page** (wake reason, last/next refresh, Wi-Fi, battery) vs. the photo. Redraws from a flash-saved copy — no network. |
| KEY2 | 3 | Fetch a **new picture** now. Held through a power-on: forget saved Wi-Fi and reopen the setup portal. |
| KEY3 | 5 | **Pin/freeze** the current photo — hourly refreshes pause (and barely sip battery) until pressed again. |
| RESET | — | Hardware reboot (clears boot counter and battery-delta memory). With BOOT held: flashing bootloader. |

Every button press is acknowledged by the LED within ~0.5 s (1 blink =
new picture, 2 = info page, 3 = pin), because the panel itself takes
~25–30 s to change — that's Spectra 6 physics, not a bug, and the panel
has **no partial-refresh mode** (color e-ink waveforms are full-panel only).

## First-time setup

1. Connect the panel's FPC cable, flip the power switch on, plug in USB-C.
2. The panel shows Wi-Fi instructions: join the `EE02-Setup` hotspot from
   a phone, open `http://192.168.4.1`, pick your **2.4 GHz** network.
3. Credentials persist on-device (NVS). Nothing secret ever enters this repo.

Timezone is detected automatically from the network's public IP on every
fetch (keyless `ip-api.com`), so DST and relocation self-correct within an
hour.

## Building & flashing

PlatformIO CLI. The display driver is selected entirely by the two
`build_flags` in `platformio.ini` — never edit library files.

```bash
pio run              # build
pio run -t upload    # flash over USB-C
pio device monitor   # serial at 115200 (USB-CDC)
```

**Flashing:** plugged into a computer, the board is in dev mode and never
sleeps — `pio run -t upload` just works. If it was last running on
battery/charger (asleep, USB port gone), wake it first: press any user
button and run the upload within the wake window (a port-watching loop
works well: `until ls /dev/cu.usbmodem* 2>/dev/null; do sleep 0.2; done;
pio run -t upload`), wait for the hourly self-wake, or hold **BOOT**, tap
**RESET**, release BOOT — then flash and press RESET after.

## Source layout

```
src/config.h    pins, buttons, timing, URLs, constants
src/display.*   EPaper object, 6-color dither, JPEG decode, frame save/load
src/net.*       Wi-Fi provisioning (WiFiManager), photo fetch, timezone+NTP
src/power.*     battery ADC + percent curve, LED, deep-sleep entry
src/ui.*        wake reason, info page, fetch metadata
src/main.cpp    wake dispatch: which wake does what
```

## Hardware notes (hard-won)

- **The 4 bpp framebuffer is palette-indexed.** `TFT_*` color macros are
  panel nibbles (WHITE=0x0, GREEN=0x2, RED=0x6, YELLOW=0xB, BLUE=0xD,
  BLACK=0xF) and `drawPixel` stores `color & 0x0F`. Raw RGB565 pushed via
  `pushImage` renders garbage — photos must be dithered to the palette.
- **Image source goes through images.weserv.nl** because picsum serves
  *progressive* JPEGs, which the on-device decoder can't parse; weserv
  re-encodes to baseline at exactly 1200×1600.
- **Deep sleep floats digital-only pads** — the panel/battery enable lines
  (GPIO43/6) are latched with `gpio_hold_en` before sleeping, released at
  boot. The held-timer fast path (`quickSleep`) deliberately leaves them
  latched.
- **The TZ environment doesn't survive deep sleep** — it's reapplied from
  the NVS-cached offset at every boot, or non-fetch wakes render UTC.
- **Battery**: charged from USB-C through the board's BQ24070 (~300 mA
  fast charge, ~30 mA precharge below 3 V). Its **8-hour safety timer**
  latches a fault on deeply discharged large packs — both charge LEDs go
  dark and charging stops; unplug/replug USB to resume. The battery
  percent shown is a discharge-curve estimate, not a fuel gauge.
- ADC reads use `analogReadMilliVolts` (eFuse-calibrated); the naive
  `raw/4095*3.3` conversion reads 20–30 % low on the S3.
