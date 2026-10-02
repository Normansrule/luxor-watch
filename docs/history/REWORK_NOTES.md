# Luxor rework notes

> **Second pass** — the white/black/gold redesign, the separated honeycomb,
> the dress-watch dial, the Display gallery and the corrected tilt axis are
> described in *Second pass: the redesign* at the end of this file.

Three things were asked for: a more even and user-friendly design, a watch that
shows the correct time, and a gyroscope that is not backwards. This file says
what changed for each, how to check it on the hardware, and what to do if the
tilt direction is still wrong on your particular board.

---

## 1. The time

### What was wrong

Four separate faults stacked up.

1. `CONFIG_RW_TIMEZONE` defaulted to `"UTC0"`. Even with a perfectly working
   real-time clock (RTC) chip, the watch displayed Coordinated Universal Time
   (UTC), which in San Pedro is seven or eight hours ahead of the wall clock.
2. The RTC was read at boot step 5 of 7, *after* the interface had been built
   and drawn. The first frames therefore showed placeholders, and the hands
   jumped once the read landed.
3. If the RTC came back with a year before 2024, the clock stayed unset
   forever and the face showed `--:--` with no way out.
4. There was no way to set the time on the watch. Your only options were a
   Wi-Fi Simple Network Time Protocol (SNTP) sync or a Bluetooth Low Energy
   (BLE) write from a phone. If neither was available, the watch was a
   very nice paperweight with no hands.

### What it does now

- The timezone default is `PST8PDT,M3.2.0,M11.1.0` — US Pacific with the
  standard daylight-saving rules. Change it in `idf.py menuconfig` under
  **LUXOR -> POSIX timezone string** if you move; a few common strings are
  listed in the help text.
- `setenv("TZ", ...)` / `tzset()` and the RTC read now happen at step **3 of
  7**, *before* `luxor_ui_init()`. The first frame the panel ever shows
  already has the right time on it.
- If the RTC reads back nonsense (or is missing entirely), the clock seeds
  itself from the firmware's own build timestamp — `__DATE__` / `__TIME__`
  parsed into local time. That is never exactly right, but it is within hours
  or days instead of being stuck in 1970, so the calendar, the day rollover
  for step counting, and the hands all behave. The time is flagged
  *untrusted* and Settings says so.
- **Settings -> Set time** is a new screen: year, month, day, hour, minute,
  each with a stepper. Saving runs `mktime()` with `tm_isdst = -1`, so the
  daylight-saving offset is resolved from the timezone rules rather than
  guessed, then calls `settimeofday()` *and* writes the RTC chip so it
  survives a reboot.
- 12-hour versus 24-hour is now a runtime switch in Settings, not the
  compile-time `CONFIG_RW_TIME_24H` it used to be.
- A Wi-Fi time sync now also writes the RTC and shows a "Time synced" toast.

### How to check it

Flash, and watch the serial log for the `clock:` line at boot:

```
I Luxor: clock: RTC gave 2026-09-19 10:14:33 local (PST8PDT,M3.2.0,M11.1.0)
```

If instead you see `clock: no valid RTC; seeded from the build timestamp`, the
RTC chip lost power or was never set — open **Settings -> Set time**, set it
once, and it will persist from then on.

---

## 2. The gyroscope / tilt orientation

### What was wrong

The tilt maze took `roll` and `pitch` out of a complementary filter that
*integrated the gyroscope*. Two consequences:

- A gyroscope with any zero-rate bias — all of them have some — slowly walked
  the estimate, so the ball drifted even on a flat table.
- The mapping from sensor axes to screen axes was effectively
  `screen_x ∝ +sensor[1]`, `screen_y ∝ +sensor[0]`, which is a swap with no
  inversion. That is the mirror of what the board actually does, which is the
  backwards behaviour you saw.

There was also no way to correct it without editing source and rebuilding.

### What it does now

There is an explicit **watch frame**, defined once and used everywhere:

```
+X  towards 3 o'clock   (screen right)
+Y  towards 12 o'clock  (screen up)
+Z  out of the glass    (towards your eye)
```

