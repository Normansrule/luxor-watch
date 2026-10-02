"""Generate docs/images/wiring.svg — the Luxor system and pin map.

    python3 make_wiring_svg.py            (writes wiring.svg next to this file)

Every pin here comes from main/board.c and main/audio.c.  Edit PINS below,
re-run, commit the SVG.  The drawing sits on its own white card so it reads
on GitHub's light and dark themes.
"""
import os

# bus colours
C = {
    "qspi": "#B8860B",   # gold
    "i2c":  "#2F6FB5",   # blue
    "i2s":  "#B5527A",   # rose
    "gpio": "#4A4A4A",   # graphite
    "pwr":  "#C0392B",   # red
    "usb":  "#2E8B57",   # green
}

# (group title, bus, [(gpio, signal)], chips)
GROUPS = [
    ("Display  SPD2010  412x412", "qspi",
     [("40", "QSPI SCK"), ("46", "QSPI D0"), ("45", "QSPI D1"), ("42", "QSPI D2"),
      ("41", "QSPI D3"), ("21", "QSPI CS")], "40 MHz"),
    ("I2C bus  (400 kHz)", "i2c",
     [("11", "SDA"), ("10", "SCL")],
     "0x53 touch SPD2010 · 0x6A or 0x6B IMU QMI8658 · 0x51 RTC PCF85063 · 0x20 IO expander TCA9554|TCA9554: EXIO0 + EXIO1 = LCD and touch reset (pulsed together at boot) · EXIO2 = microSD chip select"),
    ("Speaker  I2S out → PCM5101 DAC → amplifier", "i2s",
     [("48", "BCLK"), ("38", "WS"), ("47", "DOUT")], ""),
    ("Microphone  I2S in", "i2s",
     [("15", "BCLK"), ("2", "WS"), ("39", "DIN")], ""),
    ("Controls and power", "gpio",
     [("6", "PWR key (low = pressed)"), ("0", "BOOT key (low = pressed)"),
      ("7", "Power hold (high = stay on)"), ("5", "Backlight PWM (LEDC)"),
      ("8", "Battery sense ADC1 ch7, 3:1 divider")], ""),
    ("USB  (native Serial/JTAG)", "usb",
     [("19", "USB D-"), ("20", "USB D+")], "VID:PID 303a:1001"),
]

W, H = 1200, 860
out = []
a = out.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
  'font-family="Inter, Segoe UI, Helvetica, Arial, sans-serif">')
a(f'<rect x="0" y="0" width="{W}" height="{H}" rx="24" fill="#FFFFFF"/>')
a('<text x="40" y="58" font-size="30" font-weight="700" fill="#0A0A0A">Luxor — system and pin map</text>')
a('<text x="40" y="88" font-size="16" fill="#5C5648">Everything inside the dashed line is already on the Waveshare module. '
  'The only connection you make is the battery plug.</text>')

# module outline
module_idx = len(out)
a("")  # module outline, filled in once the content height is known
a('<text x="50" y="138" font-size="14" font-weight="600" fill="#A07C14" letter-spacing="1.5">'
  'WAVESHARE ESP32-S3-TOUCH-LCD-1.46 MODULE</text>')

