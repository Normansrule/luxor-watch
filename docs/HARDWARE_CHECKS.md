# Hardware checks — v9

Flash, then work down the list. Report the first thing that fails.

| # | Check | Pass |
|---|---|---|
| 1 | Serial monitor first line | `=== Luxor v9 boot ===` |
| 2 | Boot | LUXOR wordmark on black for about 1 s, then home fades in |
| 3 | Settings → About | Firmware `Luxor v9`; Battery row shows a voltage |
| 4 | Text | Inter everywhere; no clipped words; the home and settings icons still show |
| 5 | Tap a hexagon | It tints gold and sinks slightly while held; the app fades in |
| 6 | Tap any button | It settles a pixel and springs back; no lag |
| 7 | STEPS | Three rings (gold, rose, platinum) and three numbers under them |
| 8 | MUSIC, press play | The five bars move; tap pause, they rest |
| 9 | CALC, type 12345678 × 9 = | × shows as a real ×; the long result drops to the smaller font, no clipping |
| 10 | Home, top right | Battery % and glyph, clear of the bezel and the hexagons |
| 11 | Carried over: tilt after calibration | The marble rolls towards the edge you lower |
| 12 | Carried over: leave the watch on home for 2 min | It does not change screen by itself |
| 13 | Carried over: Noir Gold theme | No horizontal band across the background |
| 14 | Clock | The second hand still sweeps smoothly |
| 15 | Touch areas (new in v9) | On CALC, MOTION, MAZE and TIME, a tap just beside a small button still presses it; a tap in open space still counts for the double-tap home |

If a fade or the music meter stutters on the watch, tell me the screen. `LUX_FADE_MS` in `ui.c` can be cut to 0 to disable the fades.

With a cell connected later: compare the About voltage against a multimeter across the cell. If they differ by more than ~30 mV, set `CONFIG_RW_ADC_SCALE_PERMILLE = 1000 × meter / About` (menuconfig → LUXOR) and rebuild.

Until the charge current is verified for your cell, leave the battery unplugged while USB is connected.

## Case checks

See [`hardware/README.md`](../hardware/README.md) → *Order of operations* (measure → coupons B, A, C → parts → [`hardware/ASSEMBLY.md`](../hardware/ASSEMBLY.md)).