`board_imu_read()` maps the raw QMI8658 axes into that frame using a four-bit
axis map — swap X/Y, then invert X, Y, Z individually. That covers all eight
orientations a part can be soldered in. The gyroscope is multiplied by the
map's determinant as well, because angular rate is a pseudovector and a single
axis flip would otherwise reverse its sense relative to the accelerometer.

`motion.c` then reports tilt in **screen directions**, from a 250 ms low-pass
of gravity only. The gyroscope no longer feeds the tilt estimate at all, so a
biased part cannot walk the maze ball:

```
tilt_x > 0  ->  the 3 o'clock edge is lower  ->  the ball rolls right
tilt_y > 0  ->  the 6 o'clock edge is lower  ->  the ball rolls down
```

The default map is `0x5` (swap X/Y, invert Y), which is the exact reverse of
the behaviour you reported as wrong.

### How to verify on the watch, and how to fix it if it is still wrong

Open **Settings -> Motion setup**. There is a live bubble-level dot plus the
raw sensor numbers.

1. Hold the watch flat, glass up. The dot should sit dead centre.
2. Tip the **3 o'clock edge (right side) down**. The dot must move **right**.
3. Tip the **6 o'clock edge (bottom, the USB-C side) down**. The dot must move
   **down**.

If either is backwards, use the buttons on that screen:

| Symptom | Press |
|---|---|
| Dot moves left when you dip the right edge | **FLIP X** |
| Dot moves up when you dip the bottom edge | **FLIP Y** |
| Dipping the right edge moves the dot vertically (and vice versa) | **SWAP** |

Fix the swap first if it applies, then the flips. The setting is written to
non-volatile storage (NVS) immediately — no rebuild, and it survives reflashing
as long as you do not erase the whole flash.

**ZERO GYROSCOPE** on the same screen averages ~2 seconds of genuinely still
samples and stores the bias. It refuses to finish if you move, rather than
silently recording a wrong number.

If you would rather bake in a different default for a fresh flash, it is
`idf.py menuconfig` -> **LUXOR -> Default IMU axis map**.

### What is tested

`tests/test_motion.c` asserts the direction convention directly, so a future
edit cannot silently re-mirror it:

```
3 o'clock edge down -> tilt_x = +0.50 (ball rolls right)
6 o'clock edge down -> tilt_y = +0.50 (ball rolls down)
9 o'clock edge down -> tilt_x = -0.50
```

It also holds a fixed attitude for 60 simulated seconds with a 40 deg/s
gyroscope offset applied and asserts the tilt moves by less than 0.001 — that
is the drift bug, pinned so it cannot come back.

---

## 3. The design

### Geometry is now computed, not typed

Every coordinate moved into `main/layout.h`, which contains no LVGL at all —
just numbers and small helper functions. `tests/test_layout.c` compiles that
header on your PC and proves the layout before any hardware is involved.

The screen is 412 x 412 with the centre at (206, 206), a **safe radius of
188 px** and a decorative ring at 202. `lux_half_width_at(y)` gives the usable
half-width at any row, so "does this rectangle fit inside a circle" is a
function call rather than a guess.

Things the test caught and forced fixed, which had been wrong in the shipped
layout:

- The honeycomb ring was **not** actually even. Neighbour distances were
  95 px on some spokes and 101 px on others, and the centre tile sat at
  y = 197 instead of 206.
- The home clock strip was 150 px wide at y = 30, which poked past the safe
  circle. Now 132 px at y = 32.
- The settings list was 260 px wide at x = 76 — same problem. Now 248 at 82.
- The music disc and the transport row were colliding at the bottom.

### The honeycomb

Real flat-top hexagons, R = 56, neighbour spacing d = 97, generated from exact
offsets `(0, ±d)` and `(±3R/2, ±d/2)`. That is point-symmetric by
construction, so the rosette is even no matter what. The furthest vertex is
148 px from the centre, which leaves a clean annulus for the clock strip at
the top and the status strip at the bottom.

Slot assignment follows your spec: Display in the centre, Settings at 12,
Music at 2, Calculator at 4, Clock at 6, Tilt Maze at 8, Fitness at 10. Seven
launcher applications, exactly as specified — the three new screens (Set time,
Motion setup, About) are reached from Settings, so the launcher is untouched.

### The watch face

