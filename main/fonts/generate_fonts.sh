#!/bin/bash
# Regenerates the Luxor Inter fonts for LVGL 8.3.
#   npm i -g lv_font_conv
#   Inter 4.1:   https://github.com/rsms/inter/releases (unzip; point INTER at extras/ttf)
#   FontAwesome: lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
# Usage: INTER=~/inter/extras/ttf FA=~/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff ./generate_fonts.sh
set -e
cd "$(dirname "$0")"
: "${INTER:?set INTER to the Inter extras/ttf folder}"
: "${FA:?set FA to the LVGL FontAwesome woff}"
# LVGL's LV_SYMBOL_* code points (same list LVGL's built-in fonts use).
SYMS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
# ASCII plus: degree, plus-minus, middle dot, times, divide, en/em dash, bullet, ellipsis, minus.
R="0x20-0x7E,0xB0,0xB1,0xB7,0xD7,0xF7,0x2013,0x2014,0x2022,0x2026,0x2212"
gen() {
  local name=$1 size=$2 ttf=$3 sym=$4
  local extra=()
  [ "$sym" = 1 ] && extra=(--font "$FA" -r "$SYMS")
  lv_font_conv --bpp 4 --size "$size" --font "$INTER/$ttf" -r "$R" "${extra[@]}" \
      --format lvgl --no-compress --force-fast-kern-format --lv-font-name "$name" -o "$name.c" >/dev/null
  sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' "$name.c"
  echo "  $name.c  ($ttf $size px)"
}
gen lux_inter_14 14 Inter-Medium.ttf        1
gen lux_inter_16 16 Inter-Medium.ttf        1
gen lux_inter_20 20 Inter-Medium.ttf        1
gen lux_inter_28 28 InterDisplay-Medium.ttf 1
gen lux_inter_48 48 InterDisplay-Light.ttf  0
