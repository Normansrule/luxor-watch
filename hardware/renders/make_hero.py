"""README hero: the assembled watch rendered from the CAD with the real UI
frame on its glass.

Needs: openscad, xvfb-run, python3-pil, numpy, and simulator frames
(cd tools/ui_sim && make LVGL_DIR=~/lvgl gallery).

    cd hardware && python3 renders/make_hero.py ../docs/images/hero.png
"""
import os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)
FRAMES = os.path.join(HW, "..", "tools", "ui_sim", "frames")
TMP = os.environ.get("TMPDIR", "/tmp")

def frame(theme, app):
    a = np.fromfile(os.path.join(FRAMES, f"{theme}_{app}.raw"), dtype=np.uint16).reshape(412, 412)
    r = ((a >> 11) & 31) * 255 // 31; g = ((a >> 5) & 63) * 255 // 63; b = (a & 31) * 255 // 31
    return Image.fromarray(np.dstack([r, g, b]).astype(np.uint8))

def render(finish):
    out = os.path.join(TMP, f"luxor_hero_{finish}.png")
    subprocess.run(["xvfb-run", "-a", "openscad", "-q", "-D", f'finish="{finish}"',
                    "--projection=o", "--imgsize=1800,3000", "--camera=0,0,0,0,0,0,300",
                    "--colorscheme=Tomorrow", "-o", out, os.path.join(HW, "hero.scad")], check=True)
    return np.asarray(Image.open(out).convert("RGB")).astype(int)

def watch(finish, theme, app):
    px = render(finish)
    bg = px[5, 5]
    obj = np.abs(px - bg).sum(axis=2) > 18                       # everything that is not background
    key = (px[:, :, 2] > 150) & (px[:, :, 0] < 40) & (px[:, :, 1] < 40)   # the blue screen disc
    ys, xs = np.nonzero(key)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    d = int(round(max(xs.max() - xs.min(), ys.max() - ys.min()) + 4))
    im = Image.fromarray(px.astype(np.uint8)).convert("RGBA")
    # UI frame, circular, with a faint glass sheen
    ui = frame(theme, app).resize((d, d), Image.LANCZOS).convert("RGBA")
    m = Image.new("L", (d * 4, d * 4), 0)
    ImageDraw.Draw(m).ellipse((0, 0, d * 4 - 1, d * 4 - 1), fill=255)
    m = m.resize((d, d), Image.LANCZOS)
    sheen = Image.new("RGBA", (d, d), (255, 255, 255, 0))
    sd = ImageDraw.Draw(sheen)
    sd.ellipse((-d * 0.35, -d * 0.55, d * 0.95, d * 0.42), fill=(255, 255, 255, 16))
    ui = Image.alpha_composite(ui, sheen)
    ui.putalpha(m)
    im.alpha_composite(ui, (int(cx - d / 2), int(cy - d / 2)))
    # transparent background, anti-aliased by downsampling the mask
    alpha = Image.fromarray((obj * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3))
    im.putalpha(alpha)
    return im.resize((im.width // 3, im.height // 3), Image.LANCZOS)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HW, "..", "docs", "images", "hero.png")
    a = watch("noir", "noir", 0)       # home screen
    b = watch("ivory", "ivory", 5)     # dress dial
    gap = 40
    W = Image.new("RGBA", (a.width + b.width + gap, max(a.height, b.height)), (0, 0, 0, 0))
    W.alpha_composite(a, (0, 0)); W.alpha_composite(b, (a.width + gap, 0))
    bbox = W.getbbox(); W = W.crop(bbox)
    # keep the watch heads and a short length of strap
    keep = int(W.height * 0.64)
    top = (W.height - keep) // 2
    W = W.crop((0, top, W.width, top + keep))
    W.save(out)
    print("wrote", out, W.size)
