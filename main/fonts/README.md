# Fonts

Inter 4.1 (SIL Open Font License 1.1 — `Inter-LICENSE.txt`), converted for LVGL 8.3 by `lv_font_conv`, 4 bpp, with LVGL's FontAwesome symbol glyphs merged in so `LV_SYMBOL_*` still render.

| File | Face | Size | Used for |
|---|---|---|---|
| lux_inter_14.c | Inter Medium | 14 | body text, captions, battery badge |
| lux_inter_16.c | Inter Medium | 16 | list titles |
| lux_inter_20.c | Inter Medium | 20 | screen titles, stats, keys |
| lux_inter_28.c | Inter Display Medium | 28 | time, splash wordmark |
| lux_inter_48.c | Inter Display Light | 48 | calculator display |

Character set: ASCII plus ° ± · × ÷ – — • … − (see `generate_fonts.sh`). The UI simulator's `make check` fails if a label uses a character a font lacks.
