#!/usr/bin/env python3
"""Printability check for the Luxor STLs (in their exported print orientation).

For every part: watertight single body, sits on z = 0, fits a 180 mm bed,
and lists every down-facing region steeper than 45 degrees that is not on
the bed.  Each region is reported with its size, so you can tell a short
bridge (fine) from a real overhang (needs support or a redesign).

    pip install trimesh numpy
    python3 check_print.py            # checks stl/*.stl
"""
import glob, os, sys
import numpy as np
import trimesh

os.chdir(os.path.dirname(os.path.abspath(__file__)))   # works from any folder

BED = 180.0          # mm, Bambu A1 mini / most small printers
MAX_BRIDGE = 5.0     # mm: a down-facing strip narrower than this bridges cleanly

bad = 0
stls = sorted(glob.glob("stl/*.stl"))
if not stls:
    sys.exit("no STLs found in hardware/stl - run ./render_all.sh")
for f in stls:
    m = trimesh.load(f)
    name = f.split("/")[-1]
    ext = m.bounds[1] - m.bounds[0]
    issues = []
    if not m.is_watertight:
        issues.append("not watertight")
    if len(m.split(only_watertight=False)) != 1:
        issues.append("more than one body")
    if abs(m.bounds[0][2]) > 0.01:
        issues.append(f"not on the bed (zmin {m.bounds[0][2]:.2f})")
    if max(ext[0], ext[1]) > BED:
        issues.append("larger than the bed")
    # down-facing faces steeper than 45°, above the first layer
    n = m.face_normals
    c = m.triangles_center
    sel = np.where((n[:, 2] < -0.7071) & (c[:, 2] > 0.25))[0]
    regions = []
    if len(sel):
        sub = m.submesh([sel], append=True)
        for part in sub.split(only_watertight=False):
            area = part.area
            if area < 0.5:
                continue
            # The width a bridge has to span.  For a strip or a ring,
            # 2 * area / perimeter is exactly its width; for a square it is
            # half the side, so MAX_BRIDGE is set conservatively.
            try:
                perim = sum(e.length(part.outline().vertices) for e in part.outline().entities)
            except Exception:
                perim = 0.0
            span = 2.0 * area / perim if perim > 0 else float("inf")
            regions.append((area, span, part.bounds[0][2]))
    worst = [r for r in regions if r[1] > MAX_BRIDGE]
    status = "OK " if not issues and not worst else "CHECK"
    if issues or worst:
        bad += 1
    print(f"{status} {name:24s} {ext[0]:6.1f} x {ext[1]:6.1f} x {ext[2]:5.1f} mm  "
          f"vol {m.volume/1000:6.2f} cm3  down-facing patches {len(regions)}")
    for i in issues:
        print(f"      !! {i}")
    for a, s, z in sorted(worst, reverse=True)[:5]:
        print(f"      overhang {a:6.1f} mm2 spanning {s:4.1f} mm at z {z:4.1f}")
sys.exit(1 if bad else 0)