Rebuilt on `lv_meter` instead of 60 individual `lv_obj` dots. One tick scale
(61 ticks over 360 degrees with rotation 270, which is the only arrangement
that spaces ticks at exactly 6 degrees in LVGL 8.3) plus three needle-only
scales with ranges 0..720, 0..3600 and 0..60000 ms. The millisecond range is
what gives a genuinely smooth sweeping second hand rather than a 1 Hz
stutter — and there is a Settings switch to turn the sweep off if you prefer
the tick.

### Everything else

- **Calculator**: the keypad is circle-aware. Rows 0–3 hold four keys, row 4
  holds three, 19 total, all inside the safe circle. Operators are ASCII
  (`/`, `*`) because the Montserrat fonts do not carry the multiplication and
  division signs, so the old glyphs would have rendered as empty boxes.
- **Tilt maze**: the arena is now a circle of radius 148 with six inner walls,
  instead of a rectangle that the round glass was cutting the corners off.
- **Settings**: a scrollable flex list with sliders, switches, steppers and
  action rows, rather than a fixed page of buttons.
- **Fitness**: distance and goal percentage were being computed and then never
  displayed. They are on screen now.
- **Hex tiles**: drawn once into two `LV_IMG_CF_TRUE_COLOR_ALPHA` canvas
  sprites in PSRAM and reused, rebuilt only on a theme change. If the
  allocation fails the code falls back to rounded buttons rather than crashing.
- **Labels**: `set_text()` compares before writing. The old code called
  `lv_label_set_text` unconditionally at 20 Hz, which invalidated and redrew
  the whole label every frame whether or not anything had changed.
- **Frame rate**: 20 Hz only where it shows — the sweeping second hand, the
  maze ball, the bubble level. Static screens redraw at 5 Hz.
- **Colour**: `CONFIG_LV_COLOR_16_SWAP=y`. The SPD2010 wants big-endian
  RGB565; LVGL now emits it directly instead of the CPU byte-swapping every
  pixel of every flush.
- **Backlight**: a gamma-2.2 perceptual curve with a floor of 12/1023. The
  linear duty cycle made the low brightness steps visually indistinguishable.
- **Battery**: a four-point lithium-polymer discharge curve
  (3.30 / 3.60 / 3.75 / 3.95 / 4.20 V) instead of a linear voltage-to-percent
  map, which reads far too optimistic in the middle of the range.
- **Never crash**: `board_init()` no longer `ESP_ERROR_CHECK`s the expander,
  the RTC, the backlight or the ADC. Only a dead I2C bus is fatal. This was
  in your spec already; the code did not honour it.
- **Settings persistence**: one `luxor_config_t` blob in NVS namespace
  `luxor`, key `cfg`, with a validation pass on load, replacing a scatter of
  individual keys. Writes are debounced by 3 seconds so dragging a slider
  does not hammer the flash.
- The music track named "Luxor Drift" is now "Slow Orbit", matching the spec.

---

## Building and flashing

From the ESP-IDF 5.5.5 command prompt, in the `firmware` directory:

```
idf.py set-target esp32s3
idf.py menuconfig          # optional: timezone, Wi-Fi credentials
idf.py build
idf.py -p COM9 flash monitor
```

Your board's COM port has moved between COM5, COM8 and COM9 before, so check
Device Manager rather than assuming — or let esptool find it with
`idf.py flash monitor` and no `-p`.

Because `sdkconfig.defaults` changed (colour byte order, extra fonts, the
meter and canvas widgets), delete any existing `sdkconfig` before the first
build so the new defaults are actually picked up:

```
del sdkconfig
idf.py fullclean
```

## Running the host tests

These need no ESP-IDF and no hardware — any gcc or clang will do, so the WSL2
Ubuntu box is the easy place:

```
cd firmware/tests
make
```

Five binaries: layout geometry, motion/tilt/steps, fitness, gestures and the BLE protocol parser.

## Files added or rewritten

