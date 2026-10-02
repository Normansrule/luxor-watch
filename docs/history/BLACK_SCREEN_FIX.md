# Luxor black-screen boot fix

This revision changes boot order so the display and Luxor UI are initialized and flushed before IMU, RTC, audio, Wi-Fi, or BLE. Optional subsystem failures no longer prevent the home screen from appearing. INFO logging is enabled and display initialization emits milestone messages.

Expected monitor sequence includes:
- `1/7 board init`
- `2/7 display + touch init`
- `display: panel init complete`
- `3/7 creating Luxor UI`
- `Luxor first frame submitted`
- `=== Luxor runtime started ===`

If the screen is still black, run `idf.py -p <your COM port> monitor`, reset the board, and capture the first error after `=== Luxor boot ===`.
