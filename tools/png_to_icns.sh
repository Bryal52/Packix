#!/bin/sh
# png_to_icns.sh <master-1024.png> <out.icns> — every size macOS needs, from one 1024 px picture.
set -e
MASTER="$1"
OUT="$2"
WORK="${OUT%.icns}.iconset"
rm -rf "$WORK"
mkdir -p "$WORK"
for s in 16 32 128 256 512; do
  sips -z $s $s "$MASTER" --out "$WORK/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z $d $d "$MASTER" --out "$WORK/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$WORK" -o "$OUT"
rm -rf "$WORK"