| File | Status |
|---|---|
| `main/layout.h` | new — all geometry, no LVGL |
| `main/luxconf.h` / `main/luxconf.c` | new — one persisted config blob |
| `main/watch_main.c` | rewritten — clock before UI, new command handling |
| `main/ui.h` / `main/ui.c` | rewritten — all eleven screens |
| `main/motion.h` / `main/motion.c` | rewritten — screen-direction tilt |
| `main/board.h` / `main/board.c` | patched — axis map, non-fatal init, curves |
| `main/Kconfig.projbuild` | rewritten — real timezone, runtime IMU map |
| `sdkconfig.defaults` | updated — colour order, fonts, widgets |
| `tests/test_layout.c` | new |
| `tests/test_motion.c` | rewritten |
| `tests/Makefile` | new |


---

# Second pass: the redesign

## The palette is now white, black and gold

The default theme is **Ivory Gold**: a white field, black type, gold for
rules, rims, hands and the one element on a screen that matters. Nothing
else gets a colour, which is what lets the gold read as gold rather than as
decoration.

Two golds, because one cannot do both jobs on white:

| Token | Hex | Job |
|---|---|---|
| `LUX_GOLD_DEEP` | `#A07C14` | text and hairlines — passes on white |
| `LUX_GOLD` | `#D4AF37` | leaf tone — fills, rims, hands |
| `LUX_INK` | `#0A0A0A` | type |
| `LUX_INK_SOFT` | `#5C5648` | secondary type |
| `LUX_PAPER` | `#FFFFFF` | the field |

Themes now carry a `light` flag, and every component that used to assume a
dark background asks for it. That mattered in more places than expected: the
maze ball was `#EDF2F8`, which is invisible on paper, so it now flips to ink;
outlined buttons had a 40 % gold hairline that simply vanished, so on a light
theme they get a 2 px edge at 80 %; the home screen's edge ring was drawn at
20 % opacity, which is nothing on white.

Six themes remain — **Ivory Gold**, **Porcelain**, **Noir Gold**, **Midnight
Gold**, **Champagne Noir**, **Monochrome** — but the picker moved (see below).

## The honeycomb no longer touches

Two flat-top hexagons of circumradius R, laid out at spacing `sqrt(3)·R`,
share an edge *exactly*. The old rosette was R = 56 at a spacing of 97, which
is that case to within a rounding error — the tiles were welded into one slab.

Layout radius and drawn radius are now separate numbers:

```
LUX_HEX_R      52    pitch — sets where the centres go
LUX_HEX_DRAW_R 48    the radius the tile is actually drawn at
LUX_HEX_D      90    centre-to-centre spacing

gap = 90 - sqrt(3) x 48 = 6.9 px on every edge
```

`tests/test_layout.c` now asserts that gap is greater than 4 px and prints it,
so nobody can quietly weld them back together.

The smaller rosette also frees a real annulus top and bottom — 56 px above and
below instead of 9 px — which is what made room for the time to be laid out
properly rather than crammed against the glass.

## No captions under the app names

The launcher tiles carried a grey second line (`THEME / display`,
`SETUP / settings`). Gone. One word per tile, centred, letter-spaced:
**DISPLAY, SETTINGS, MUSIC, CALC, CLOCK, MAZE, STEPS**. A tile that needs a
caption to be understood needs a better word, not a caption.

The same applies to every app screen. `app_header()` no longer takes a
subtitle argument at all, so the caption cannot come back by accident. What
used to separate the title from the content is now a short gold rule.

## The time lines up

A centred time label wanders. `1:05` and `12:48` are different widths in a
proportional font, so the colon slides left and right as the hour changes and
the whole face reads as loose. That is the "time doesn't line up" problem, and
it is not fixable by nudging coordinates.

The fix is to split the string at the colon and anchor the halves to a fixed
axis:

```
hours + colon   right-aligned, ending at the axis
minutes         left-aligned,  starting at the axis
meridiem        a small subordinate mark at a fixed offset
```

The colon is now nailed to one column all day and the digits grow outward
symmetrically. It is the same `mk_timebox()` widget on the home screen and on
the watch face, so the two agree.

## The watch face

Rebuilt as a dress watch rather than a dashboard gauge:

- **Gold bezel** — a 6 px gold ring with a deeper gold hairline inside it,
  sitting outside the dial like a case.
