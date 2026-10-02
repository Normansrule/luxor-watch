## Luxor v9: the open-source release

Everything needed to build the watch: firmware, printable case, bill of materials, pin-by-pin wiring, requirements and checks.

**Downloads**
- `bootloader.bin`, `partition-table.bin`, `luxor_v9.bin`: flash with esptool (see the README, step 2). `SHA256SUMS.txt` lists their hashes.
- `luxor-v9-stl.zip`: case shell, back cover, gold bezel inlay, charging dock and test coupons A, B and C.

**New in v9**
- Every control has a 44 px touch area. Small buttons keep their look and get an invisible padded hit area. A layout test and a simulator audit of all 11 screens in all 6 themes prove no two touch areas overlap.
- Manufacturing docs: [BOM](https://github.com/Normansrule/luxor-watch/blob/main/docs/BOM.md) (and `bom.csv`), [wiring and pin map](https://github.com/Normansrule/luxor-watch/blob/main/docs/WIRING.md), [requirements with verification](https://github.com/Normansrule/luxor-watch/blob/main/docs/REQUIREMENTS.md), [publishing guide](https://github.com/Normansrule/luxor-watch/blob/main/docs/PUBLISHING.md).
- CI checks that the STLs match the OpenSCAD sources, print without supports and fit together.

**Not yet verified on hardware.** Work through [HARDWARE_CHECKS](https://github.com/Normansrule/luxor-watch/blob/main/docs/HARDWARE_CHECKS.md) and print the coupons before the full case.
