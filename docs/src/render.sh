#!/usr/bin/env bash
#
# Render every slide of Thorium.pptx to shots/slideNN.png for visual QA.
#
# LibreOffice ignores the PNG filter's PageRange option (verified: asking for
# page 1 and page 16 yields byte-identical output), so the deck is split into one
# single-slide .pptx per slide first. That split only needs the zip: rewriting
# <p:sldIdLst> to hold one slide id leaves every layout, master and font part in
# place, so each fragment renders exactly as that slide does in the full deck.
#
set -eu
cd "$(dirname "$0")"

DECK=${1:-../Thorium.pptx}
SOFFICE=${SOFFICE:-soffice}

rm -rf single shots && mkdir -p single shots

python3 - "$DECK" <<'PY'
import re, sys, zipfile
# Read everything up front: handing a source ZipInfo straight to writestr()
# mutates it with the *new* archive's offsets, corrupting later reads.
with zipfile.ZipFile(sys.argv[1]) as src:
    data = {n: src.read(n) for n in src.namelist()}
pres = data['ppt/presentation.xml'].decode()
n = len(re.findall(r'r:id="rSlide(\d+)"', pres))
for i in range(1, n + 1):
    only = f'<p:sldIdLst><p:sldId id="{600+i}" r:id="rSlide{i}"/></p:sldIdLst>'
    one  = re.sub(r'<p:sldIdLst>.*?</p:sldIdLst>', only, pres, flags=re.S).encode()
    with zipfile.ZipFile(f'single/s{i:02d}.pptx', 'w', zipfile.ZIP_DEFLATED) as z:
        for name, blob in data.items():
            z.writestr(name, one if name == 'ppt/presentation.xml' else blob)
print(n)
PY

for f in single/s*.pptx; do
    i=$(basename "$f" .pptx | tr -d 's')
    "$SOFFICE" --headless --norestore \
      --convert-to 'png:impress_png_Export:{"PixelWidth":{"type":"long","value":1600},"PixelHeight":{"type":"long","value":900}}' \
      --outdir shots "$f" >/dev/null 2>&1
    mv "shots/s$i.png" "shots/slide$i.png"
done

echo "rendered $(ls shots/*.png | wc -l | tr -d ' ') slides to $(pwd)/shots"
