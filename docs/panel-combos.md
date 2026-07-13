# Panel combos for EE04 / EE05

EE04 and EE05 are universal driver boards, both supporting 24-pin FPC panels; EE04
additionally has a 50-pin FPC adapter (jumper-selected) that EE05 does not. The
vendored `Seeed_GFX` library supports swapping in a different panel by changing exactly
one number: `BOARD_SCREEN_COMBO` in the env's `build_flags` (`platformio.ini`). Nothing
else in this firmware needs to change — `src/display.cpp`'s palette and
`src/logic/layout_math.h`'s layout both key off the panel's actual size and the
library's own color-mode macros, not a hardcoded combo number.

To use a different panel: copy the `[env:ee04]` (or `[env:ee05]`) block in
`platformio.ini`, change `BOARD_SCREEN_COMBO`, done.

This table covers the XIAO-ePaper-family combos defined in
`.pio/libdeps/*/Seeed_GFX/User_Setups/Dynamic_Setup.h` (the ones built with
`ENABLE_EPAPER_BOARD_PIN_SETUPS`, i.e. the XIAO driver-board pinout family — as opposed
to the separate Wio Terminal / reTerminal combos in the same file, which are different
physical products). **Check Seeed's own EE04/EE05 product page or wiki for which of
these panels are actually sold as compatible with your specific driver board's FPC
connector** — this table is generated from the software driver definitions, which don't
encode physical/FPC compatibility.

| Combo | Panel | Driver | Resolution | `display.cpp` branch |
|---|---|---|---|---|
| 502 | 7.5″ mono (EE04/EE05 default) | UC8179 | 800×480 | mono default |
| 503 | 5.83″ mono | UC8179 | 648×480 | mono default |
| 504 | 2.9″ mono | SSD1680 | 128×296 | mono default |
| 505 | 1.54″ mono | SSD1681 | 200×200 | mono default |
| 506 | 4.26″ mono | SSD1677 | 800×480 | mono default |
| 507 | 4.2″ mono | SSD1683 | 400×300 | mono default |
| 508 | 2.13″ mono | SSD1680 | 128×250 | mono default |
| 509 | 7.3″ colorful (needs 50-pin adapter — EE04 only, EE05 has no 50-pin option) | ED2208 | 800×480 | `USE_COLORFULL_EPAPER` |
| 511 | 10.3″ 16-gray (same panel as EE03) | ED103TC2 | 1872×1404 | `USE_MUTIGRAY_EPAPER`+`GRAY_LEVEL16` |
| 512 | 2.9″ BWRY (black/white/red/yellow) | JD79667 | 128×296 | `USE_BWRY_EPAPER` |
| 513 | 2.13″ BWRY | JD79676 | 128×250 | `USE_BWRY_EPAPER` |
| 514 | 4.0″ colorful | ED2208 | 400×600 | `USE_COLORFULL_EPAPER` |
| 515 | 3.97″ mono | SSD1677 | 800×480 | mono default |
| 516 | 3.97″ (library sets colorful mode; filename says BWRY — verify on hardware) | SSD2677 | 800×480 | `USE_COLORFULL_EPAPER` |
| 517 | 1.54″ (library sets colorful mode; filename says BWRY — verify on hardware) | JD79660 | 200×200 | `USE_COLORFULL_EPAPER` |

Note on 200×200 and 128×250/296 panels (505, 504, 508, 513, 517): `layout_math.h`'s
formula avoids overflow/crashes down to these sizes, but the status/provisioning
screens (5 status lines + QR + a 3-line legend) won't lay out legibly that small — see
the compact-layout gap noted in `docs/hardware-checklist.md`.
