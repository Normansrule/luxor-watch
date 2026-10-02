# LUXOR SMARTWATCH — MASTER BUILD BRIEF (paste into a new chat; attach the latest Luxor_vN zip)

You are continuing an embedded hardware + firmware project. Read all of this before
suggesting changes. GOAL: take Luxor from firmware-at-hand to a REAL, wearable watch —
flashed and tested firmware, 3D-printed screw-together case you can print, and an
orderable bill of materials — with a design as polished, predictable, and easy to use
as a commercial watch. Firmware version at hand-off: v9 (Luxor_v9_IDF5_5_5.zip).

## 0. How to work (rules for whoever continues)
- Concise, direct answers. Write each acronym out once with the acronym in brackets.
- Never ship a package with a reused file name. Always bump main/version.h and the on-watch
  About + boot log line. Package name Luxor_vN_IDF5_5_5.zip → folder Luxor_vN.
- Any layout/geometry change is LAYOUT-TEST-FIRST: add/adjust tests/test_layout.c, then code.
  Every change must keep `make -C tests` AND `make -C tools/ui_sim LVGL_DIR=… check` (clipped
  text / missing glyphs on every screen x theme) passing before it is called done.
- Case changes: edit hardware/luxor_params.scad, then `python3 hardware/check_fit.py` and
  `python3 hardware/check_print.py` must pass; re-render hardware/stl.
- Don't guess sensor directions or physical dimensions — measure on the watch (calibration)
  or ask. A wrong ADC channel, connector polarity, or screw length can destroy hardware.
- Windows ESP-IDF commands start by `cd`-ing into the project folder.
- 3D parts are delivered as parametric OpenSCAD (.scad) source with all board-dependent
  dimensions as named variables at the top; the user renders STL and prints. Print
  test-fit coupons before any full part.

## 1. Hardware target (the whole board is the flash target; nothing else runs code)
Waveshare ESP32-S3-Touch-LCD-1.46B (cover-glass version).
| Property | Value |
|---|---|
| System-on-chip | Espressif ESP32-S3 (dual-core Xtensa LX7, 240 MHz, Wi-Fi 2.4 GHz + Bluetooth Low Energy) |
| Flash / PSRAM (pseudo-static RAM) | 16 MB flash (QIO, 80 MHz) / octal PSRAM 8 MB class (verify chip marking, e.g. ESP32-S3R8) |
| Build target | esp32s3 |
| USB programming | native USB Serial/JTAG, VID:PID 303a:1001; enumerates as COM5/COM8/COM9 — never assume |
| Flash layout | nvs 24 KB @0x9000, phy_init 4 KB @0xF000, factory app 4 MB @0x10000 |
| Orientation | USB-C at 6 o'clock | Mounting | 3 × M2 standoffs |

On-board parts firmware drives (reference, not bought separately):
Display 1.46" round 412×412 IPS SPD2010 (QSPI) · Touch SPD2010 (I2C 0x53) ·
IMU QMI8658 6-axis (I2C 0x6B, 0x6A fallback) · RTC PCF85063-class (I2C 0x51) ·
I/O expander TCA9554-class (I2C 0x20: touch reset, LCD reset, SD chip-select) ·
I2C SDA GPIO11 / SCL GPIO10 · Speaker I2S out BCLK48/WS38/DOUT47 ·
Mic I2S in BCLK15/WS2/DIN39 · Power hold GPIO7 (high = stay on) ·
PWR key GPIO6 / BOOT key GPIO0 (active low) · Backlight LEDC PWM ·
Battery sense ADC through on-board divider (read exact channel + ratio from board.c) ·
microSD slot chip-select via expander.

## 2. Toolchain & flashing
- ESP-IDF 5.5.5 (Windows: C:\Espressif\frameworks\esp-idf-v5.5.5-2; also WSL2 Ubuntu "Aleksander-Desk")
- LVGL (Light and Versatile Graphics Library) 8.3.11, espressif/esp_lcd_spd2010 ==2.0.0~1
- Fonts: Inter (OFL) generated into main/fonts by lv_font_conv (main/fonts/generate_fonts.sh)
- Flash (ESP-IDF 5.5 CMD, from the project folder):
  cd <project folder>
  idf.py set-target esp32s3
  idf.py build
  idf.py -p COM5 flash monitor

## 3. Firmware layout (main/)
watch_main.c boot order/main loop/buttons/gestures/standby/config saves ·
board.c/.h display,touch,IMU(runtime axis map),RTC,backlight,battery,buttons ·
ui.c/.h screens,theme,toast,tap-background test · layout.h ALL geometry (LVGL-free, host-tested) ·
luxconf.c/.h one NVS settings blob, versioned migration · motion.c/.h tilt(gravity low-pass),steps,wrist-raise,stillness ·
battery.c gauge filter (host-tested) · fonts/ Inter LVGL fonts ·
fitness.c gestures.c audio.c radio.c protocol.c state.c support · version.h version string ·
tests/ host tests (layout, motion, fitness, gestures, protocol, battery) via `make -C tests` ·
tools/ui_sim/ PC simulator of the real ui.c (render + check) · hardware/ OpenSCAD case.

