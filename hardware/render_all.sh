#!/usr/bin/env bash
# Re-render every printable STL from the OpenSCAD sources.
#   ./render_all.sh            writes stl/
#   ./render_all.sh /tmp/out   writes somewhere else (CI compares the two)
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:-stl}"
mkdir -p "$OUT"
for p in parts/0*.scad; do
    n=$(basename "$p" .scad)
    echo "render $n"
    openscad -q -o "$OUT/$n.stl" "$p"
done
for c in A B C; do
    echo "render coupon_$c"
    openscad -q -D "part=\"$c\"" -o "$OUT/coupon_$c.stl" coupons.scad
done
