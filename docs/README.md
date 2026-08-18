# docs/ — the presentation

`Thorium.pptx` and `Thorium.html` are the same 25-slide deck, built in Knowit
Connectivity's brand from one source outline.

**Audience: the people who will write and run the tests.** The deck is in two
acts, and the order carries the argument:

1. **What a test writer gets** — the three files they actually edit, what a
   script never has to mention, what the compiler tells them when they get it
   wrong, working without a bench (inject / record / replay), the three
   tolerance tables, running a selection, and the evidence a run leaves behind.
   Nothing in this act requires knowing how the framework is built.
2. **The mechanisms that make that possible** — the layering, routes composed
   rather than stored, a test point carrying its location in its *type*,
   reflection instead of parallel lists, the test files that exist only to
   compile, and an honest slide on what is still a runtime check and why.

It closes with a pros-and-cons slide and a status slide, both deliberately
unflattering where that is the truth.

- **`Thorium.pptx`** — open or edit in PowerPoint. Cloned from the official Knowit
  template, so it keeps the embedded Bagoss font, the aurora gradients, the
  wordmark and all 40 layouts.
- **`Thorium.html`** — present from a browser; arrow keys or click to advance.
  One self-contained file with the brand assets base64-embedded, so it works
  offline and can be sent as a single attachment.

## Rebuilding

Both come from `src/outline.py` — edit the content there, then:

```bash
cd docs/src
cp ../Knowit_template.pptx template.pptx && rm -rf unpacked && mkdir unpacked
python3 -c "import zipfile; zipfile.ZipFile('template.pptx').extractall('unpacked')"
python3 build.py                                  # -> Thorium.pptx
SK=<knowit-presentation-skill-dir> python3 build_html.py    # -> Thorium.html
python3 check_sync.py                             # both agree with the outline?
cp Thorium.pptx Thorium.html ..                   # install over the committed pair
```

**Run both writers, then `check_sync.py`, before installing.** The two artifacts
are produced by separate scripts, so rebuilding one and forgetting the other
leaves a PowerPoint and a browser deck whose *wording differs* — which is the one
thing keeping a single outline is supposed to prevent. That has happened; the
check exists so it cannot happen quietly. It compares every string in
`outline.py` against both artifacts, unescaping first because the two writers
escape differently.

`build.py` needs `template.pptx` and an `unpacked/` copy of it beside it — it
edits the unpacked XML and repacks, so it depends on neither python-pptx nor any
other library. Both, plus the built pair in `src/`, are git-ignored; the deck
that ships is the one at `docs/`.

Keeping one outline for both is the point: a slide's wording cannot differ
between the PowerPoint a stakeholder reads and the deck presented from a browser.

### If the brand skill is not installed

`build_html.py` only wants three PNGs from it — `gradient_warm.png`,
`gradient_cool.png`, `knowit_asterisk.png`. They are already base64-embedded in
the committed `Thorium.html`, so they can be recovered from it and `SK` pointed
at the result:

```bash
cd docs/src && mkdir -p brand_assets/assets && python3 - <<'PY'
import re, base64
html = open('../Thorium.html').read()
blobs = list(dict.fromkeys(re.findall(r"data:image/png;base64,([A-Za-z0-9+/=]+)", html)))
warm = re.findall(r"--bg-light\) url\('data:image/png;base64,([A-Za-z0-9+/=]+)'\) center/cover", html)[0]
mark = re.findall(r"background:url\('data:image/png;base64,([A-Za-z0-9+/=]+)'\) center/contain", html)[0]
cool = next(b for b in blobs if b not in (warm, mark))
for name, blob in (('gradient_warm.png', warm), ('gradient_cool.png', cool), ('knowit_asterisk.png', mark)):
    open(f'brand_assets/assets/{name}', 'wb').write(base64.b64decode(blob))
PY
SK=brand_assets python3 build_html.py
```

Identified by the CSS context each one appears in rather than by position, since
two of the three are used the same way and would otherwise be indistinguishable.
Verified by rebuilding the committed deck from a clean checkout and diffing: the
HTML came out byte-identical and every `.pptx` slide part matched.

## Visual QA

LibreOffice ignores `PageRange` for PNG export, so `src/render.sh` splits the deck
into one single-slide `.pptx` per slide (rewriting `sldIdLst`, which is why it can
be done from the zip alone) and renders each. That is how every slide was
inspected before this was presented.

```bash
cd docs/src && SOFFICE=/Applications/LibreOffice.app/Contents/MacOS/soffice ./render.sh Thorium.pptx
# -> shots/slide01.png … slide25.png
```

Two caveats when reading those renders:

- **The font is wrong.** LibreOffice cannot use the deck's embedded Bagoss
  (`EOT out of spec` on load), so everything falls back to a serif. Layout,
  wrapping and overflow are trustworthy; typography is not.
- **Gradients are missing** on the title slide. In PowerPoint it is the warm
  aurora background. The Conclusion slide's navy is *not* a missing gradient —
  `slideLayout17` has no background override, so navy is the template's own
  intent.

What the renders are actually for is overflow and wrapping, and they earn their
keep: the "what happens when you get it wrong" slide is read as *rows* across two
columns, and the first draft had a right-hand entry wrap to two lines, which
silently pushed every pair below it out of alignment. Nothing about the outline
source hinted at it.

One thing worth knowing if you edit `src/build.py`: `layouts.md` in the brand
skill has the Quote layout's two placeholders the wrong way round for this
template. Verified against `slideLayout19.xml`: **idx15** is the 32pt quote,
**idx14** the 10pt all-caps attribution. Same class of surprise on the Conclusion
layout, where `title` is a 12pt corner label rather than the headline — the
headline belongs in the content placeholder, whose first paragraph is already
32pt.