- **Ivory dial** with a vertical gradient, a gold rim, and two concentric
  hairlines (chapter ring at 336 px, inner ring at 212 px). Two hairlines are
  most of the difference between a dial and a circle with hands on it.
- **Twelve Roman numerals**, not four Arabic ones, with **IIII** at four
  o'clock in the horological convention. Numerals at the quarters are set one
  size larger. The test checks all twelve boxes against the safe circle and
  checks the 72 px numeral pitch against the 54 px box, so they cannot collide.
- **Tapered hands.** Each hand is two overlapping needles on the same scale —
  a wide stub and a narrow tip — which reads as a taper without redrawing a
  polygon sixty times a minute. Hour and minute in deep gold; the second hand
  is a thin lacquer red, the one place in the whole interface that is neither
  black nor gold, because a sweep hand needs to be findable.
- **Gold centre cap** over the pivot so the three hands meet cleanly.
- **Date aperture** at 3 o'clock: ink on paper in a gold frame.
- No HOME chip. It would have landed on top of the XII. Double-tap the glass,
  or press either physical button.

## Display is a gallery now

The Display app was a theme picker wearing a preview. It is now what you
asked for: artwork, big, as the point of the screen. A 252 px plate in a gold
mount, swipe or arrows to change, six plates:

**Sunburst · Orbit · Guilloche · Dune · Atrium · Meridian**

They are drawn procedurally at runtime rather than stored. Six 252×252 images
would be 762 kB of flash; the drawing code is a couple of kilobytes and
re-renders itself in whatever palette the theme is using. All original
geometric compositions — no bundled photographs, no licensed artwork.

The plate is rendered once into a PSRAM canvas and reused, redrawn only when
you change plate or theme. Your choice persists.

Theme selection moved to **Settings → Theme**: one row with a swatch, the
theme name, and a tap to advance. It used to be a whole app, which is a lot of
screen for a setting you change twice.

## The tilt maze was upside down

You were right, and the axis map was the place to fix it rather than putting a
minus sign in the maze. Left/right was correct, up/down was inverted, so
`LUX_IMU_INV_Y` comes out of the default:

```
old default 0x5 = SWAP_XY | INV_Y
new default 0x1 = SWAP_XY
```

The bubble level in Motion setup reads from the same mapped sample, so the two
screens still cannot disagree.

**The part that matters for a watch that is already flashed:** the settings
blob lives in non-volatile storage (NVS), so simply changing the compile-time
default would have done nothing — the old wrong value would have won forever
and the only fix would have been erasing the flash. `luxconf` now carries a
version number. On a mismatch it keeps your personal choices and resets only
the fields whose *defaults* changed — the axis map and the theme — then writes
the blob back. You will see this once in the log:

```
W luxconf: settings upgraded v1 -> v2; axis map and theme reset to the
           corrected defaults
```

If your board still disagrees, **Settings → Motion setup** overrides it and
that choice sticks.

---

# Third pass: the tilt, for good, and the overlaps

## Why the tilt was still upside down

Left/right correct and up/down still inverted, after the default had been
changed, pointed at one of two things: a sign error in `motion.c` that agreed
with its own test, or a default that never reached the board.

The sign chain checks out against physics. The accelerometer reports the
*up* direction in the watch frame; a ball rolls towards the opposite; screen
y grows downwards where watch Y grows upwards; so `tilt_y = +uy`. And
`board.c` swaps before it inverts, so `LUX_IMU_INV_Y` acts on the watch Y
axis, not on raw Y. Toggling it does flip up/down.

Which leaves the default. `luxconf_defaults()` read `CONFIG_RW_IMU_MAP` from
Kconfig, and Kconfig's value is whatever is in `sdkconfig`. A stale
`sdkconfig` in the build directory re-injects the old value, and the version
migration — which resets the map to *the default* — then faithfully resets
it to the old, wrong value. It worked exactly as written and did nothing.

Three changes so this cannot recur:

1. **The default is no longer a Kconfig symbol.** It is
   `LUX_IMU_MAP_DEFAULT` in `luxconf.h`, a plain constant compiled into the
   binary. No `sdkconfig` on earth can change it.
2. **`LUXCONF_VERSION` is 3**, so every watch with stored settings migrates
   again and picks up the constant.