sx, sw, sy = 70, 250, 160
# groups on the right, each wired to the SoC edge
gx, gw = 430, 700
y = 160
for title, bus, pins, note in GROUPS:
    col = C[bus]
    rows = (len(pins) + 1) // 2
    nlines = len(note.split('|')) if note else 0
    gh = 44 + rows * 26 + 22 * nlines
    a(f'<rect x="{gx}" y="{y}" width="{gw}" height="{gh}" rx="12" fill="{col}" fill-opacity="0.07" '
      f'stroke="{col}" stroke-width="1.5"/>')
    a(f'<text x="{gx+16}" y="{y+26}" font-size="16" font-weight="700" fill="{col}">{title}</text>')
    for k, (g, sig) in enumerate(pins):
        cx = gx + 16 + (k % 2) * 340
        cy = y + 52 + (k // 2) * 26
        a(f'<rect x="{cx}" y="{cy-16}" width="68" height="22" rx="11" fill="{col}"/>')
        a(f'<text x="{cx+34}" y="{cy}" font-size="13" font-weight="700" fill="#FFFFFF" text-anchor="middle">GPIO{g}</text>')
        a(f'<text x="{cx+78}" y="{cy}" font-size="14" fill="#0A0A0A">{sig}</text>')
    for j, line in enumerate(note.split('|') if note else []):
        a(f'<text x="{gx+16}" y="{y+gh-12-22*(nlines-1-j)}" font-size="13" fill="#5C5648">{line}</text>')
    # connector line SoC -> group
    ly = y + gh / 2
    a(f'<path d="M{sx+sw} {ly} H{gx}" stroke="{col}" stroke-width="3" fill="none"/>')
    a(f'<circle cx="{sx+sw}" cy="{ly}" r="5" fill="{col}"/>')
    y += gh + 10

# SoC block
sh = y - 10 - sy
a(f'<rect x="{sx}" y="{sy}" width="{sw}" height="{sh}" rx="18" fill="#0A0A0A"/>')
a(f'<text x="{sx+sw/2}" y="{sy+46}" font-size="22" font-weight="700" fill="#D4AF37" text-anchor="middle">ESP32-S3</text>')
for i, t in enumerate(["dual-core LX7 · 240 MHz", "16 MB flash · 8 MB PSRAM",
                       "Wi-Fi 2.4 GHz · Bluetooth LE", "firmware: Luxor"]):
    a(f'<text x="{sx+sw/2}" y="{sy+80+i*24}" font-size="14" fill="#F2EEE3" text-anchor="middle">{t}</text>')


legend = [("qspi", "QSPI — display"), ("i2c", "I2C — sensors, touch"), ("i2s", "I2S — audio"),
          ("gpio", "GPIO / ADC / PWM"), ("usb", "USB")]
for i, (k, t) in enumerate(legend):
    ly2 = sy + sh - 30 - (len(legend) - 1 - i) * 28
    a(f'<circle cx="{sx+28}" cy="{ly2-5}" r="7" fill="{C[k]}"/>')
    a(f'<text x="{sx+44}" y="{ly2}" font-size="14" fill="#F2EEE3">{t}</text>')

# external connections
mod_bottom = y + 20
out[module_idx] = (f'<rect x="30" y="110" width="1140" height="{mod_bottom - 110}" rx="22" fill="none" '
                   'stroke="#A07C14" stroke-width="2" stroke-dasharray="8 6"/>')
ey = mod_bottom + 40
a(f'<text x="40" y="{ey}" font-size="18" font-weight="700" fill="#0A0A0A">You connect</text>')
# battery
bx = 40
a(f'<rect x="{bx}" y="{ey+16}" width="570" height="124" rx="14" fill="{C["pwr"]}" fill-opacity="0.07" stroke="{C["pwr"]}" stroke-width="1.5"/>')
a(f'<text x="{bx+16}" y="{ey+44}" font-size="16" font-weight="700" fill="{C["pwr"]}">LiPo cell → board battery header (MX1.25, 2-pin)</text>')
a(f'<text x="{bx+16}" y="{ey+70}" font-size="14" fill="#0A0A0A">Red wire → header pin marked +     ·     Black wire → pin marked −</text>')
a(f'<text x="{bx+16}" y="{ey+94}" font-size="14" fill="#0A0A0A">Single cell, 3.7 V nominal, 4.2 V full. Charged on board from USB-C.</text>')
a(f'<text x="{bx+16}" y="{ey+118}" font-size="14" font-weight="700" fill="{C["pwr"]}">Check polarity with a meter first: seller plugs are often reversed.</text>')
# usb
ux = 630
a(f'<rect x="{ux}" y="{ey+16}" width="530" height="124" rx="14" fill="{C["usb"]}" fill-opacity="0.07" stroke="{C["usb"]}" stroke-width="1.5"/>')
a(f'<text x="{ux+16}" y="{ey+44}" font-size="16" font-weight="700" fill="{C["usb"]}">USB-C data cable → PC  (flash, serial log, charge)</text>')
a(f'<text x="{ux+16}" y="{ey+70}" font-size="14" fill="#0A0A0A">Enumerates as a COM port (Windows) or /dev/ttyACM0 (Linux).</text>')
a(f'<text x="{ux+16}" y="{ey+94}" font-size="14" fill="#0A0A0A">Must be a DATA cable: charge-only cables flash nothing.</text>')
a(f'<text x="{ux+16}" y="{ey+118}" font-size="14" fill="#0A0A0A">Overmould up to 12.5 x 7 mm fits the case mouth as modelled.</text>')
H = ey + 160
out[0] = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
          'font-family="Inter, Segoe UI, Helvetica, Arial, sans-serif">')
out[1] = f'<rect x="0" y="0" width="{W}" height="{H}" rx="24" fill="#FFFFFF"/>'
a('</svg>')

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wiring.svg")
with open(path, "w") as f:
    f.write("\n".join(out))
print("wrote", path)
