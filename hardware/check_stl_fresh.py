"""Fail if the committed STLs no longer match the OpenSCAD sources.

    ./render_all.sh /tmp/fresh && python3 check_stl_fresh.py /tmp/fresh

Compares geometry (volume, face count, bounds), not bytes: OpenSCAD's
output order can differ between runs with identical geometry.
"""
import glob, os, sys
import numpy as np
import trimesh

fresh = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fresh"
bad = 0
for f in sorted(glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "stl", "*.stl"))):
    n = os.path.basename(f)
    g = os.path.join(fresh, n)
    if not os.path.exists(g):
        print(f"MISSING {n} was not re-rendered"); bad += 1; continue
    a, b = trimesh.load(f), trimesh.load(g)
    same = (abs(a.volume - b.volume) < 1e-3 * max(1.0, abs(a.volume))
            and len(a.faces) == len(b.faces)
            and np.allclose(a.bounds, b.bounds, atol=1e-3))
    print(f"{'OK   ' if same else 'STALE'} {n:22s} {a.volume:9.1f} mm3  {len(a.faces)} faces")
    bad += 0 if same else 1
if bad:
    print(f"\n{bad} STL(s) out of date: run ./render_all.sh and commit stl/"); sys.exit(1)
print("\nALL STLs MATCH THE SOURCES")
