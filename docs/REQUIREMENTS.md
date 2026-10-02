# Requirements and verification

Each requirement has one verification method:

- **T** test: an automated check that fails the build
- **A** analysis: a script or render that proves it from the design
- **I** inspection: check by eye or with a meter
- **D** demonstration: do it on the watch

Status is what has actually been run. **Host ✓** means proven on a PC by the named check; **Watch ☐** is still to do on hardware, listed in [HARDWARE_CHECKS.md](HARDWARE_CHECKS.md).

## Function

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| F-01 | The watch shall show local time on its first frame after boot, read from the RTC before the UI is built | D | boot log, HARDWARE_CHECKS #1–2 | Watch ☐ |
| F-02 | The time display shall keep the colon on a fixed axis (hours right-aligned, minutes left-aligned) | I | `ui.c` timebox; `tools/ui_sim gallery` renders | Host ✓ |
| F-03 | The launcher shall offer 7 apps in an evenly spaced hexagon rosette | T | `test_layout.c` (rosette geometry, 6.9 px gaps) | Host ✓ |
| F-04 | 1 tap shall wake a dark screen; 2 taps on background go home; 3 taps on background turn the screen off/on; taps on controls never count as gestures | T | `tests/test_gestures.c` | Host ✓ · Watch ☐ |
| F-05 | The home screen shall show battery percentage top right, steady to ±1 % under ±40 mV noise | T | `tests/test_battery.c` | Host ✓ · Watch ☐ |
| F-06 | The step counter shall count zero steps over 60 s stationary and never ratchet on slow arm sway | T | `tests/test_motion.c` | Host ✓ |
| F-07 | Tilt shall come from gravity only, so a biased gyroscope cannot drift the marble | T | `test_motion.c` | Host ✓ |
| F-08 | After calibration, the marble shall roll towards the lowered edge | D | HARDWARE_CHECKS #11 | Watch ☐ |
| F-09 | Settings shall persist across power cycles in one versioned NVS (non-volatile storage) blob | D | change a setting, power off, check | Watch ☐ |
| F-10 | Holding PWR for 3 s shall power the watch off | D | HARDWARE_CHECKS | Watch ☐ |

## Interface quality

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| U-01 | Nothing the user reads or touches shall sit outside the 188 px safe circle | T | `test_layout.c` | Host ✓ |
| U-02 | No two interface boxes on a screen shall intersect | T | `test_layout.c` | Host ✓ |
| U-03 | Every control shall have a touch area of at least 44 × 44 px, and touch areas shall not overlap | T | `test_layout.c` + `tools/ui_sim check` (every real object, 11 screens × 6 themes) | Host ✓ |
| U-04 | No text shall be clipped and no glyph missing, on any screen in any theme | T | `tools/ui_sim check` | Host ✓ |
| U-05 | Every control shall give visible press feedback | I | HARDWARE_CHECKS #5–6 | Watch ☐ |
| U-06 | Gradients shall show no visible banding in 16-bit colour | I | ordered dithering; HARDWARE_CHECKS #13 | Host ✓ · Watch ☐ |

## Performance

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| P-01 | The firmware shall fit the 4 MB factory partition with at least 50 % free | A | `idf.py build` size report (64 % free) | Host ✓ |
| P-02 | The clock's second hand shall sweep (20 Hz updates) without visible stutter | D | HARDWARE_CHECKS #14 | Watch ☐ |
| P-03 | *Target:* the watch should run at least 8 hours on a 250 mAh cell | D | timed discharge (roadmap step 8) | not yet measured |

## Physical

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| M-01 | Case: Ø50.2 mm outer, 2 mm uniform wall, Ø46.2 mm bore | A | `luxor_params.scad` asserts | Host ✓ |
| M-02 | No printed part shall collide with another part, the glass, the board or the cell | A | `hardware/check_fit.py` | Host ✓ |
| M-03 | Every part shall print without supports (overhangs ≤ 45°, short bridges only) | A | `hardware/check_print.py` | Host ✓ |
| M-04 | Each STL shall be one watertight body | A | `check_print.py` | Host ✓ |
| M-05 | The watch shall be no thicker than 19 mm with a 402530 cell | A | render report: 18.4 mm | Host ✓ |
| M-06 | The front lip shall retain the cover glass without hiding the active display | A + T | lip asserts; coupon A | Host ✓ · Print ☐ |
| M-07 | The three M2 holes shall line up with the board's standoffs | T | coupon B | Print ☐ |
| M-08 | The lugs shall hold a standard 22 mm quick-release strap | T | coupon C | Print ☐ |

## Safety

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| S-01 | Battery polarity shall be confirmed with a meter before the first connection | I | [WIRING.md](WIRING.md#the-one-connection-you-make-the-battery) step 2 | Each build |
| S-02 | The battery shall stay unplugged while USB is connected until the charge current is verified for the chosen cell | I | README warning, ASSEMBLY step 8 | Each build |
| S-03 | The watch shall warn when the filtered cell voltage stays below 3.45 V for 10 s | I | `main/watch_main.c` | Watch ☐ |
| S-04 | Case screws shall not bottom out in the standoffs | A | `m2_engage` assert, screw-length echo | Host ✓ |
| S-05 | The first battery run shall be off-wrist on a non-flammable surface | I | ASSEMBLY step 8 | Each build |

## Openness

| ID | Requirement | Method | Verified by | Status |
|---|---|:-:|---|---|
| O-01 | Firmware, CAD and docs shall be published under open licences | I | `LICENSE.txt`, `THIRD_PARTY_NOTICES.md` | ✓ |
| O-02 | Anyone shall be able to rebuild the firmware with stock ESP-IDF 5.5.5 | T | GitHub Actions `build` job | runs on every push |
| O-03 | All CAD shall be parametric source, with STLs regenerated from it | A | `hardware/*.scad` → `hardware/stl/` | Host ✓ |