## 4. Design system (keep it commercial-grade: calm, predictable, nothing overlapping)
- Default theme Ivory Gold: field #FFFFFF, ink #0A0A0A, secondary #5C5648, readable gold #A07C14,
  leaf gold #D4AF37. Also Porcelain, Noir Gold, Midnight Gold, Champagne Noir, Monochrome.
- Safe circle radius 188 px around (206,206); nothing interactive outside it.
- Screen law (enforced by tests/test_layout.c): header above, content middle, controls below,
  no two boxes intersect. Titles one word, no subtitles, gold hairline rule under the title.
- Time: colon pinned to a fixed axis (hours right-aligned, minutes left-aligned).
- Type: Inter Medium (14/16/20), Inter Display Medium 28, Inter Display Light 48. Pill buttons that
  settle 1 px on press; tiles tint + zoom to 95 % (lv_img zoom — NOT style transform_zoom, which
  needs a layer buffer the tile doesn't fit); 200 ms fade on screen change; boot wordmark splash.
- Ease-of-use bar (apply to every new screen): one primary action per screen; tap targets ≥ 44 px;
  every control gives immediate visual feedback; back/home always reachable; no hidden gestures.

## 5. Screens (current)
Launcher 7 flat-top hexagons (~6.9 px gap, one word): centre DISPLAY, 12 SETTINGS, 2 MUSIC,
4 CALC, 6 CLOCK, 8 MAZE, 10 STEPS. CLOCK ivory dress dial, gold bezel, 12 Roman numerals (IIII),
tapered gold hands as lv_line (not lv_meter), red sweep second, date window at 3. DISPLAY 6
procedural plates (Sunburst, Orbit, Guilloche, Dune, Atrium, Meridian), swipe/arrows, persisted.
MAZE circular arena r=108 at y=232, marble physics, first-open tilt calibration, reset+↑↓ chips.
STEPS three activity rings (steps gold, active min/30 rose gold, distance platinum) + stats row. MUSIC 4 synthesized tracks + level meter. CALC 19-key circle-aware
keypad, × ÷ − ±, 48 px light display. SETTINGS brightness,volume,theme,12/24h,sweep seconds,step goal,stride,timeout,set time,
Wi-Fi time sync,calibrate tilt,motion setup,reset steps,about. Sub-screens TIME, MOTION
(axis map + gyroscope zero + bubble level), ABOUT.

## 6. Interaction rules (do not change without being asked)
1 tap on dark = wake · 2 taps empty background = home · 3 taps empty background = off/on ·
taps on controls affect only the control (never a gesture) · BOOT key = home (40 ms debounce) ·
PWR short = wake/home, hold 3 s = power off (ignored until first release after boot) ·
wrist raise = wake · NO swipe-to-home (removed on purpose).

## 7. Motion / time / persistence (unchanged behaviour)
- Frame +X=3 o'clock, +Y=12 o'clock, +Z=out of glass. 4-bit axis map (0x1 swap XY, 0x2 invX,
  0x4 invY, 0x8 invZ); default LUX_IMU_MAP_DEFAULT=0x1 (luxconf.h constant, NOT Kconfig).
  Tilt from gravity only (250 ms low-pass). Maze ball rolls to lower edge; first open asks
  "tilt the USB-C edge toward the floor", flips invert-Y, stores tilt_cal.
- Timezone PST8PDT,M3.2.0,M11.1.0. RTC read before UI; if invalid, seed from build timestamp
  and mark time untrusted. Set via Settings→Set time (writes RTC) or Wi-Fi SNTP.
- Settings: one blob, NVS namespace "luxor", key "cfg", LUXCONF_VERSION=4. On mismatch keep
  personal settings, reset only fields whose defaults changed.

## 8. Status at v8 (host tests + UI simulator audit + full ESP-IDF 5.5.5 build; NOT yet on hardware)
Done v6: tap-gesture fix, debounce, marble tilt+calibration, lv_line hands, hex hit-testing, layout tests.
Done v7: battery badge (battery.c filter), About volts, gradient dithering, tile labels auto-fit.
Done v8: Inter type, fades + splash, press feedback, activity rings, music meter, calc glyphs, 10
clipped strings fixed, tools/ui_sim, full printable case set (shell, cover, gold inlay, dock,
coupons A/B/C) with check_fit.py + check_print.py.
Verify on hardware FIRST: docs/HARDWARE_CHECKS.md (v8 items + carry-overs: tilt direction, no self-return
to home, touch smoothness, fade/meter smoothness).
Build check method: ESP-IDF 5.5.5 in the cloud workspace with LVGL/SPD2010 vendored from GitHub
(Espressif registry unreachable there); UI simulator renders + audits ui.c on the host.

Done v9: published-repo layout (README, docs/BOM.md + bom.csv, docs/WIRING.md + wiring.svg,
docs/REQUIREMENTS.md, docs/PUBLISHING.md), 44 px touch areas on every control (size_control +
lux_hit_pad; layout test + ui_sim touch audit), STL freshness check, 3-job CI, README hero render.
Repo target: github.com/<owner>/luxor-watch, published with docs/PUBLISHING.md.
Board naming: Waveshare lists the cover-glass board as ESP32-S3-Touch-LCD-1.46 (1.46B = no glass);
confirm which the user owns.

## 9. Build roadmap (cheap/reversible steps first; firmware and case run in parallel)
FIRMWARE
1. Flash v6, confirm boot log `=== Luxor v6 boot ===` and Settings→About = v6.
2. Fix anything janky/overlapping (layout test first).
3. DONE v7 — battery percentage top-right of home.
4. microSD (TF) detect + storage used/free.   (next firmware milestone, v9)
5. Display follows wrist rotation (auto-rotate / wrist-raise behaviours).
6. Real music playback: data partition + LittleFS + IMA-ADPCM decoder + I2S streaming, fed by a
   PC conversion script for audio the user owns/licenses. Do NOT bundle copyrighted tracks.
7. Better synthesized tracks meanwhile.
8. Power: battery-life measurement, deeper sleep in standby.
HARDWARE (parallel)
9. 3D case — DONE as models in v8 (hardware/); NEXT: fill MEASURE/ASSUMED values, print coupons
   B → A → C, then parts.
10. Order BOM (section 10) once the two measure-first items are confirmed.
11. Assemble + tune power/sleep once parts arrive.

## 10. Bill of materials (orderable; Amazon sourcing preference)
| # | Item | Qty | Spec / note | Status |
|---|---|---|---|---|
| 1 | Waveshare ESP32-S3-Touch-LCD-1.46B (cover-glass) | 1 | the programmed module (section 1) | Owned |
| 2 | LiPo (lithium-polymer) single-cell 3.7 V | 1 | fits inside Ø46.2 mm bore under the board; ~200–400 mAh class; Waveshare lists the header as MX1.25 2-pin — POLARITY STILL TO VERIFY (photo of +/- silkscreen) | To buy (gated) |
| 3 | M2 screws | 3 | back cover into standoffs; length = back-cover thickness + engagement (MEASURE; ~M2×4–6 mm) | To buy (gated) |
| 4 | microSD (TF) card | 1 | for SD feature (goal 4) | Optional |
| 5 | Quick-release strap | 1 | 20 or 22 mm — match the lug gap chosen in CAD | To buy |
| 6 | Spring bars | 2 | same width as strap | To buy |
| 7 | Foam pad / Kapton tape | 1 | battery retention + insulation | To buy |
| 8 | Thin hookup wire | a.n. | only if battery lead needs extending | Optional |
| 9 | USB-C DATA cable | 1 | must carry data (not charge-only) | Owned |
| 10 | 3D-printed parts | set | section 11 | To print |
Gate rule: items 2 and 3 are ordered ONLY after the on-board battery header
(type + polarity, photo is fine) and the screw length (calipers) are confirmed.
Deliver the final BOM as a table with concrete Amazon picks once gates clear (docs/BOM.md has the
specs + search terms now; cell = 402530 class, screws = M2x6 at m2_engage 2.0).

## 11. 3D models (delivered as parametric OpenSCAD .scad; reference = Waveshare 1.46 STEP)
Fixed decisions: Ø50.2 mm outer, 2 mm uniform shell, Ø46.2 mm bore, screw-together
(shell around the display + back cover into the 3 M2 standoffs). Every board-dependent
dimension (button XY, USB-C cut, standoff XY, lug width) is a named variable at the top of the
file, filled from the STEP or measured; unknowns are clearly marked TODO with a sane default.
Parts: 1 Case shell (cover-glass retaining lip, integral lugs) · 2 Back cover (3 counterbored M2
holes to standoffs, battery pocket, speaker/mic vents, optional engraved logo) · 3 USB-C cut-out
at 6 o'clock sized for a real cable overmould · 4 Button openings + caps/plungers for PWR/BOOT ·
5 TF slot access (optional) · 6 Battery cradle/spacer · 7 Strap lugs with spring-bar holes ·
8 Optional gold bezel ring · 9 Optional charging stand leaving USB-C reachable ·
10 Test-fit coupons — PRINT THESE FIRST.
v8 status: ALL modelled in hardware/ (parts/01–04, coupons A/B/C, assembly.scad, stl/, renders/).
Shell has a flush gold inlay bezel (part 3), seamless flexure buttons, 22 mm lugs; cover clamps via
the 3 M2 standoffs, shell trapped through a 0.3 mm gasket, 12 o'clock key; watch 18.4 mm thick.
Standoff XY, standoff depth, button angles/depth, USB depth are ASSUMED (ORBIT B-series) — confirm
with coupon B / the STEP before printing parts.
Print guidance: state layer height, wall count, and material assumption; call out any overhang
needing supports; give a coupon → fit-check → full-part order of operations.

## 12. Definition of done (the real watch)
Firmware v-final flashes clean and boots; all hardware-verify items pass; `make -C tests` green;
case + back cover print and screw together over the board with the cover glass retained and the
battery safely cradled; watch runs untethered on battery; BOM fully specified and ordered.
Deliverables per milestone: bumped zip (send the file), changelog, and for hardware, the .scad
files + STL render notes + a short assembly step list.