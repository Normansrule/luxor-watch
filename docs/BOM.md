# Bill of materials

Machine-readable copy: [`bom.csv`](bom.csv). Prices are rough street prices in USD, not quotes.

> **Buy the cover-glass board.** Waveshare sells three variants on [one product page](https://www.waveshare.com/esp32-s3-touch-lcd-1.46b.htm). The case's front lip holds the **cover glass**, so order the variant *with protective cover glass*. Waveshare's store names that one **ESP32-S3-Touch-LCD-1.46**, and the **1.46B** is listed *without* glass. All three run the same firmware.

## Buy

| | Item | Qty | Spec | Est. |
|---|---|:-:|---|--:|
| 🟡 B1 | Waveshare ESP32-S3-Touch-LCD-1.46, **cover-glass variant** | 1 | the whole watch: ESP32-S3, display, touch, IMU, RTC, audio, charger | $22–27 |
| 🔴 B2 | Single-cell LiPo with protection circuit | 1 | **402530** · 4.0 × 25 × 30 mm · ~250 mAh · **MX1.25 2-pin** | $6–10 |
| 🟢 B3 | USB-C **data** cable, slim overmould | 1 | overmould ≤ 12.5 × 7 mm | $6–10 |
| ⚙️ H1 | M2 × 6 screw, pan or socket head | 3 | stainless; the render prints the exact length | <$1 |
| ⌚ H2 | 22 mm quick-release strap | 1 | 1.5 mm spring-bar body | $10–25 |
| ⌚ H3 | 22 mm spring bars (spares) | 2 | 1.5 mm body, ~0.8 mm tips | $1 |
| ▫️ H4 | Gasket ring | 1 | 0.3 mm closed-cell foam or EPDM, cut 41 / 45.5 mm | $5–8 |
| ▫️ H5 | Kapton tape | 1 | 10–20 mm | $5–8 |
| ▫️ H6 | Thin CA glue (or 0.1 mm double-sided tape) | 1 | fixes the gold inlay | $4–6 |
| | *Optional:* microSD card · 4 × M8 washers (dock ballast) | | | $5–10 |

**About $60–100 for one watch**, plus filament. Most consumables (tape, glue, screws) come in packs that cover many builds.

## Print

| | Part | File | Material | Weight |
|---|---|---|---|--:|
| P1 | Case shell | [`01_case_shell.stl`](../hardware/stl/01_case_shell.stl) | PLA+ / PETG, matte black or ivory | ~6 g |
| P2 | Back cover | [`02_back_cover.stl`](../hardware/stl/02_back_cover.stl) | same | ~5 g |
| P3 | Bezel inlay | [`03_bezel_ring.stl`](../hardware/stl/03_bezel_ring.stl) | silk gold PLA | <1 g |
| P4 | Charging dock | [`04_charging_dock.stl`](../hardware/stl/04_charging_dock.stl) | any PLA | ~70 g |
| P5 | Coupons A, B, C — **print first** | [`coupon_A`](../hardware/stl/coupon_A.stl) · [`B`](../hardware/stl/coupon_B.stl) · [`C`](../hardware/stl/coupon_C.stl) | as the real parts | ~8 g |

Print settings, orientation and the fit-check order are in [`hardware/README.md`](../hardware/README.md).

## Tools

FDM 3D printer with a 0.4 mm nozzle · PC with USB (Windows, Linux or macOS) · multimeter · 1.1 mm drill bit in a pin vise · small hex/Phillips driver for M2 · flush cutters · tweezers · calipers (for the MEASURE values).

## Order gates

Two items can damage the watch if they're wrong, so check these before you order:

| Item | Confirm first | How |
|---|---|---|
| B2 battery | header **polarity** and cell size with wrap ≤ 30.5 × 25.5 × 4.2 mm | photo of the board's +/− silkscreen; multimeter on the plug ([wiring](WIRING.md#the-one-connection-you-make-the-battery)) |
| H1 screws | standoff **thread depth** (`m2_engage`) | calipers; the render prints the screw length |