3. **A flip control on the maze screen itself.** The two chips under the
   arena are *reset* and *flip up/down*. The flip toggles `LUX_IMU_INV_Y` on
   the stored map — the same operation as Motion setup's FLIP Y, so the bubble
   level and the game cannot disagree — and it persists. If a board is
   soldered differently from mine, one tap fixes it without a menu.

## Overlaps, and a test that catches them

The layout test had been checking that things fit *the glass*. It had not
been checking that things fit *each other*. Every one of these was invisible
to it:

| Screen | What was on top of what |
|---|---|
| Fitness | 284 px goal ring ran under the HOME button, the title and the rule |
| Maze | HOME button, title and reset button all sat on the arena |
| Display | artwork mount (top edge y = 65) sat under the title and the HOME button |
| Motion setup | status line at y = 382, 260 px wide — 37 px outside the safe circle |
| Music | footer at y = 366, 200 px wide — 8 px outside the safe circle |

Every screen now follows one rule: **header above, content in the middle,
controls below, and no element's box intersects another's.** Concretely:

- **Fitness**: ring shrunk to 216 px and dropped to y = 232, under the rule.
  All three text lines sit inside the ring's clear inner circle, and the test
  checks all twelve corners against that circle so no glyph ever crosses the
  arc.
- **Maze**: arena is 108 px radius at y = 232. Header above, two glyph-only
  chips below. No status text on the screen at all — a solve shows a toast.
  The walls were redrawn for the smaller arena, and the test now **flood-fills
  the arena on a 2 px grid using the firmware's own collision rule** and
  demands a path from the start to the goal. "Reachable" is no longer a
  comment.
- **Display**: no masthead. The plate is the point of the screen, so it gets
  a HOME button and nothing else; its name is the caption. The mount now
  clears the button by 4 px and the test checks it.
- **Motion setup**: the raw readout moved to y = 114, between the rule and
  the level, where it fits.
- **Music**: footer at y = 356, 150 px wide, inside the circle.
- **Clock**: the weekday line under the digital time is gone — the date
  aperture already gives the day, the home screen gives the rest, and the
  face reads cleaner for it.

Every masthead screen now asserts that its first content element clears
`LUX_RULE_Y`, and every footer asserts it fits the safe circle at its real
box size.

---

# Fourth pass: it calibrates itself, and touch stops fighting you

## The tilt, finally

Three builds changed the axis-map default and three times the report came
back the same. Whatever the reason — a stale config, a board soldered
differently from the one I derived the map from, or the value simply never
reaching the sensor path — the lesson is that the default is not something
I can determine from here. So the watch determines it.

**The first time the maze opens, it calibrates.** The ball is hidden and the
arena says:

```
TILT THE WATCH
USB-C EDGE
TOWARDS THE FLOOR
```

That is a physical instruction with exactly one meaning. For that movement
the ball *must* roll down the screen. The firmware watches the mapped tilt
for a decisive, mostly-vertical reading held for about 0.4 s; if it comes
back negative, the Y-inversion bit is flipped and the corrected map is
stored. Then the ball appears, the maze resets, and a toast says either
"Calibrated" or "Calibrated - up/down corrected". It runs once.
**Settings → Calibrate tilt** runs it again, and the ↑↓ chip under the arena
is still there as a manual override.

`LUXCONF_VERSION` is 4, which clears the calibrated flag on every watch, so
this build calibrates on first open regardless of what was stored before.

## Why touch felt janky

The watch face was the culprit, and it leaked into everything.

`lv_meter` invalidates the **entire meter** every time any needle moves. The
dial is 364 × 364 px; at RGB565 that is 265 kB pushed over QSPI at 40 MHz,
about 55 ms — and the sweep second hand moved it twenty times a second. LVGL
was spending more than 100 % of its time budget re-sending the same face,
and touch is read by the same task, so taps queued up behind frames.

The hands are now `lv_line` objects. A line invalidates only its own
bounding box, and each hand is moved to the tight box around its two points
before its points are set, so the redraw is a few kilobytes rather than a
quarter of a megabyte. The sweep is also quantised to 1/12 s, which the eye
cannot distinguish from continuous and which costs a sixth as much.

Three smaller things in the same direction:

