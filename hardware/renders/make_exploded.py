"""README exploded views (noir for light theme, ivory for dark theme).

    cd hardware && python3 renders/make_exploded.py ../docs/images
"""
import os, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HW, "..", "docs", "images")
tmp = os.environ.get("TMPDIR", "/tmp")
for finish in ("noir", "ivory"):
    raw = os.path.join(tmp, f"luxor_expl_{finish}.png")
    subprocess.run(["xvfb-run", "-a", "openscad", "-q", "-D", "explode=1", "-D", f'finish="{finish}"',
                    "--imgsize=1800,1800", "--viewall", "--autocenter", "--camera=0,0,0,62,0,28,0",
                    "--projection=p", "--colorscheme=Tomorrow", "-o", raw,
                    os.path.join(HW, "assembly.scad")], check=True)
    px = np.asarray(Image.open(raw).convert("RGB")).astype(int)
    obj = np.abs(px - px[3, 3]).sum(axis=2) > 18
    im = Image.fromarray(px.astype(np.uint8)).convert("RGBA")
    im.putalpha(Image.fromarray((obj * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)))
    im = im.resize((600, 600), Image.LANCZOS)
    im = im.crop(im.getbbox())
    im.save(os.path.join(out_dir, f"exploded_{finish}.png"))
    print("wrote", f"exploded_{finish}.png", im.size)
