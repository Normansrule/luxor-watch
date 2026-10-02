"""Build the README screen images from simulator frames.

    make LVGL_DIR=~/lvgl gallery
    python3 readme_images.py ../../docs/images

Writes screens_strip.png (four hero screens on a transparent background, so
it reads on GitHub's light and dark themes) and the two full galleries.
"""
import sys, os, shutil
import numpy as np
from PIL import Image, ImageDraw

def load(fn, size=412):
    a = np.fromfile(fn, dtype=np.uint16).reshape(412, 412)
    r = ((a >> 11) & 31) * 255 // 31
    g = ((a >> 5) & 63) * 255 // 63
    b = (a & 31) * 255 // 31
    im = Image.fromarray(np.dstack([r, g, b]).astype(np.uint8)).convert("RGBA")
    # 4x supersampled circular mask for a clean edge
    m = Image.new("L", (412 * 4, 412 * 4), 0)
    ImageDraw.Draw(m).ellipse((0, 0, 412 * 4 - 1, 412 * 4 - 1), fill=255)
    im.putalpha(m.resize((412, 412), Image.LANCZOS))
    return im.resize((size, size), Image.LANCZOS) if size != 412 else im

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)
# home, clock, steps, music  (app ids: 0 home, 5 clock, 7 steps, 3 music)
picks = [("noir", 0), ("noir", 5), ("noir", 7), ("ivory", 3)]
size, gap = 300, 24
strip = Image.new("RGBA", (len(picks) * size + (len(picks) - 1) * gap, size), (0, 0, 0, 0))
for i, (theme, app) in enumerate(picks):
    strip.alpha_composite(load(f"frames/{theme}_{app}.raw", size), (i * (size + gap), 0))
strip.save(os.path.join(out, "screens_strip.png"))
for t in ("ivory", "noir"):
    shutil.copy(f"gallery_{t}.png", os.path.join(out, f"screens_{t}.png"))
print("wrote", out)
