# docs/ — the presentation

`Thorium.pptx` and `Thorium.html` are the same 18-slide deck, built in Knowit
Connectivity's brand from one source outline. Audience: testers, developers and
stakeholders, with an explicit pros-and-cons slide.

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
python3 build.py                     # -> Thorium.pptx  (needs Knowit_template.pptx alongside)
SK=<knowit-presentation-skill-dir> python3 build_html.py    # -> Thorium.html
```

Keeping one outline for both is the point: a slide's wording cannot differ
between the PowerPoint a stakeholder reads and the deck presented from a browser.

## Visual QA

LibreOffice ignores `PageRange` for PNG export, so `src/render.sh` splits the deck
into one single-slide `.pptx` per slide (rewriting `sldIdLst`, which is why it can
be done from the zip alone) and renders each. That is how every slide was
inspected before this was presented.

```bash
cd docs/src && ./render.sh        # -> shots/slide01.png … slide18.png
```

Two caveats when reading those renders:

- **The font is wrong.** LibreOffice cannot use the deck's embedded Bagoss
  (`EOT out of spec` on load), so everything falls back to a serif. Layout,
  wrapping and overflow are trustworthy; typography is not.
- **Gradients are missing** on the title slide. In PowerPoint it is the warm
  aurora background. The Conclusion slide's navy is *not* a missing gradient —
  `slideLayout17` has no background override, so navy is the template's own
  intent.

One thing worth knowing if you edit `src/build.py`: `layouts.md` in the brand
skill has the Quote layout's two placeholders the wrong way round for this
template. Verified against `slideLayout19.xml`: **idx15** is the 32pt quote,
**idx14** the 10pt all-caps attribution. Same class of surprise on the Conclusion
layout, where `title` is a 12pt corner label rather than the headline — the
headline belongs in the content placeholder, whose first paragraph is already
32pt.
