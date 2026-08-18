#!/usr/bin/env python3
"""Both artifacts must carry the same wording -- the whole point of one outline.

Built separately by build.py and build_html.py, so rebuilding one and forgetting
the other silently produces a PowerPoint and a browser deck that disagree. That
happened once; this is how it stops being possible to miss.
"""
import html as htmllib
import re, sys, zipfile
from outline import DECK

def texts(node):
    """Every plain string in the outline, flattened."""
    for layout, fields in DECK:
        for content in fields.values():
            if isinstance(content, str):
                yield from content.split('\n')
            else:
                for _lvl, runs, _style in content:
                    for text, _b, _a in runs:
                        if text.strip():
                            yield text

# Unescaped first: both writers escape, and differently. The HTML writer turns
# an apostrophe into &#x27;, whose digits survive the alphanumeric normalisation
# below and make every string containing one look absent.
page = htmllib.unescape(open('Thorium.html', encoding='utf-8').read())
z = zipfile.ZipFile('Thorium.pptx')
deck = htmllib.unescape(''.join(z.read(n).decode('utf-8', 'replace')
                                for n in z.namelist() if n.startswith('ppt/slides/slide')))

missing = []
for t in texts(DECK):
    # compare on the visible characters only; both writers escape differently
    key = re.sub(r'[^A-Za-z0-9]', '', t)
    if not key:
        continue
    if key not in re.sub(r'[^A-Za-z0-9]', '', page):
        missing.append(('html', t))
    if key not in re.sub(r'[^A-Za-z0-9]', '', deck):
        missing.append(('pptx', t))

if missing:
    for where, t in missing[:20]:
        print(f"MISSING from {where}: {t[:70]}")
    sys.exit(f"{len(missing)} outline strings absent -- rebuild both artifacts")
print(f"both artifacts carry every string in the outline ({len(DECK)} slides)")
