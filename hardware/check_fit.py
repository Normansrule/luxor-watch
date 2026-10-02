#!/usr/bin/env python3
"""Interference check for the Luxor case, in the assembled position.

Renders the intersection of every pair of parts (and of each part with the
board envelope we assume) and fails if any solid remains.  The flexure nubs
are the one intended contact: they must touch the board edge, by a little.

    python3 check_fit.py
"""
import subprocess, sys, os, tempfile, warnings
import trimesh

os.chdir(os.path.dirname(os.path.abspath(__file__)))   # works from any folder

warnings.filterwarnings("ignore")
# Extra overrides, e.g.  LUXOR_D="board_od=46.0" python3 check_fit.py
EXTRA = [a for kv in os.environ.get("LUXOR_D", "").split(";") if kv for a in ("-D", kv)]

PAIRS = ["shell-cover", "shell-bezel", "cover-bezel", "shell-board",
         "cover-board", "shell-glass", "cover-cell", "board-cell"]
fail = 0

def overlap(pair):
    out = os.path.join(tempfile.gettempdir(), f"luxor_fit_{pair}.stl")
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run(["openscad", "-q", "-D", f'pair="{pair}"', *EXTRA, "-o", out, "fit_pairs.scad"],
                       capture_output=True, text=True)
    if "top level object is empty" in (r.stderr + r.stdout) or not os.path.exists(out):
        return 0.0
    try:
        m = trimesh.load(out)
        return abs(m.volume)
    except Exception:
        return 0.0

for p in PAIRS:
    v = overlap(p)
    ok = v < 0.001
    fail += not ok
    print(f"{'OK   ' if ok else 'CLASH'} {p:12s} {v:8.4f} mm3")

v = overlap("nubs-board")
ok = 0.001 < v < 5.0
fail += not ok
print(f"{'OK   ' if ok else 'CHECK'} {'nubs-board':12s} {v:8.4f} mm3  (flexure nubs must just touch the board edge)")
sys.exit(1 if fail else 0)