- `CONFIG_LV_INDEV_DEF_READ_PERIOD` is 10 ms and
  `CONFIG_LV_DISP_DEF_REFR_PERIOD` is 16 ms, replacing LVGL's 30 ms defaults.
- The SPD2010 poll gate dropped from 8 ms to 5 ms, and the main loop yields
  every 5 ms instead of 8.
- Launcher tiles tint gold while pressed, so a tap is acknowledged before
  the screen changes.

## Overlapping hit areas on the honeycomb

The hexagon sprites are rectangles, and neighbouring rectangles overlap at
their corners. A tap in that corner went to whichever tile happened to be
created last — the wrong app, roughly one time in eight near the seams.
Tiles now use `LV_OBJ_FLAG_ADV_HITTEST` with a point-in-hexagon test, so the
hit area *is* the hexagon you can see.

## One gesture for "back"

Every app screen now goes home on a **swipe down**, the way a sheet
dismisses on a phone. Lists consume vertical drags as scrolling before LVGL
looks for a gesture, so it never fires while you are scrolling Settings; on
every other screen it is the way back, and it is the same way back
everywhere. The HOME chip and the physical buttons still work; the clock,
which has no chip, now has a gesture that does not require knowing about
double-tap.

The toast moved from a bar at y = 306 — which sat on top of buttons — to a
centred HUD, like a phone's volume indicator. It is transient by design, so
overlapping content for a second is correct; sitting on a control was not.

---

# v6: taps that mean what you meant, and a build you can identify

## The watch went home by itself

Three independent causes, all fixed:

**Every tap counted towards double-tap.** The double/triple-tap recognizer
was fed every touch on the glass, including touches on buttons. Tap the
calculator's `1` twice, a Settings `+` twice, or the maze's reset twice
within about a third of a second, and the watch saw a double-tap and went
home. That was the main one.

Now the board layer asks the interface, *at the moment the finger lands*,
whether it landed on something pressable. If it did, that touch is a button
press and nothing else — it never reaches the gesture recognizer. The rule,
now enforced in one function (`luxor_ui_tap_is_background`):

| Taps | Where | Does |
|---|---|---|
| 1 | dark screen, anywhere | wakes it |
| 2 | empty background | home |
| 3 | empty background | off / on |

Deciding at touch-down rather than when the queue is drained matters: if the
first tap opened an app, the second tap is judged against the screen it
actually landed on, not whatever happens to be showing by then.

**A swipe-down-to-home gesture added in the previous build.** LVGL bubbles
gestures up from child objects, so a slightly downward drag while pressing
any button fired it. It was not asked for; it is removed.

**The PWR key had no debounce.** A single glitch on release read as a short
press, which means "home". It now has the same 40 ms debounce as BOOT, and it
is ignored until it has been seen released once after boot — the key is
usually still held from switching the watch on, and that press should not
count as anything.

`tests/test_gestures.c` now spells out the tap language: one tap does
nothing on a lit screen; two taps go home exactly once, only after the
triple-tap window has closed; three taps fire immediately and are never also
a double; taps in different places or too slowly never combine.

## The maze: back to plain marble physics

The previous build's vertical inversion is undone. The ball rolls towards
whichever edge of the watch is lower, and the first time the maze opens it
still calibrates that sign against the board (tilt the USB-C edge towards the
floor).

## Why earlier fixes may never have reached the watch

Each package so far had the same file name. A browser that already has
`Luxor_Rework_IDF5_5_5.zip` in Downloads saves the next one as
`Luxor_Rework_IDF5_5_5 (1).zip`, and `tar -xf Luxor_Rework_IDF5_5_5.zip`
quietly extracts the old one. From now on every package has its own name
(`Luxor_v6_IDF5_5_5.zip` → folder `Luxor_v6`), and **the version is on the
watch**: in the first line of the serial log, under **Settings → About Luxor**,
and in the About screen's first row. If the watch does not say `v6`, it is
not running this build.

## Smaller fixes

- **Display artwork no longer freezes the screen.** Two plates drew about
  1,400 separate three-pixel line segments; they are now polylines, a dozen
  draw calls in total.
- **About** now documents the tap language instead of an outdated summary.
