# -*- coding: utf-8 -*-
"""Populate the Knowit template from outline.DECK (path A: clone & fill)."""
import os, re, shutil, zipfile
from xml.sax.saxutils import escape
from outline import DECK, FOOTER

U = 'unpacked'
ACCENT = '9795FF'

# Which placeholder each outline key maps to, per layout. Taken from the
# layouts' own <p:ph> attributes -- matched by type/idx, never by name.
PH = {
    1:  {'title': 'type="ctrTitle"',                  'sub':    'type="subTitle" idx="1"'},
    6:  {'kicker':'type="body" sz="quarter" idx="15"', 'title': 'type="title"', 'body': 'idx="1"'},
    14: {'kicker':'type="body" sz="quarter" idx="13"', 'title': 'type="title"', 'body': 'sz="half" idx="2"'},
    15: {'kicker':'type="body" sz="quarter" idx="13"', 'title': 'type="title"',
         'left':  'sz="half" idx="1"',                 'right': 'sz="half" idx="2"'},
    17: {'title': 'type="title"',                      'body':  'sz="half" idx="1"'},
    # NB: layouts.md has these two the wrong way round for this template --
    # verified against slideLayout19.xml itself: idx15 is the 32pt quote at the
    # top, idx14 the 10pt all-caps attribution near the bottom.
    19: {'quote': 'type="body" sz="quarter" idx="15"', 'attrib':'type="body" sz="quarter" idx="14"'},
}
# The footer placeholder, per layout (idx differs).
FTR = {1: 'type="ftr" sz="quarter" idx="11"', 6: 'type="ftr" sz="quarter" idx="17"',
       14:'type="ftr" sz="quarter" idx="15"', 15:'type="ftr" sz="quarter" idx="15"',
       17:'type="ftr" sz="quarter" idx="15"', 19:'type="ftr" sz="quarter" idx="17"'}

def runs_xml(runs, style='bullet'):
    out = []
    for text, bold, accent in runs:
        props = ' lang="en-GB" dirty="0"'
        if bold:  props += ' b="1"'
        if style == 'code': props += ' sz="1200"'
        inner = ''
        if accent: inner += f'<a:solidFill><a:srgbClr val="{ACCENT}"/></a:solidFill>'
        # Monospace only for code -- brand type is Bagoss everywhere else. Code
        # needs a fixed pitch to stay readable; prose does not.
        if style == 'code': inner += '<a:latin typeface="Menlo"/><a:cs typeface="Menlo"/>'
        body = f'<a:rPr{props}>{inner}</a:rPr>' if inner else f'<a:rPr{props}/>'
        out.append(f'<a:r>{body}<a:t>{escape(text)}</a:t></a:r>')
    return ''.join(out)

def paras_xml(content):
    """content: plain string (newlines -> paragraphs) or list of (lvl, runs, style)."""
    if isinstance(content, str):
        content = [(0, [(line, False, False)], 'note') for line in content.split('\n')]
    out = []
    for lvl, runs, style in content:
        # 'note' and 'code' suppress the layout's inherited bullet; code also
        # loses the hanging indent so leading spaces line up.
        if style == 'bullet':
            ppr = f'<a:pPr lvl="{lvl}"/>' if lvl else '<a:pPr/>'
        else:
            # Level is kept so a note can inherit the layout's size for that
            # level; marL/indent are zeroed so it still sits flush left.
            lvlattr = f' lvl="{lvl}"' if lvl else ''
            ppr = f'<a:pPr{lvlattr} marL="0" indent="0"><a:buNone/></a:pPr>'
        if len(runs) == 1 and runs[0][0] == '':
            out.append(f'<a:p>{ppr}<a:endParaRPr lang="en-GB"/></a:p>')
        else:
            out.append(f'<a:p>{ppr}{runs_xml(runs, style)}</a:p>')
    return ''.join(out) or '<a:p><a:endParaRPr lang="en-GB"/></a:p>'

def sp_xml(shape_id, name, ph_attrs, content):
    return (f'<p:sp><p:nvSpPr><p:cNvPr id="{shape_id}" name="{name}"/>'
            f'<p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr>'
            f'<p:nvPr><p:ph {ph_attrs}/></p:nvPr></p:nvSpPr><p:spPr/>'
            f'<p:txBody><a:bodyPr/><a:lstStyle/>{paras_xml(content)}</p:txBody></p:sp>')

SLIDE = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n'
         '<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"'
         ' xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
         ' xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">'
         '<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>'
         '<p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/>'
         '<a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>'
         '{shapes}</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>')

# --- wipe the template's demo slides, write ours -----------------------------
for f in os.listdir(f'{U}/ppt/slides'):
    if f.endswith('.xml'):
        os.remove(f'{U}/ppt/slides/{f}')
for f in os.listdir(f'{U}/ppt/slides/_rels'):
    os.remove(f'{U}/ppt/slides/_rels/{f}')

for i, (layout, fields) in enumerate(DECK, start=1):
    shapes, sid = [], 2
    for key, content in fields.items():
        attrs = PH[layout].get(key)
        assert attrs, f'slide {i}: layout {layout} has no placeholder for {key!r}'
        shapes.append(sp_xml(sid, f'{key.title()} {sid}', attrs, content)); sid += 1
    # footer classification, on every slide
    shapes.append(sp_xml(sid, f'Footer {sid}', FTR[layout], FOOTER))

    open(f'{U}/ppt/slides/slide{i}.xml', 'w', encoding='utf-8').write(
        SLIDE.format(shapes=''.join(shapes)))
    open(f'{U}/ppt/slides/_rels/slide{i}.xml.rels', 'w', encoding='utf-8').write(
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/'
        f'relationships/slideLayout" Target="../slideLayouts/slideLayout{layout}.xml"/></Relationships>')

n = len(DECK)

# --- presentation.xml.rels: drop old slide rels, add ours -------------------
rels = open(f'{U}/ppt/_rels/presentation.xml.rels').read()
rels = re.sub(r'<Relationship Id="[^"]+"[^>]*/slide" Target="slides/slide\d+\.xml"/>', '', rels)
new = ''.join(f'<Relationship Id="rSlide{i}" Type="http://schemas.openxmlformats.org/'
              f'officeDocument/2006/relationships/slide" Target="slides/slide{i}.xml"/>'
              for i in range(1, n + 1))
rels = rels.replace('</Relationships>', new + '</Relationships>')
open(f'{U}/ppt/_rels/presentation.xml.rels', 'w').write(rels)

# --- presentation.xml: rebuild the slide id list ----------------------------
pres = open(f'{U}/ppt/presentation.xml').read()
lst = ''.join(f'<p:sldId id="{600+i}" r:id="rSlide{i}"/>' for i in range(1, n + 1))
pres = re.sub(r'<p:sldIdLst>.*?</p:sldIdLst>', f'<p:sldIdLst>{lst}</p:sldIdLst>', pres, flags=re.S)
open(f'{U}/ppt/presentation.xml', 'w').write(pres)

# --- [Content_Types].xml: one override per slide ---------------------------
ct = open(f'{U}/[Content_Types].xml').read()
ct = re.sub(r'<Override PartName="/ppt/slides/slide\d+\.xml"[^>]*/>', '', ct)
new = ''.join(f'<Override PartName="/ppt/slides/slide{i}.xml" ContentType="application/'
              f'vnd.openxmlformats-officedocument.presentationml.slide+xml"/>'
              for i in range(1, n + 1))
ct = ct.replace('</Types>', new + '</Types>')
open(f'{U}/[Content_Types].xml', 'w').write(ct)

# --- repack, preserving everything we did not touch ------------------------
src = zipfile.ZipFile('template.pptx')
out = 'Thorium.pptx'
if os.path.exists(out): os.remove(out)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    written = set()
    for item in src.infolist():
        name = item.filename
        if name.startswith('ppt/slides/'):
            continue                      # ours are written below
        path = os.path.join(U, name)
        z.write(path, name) if os.path.exists(path) else z.writestr(item, src.read(name))
        written.add(name)
    for i in range(1, n + 1):
        z.write(f'{U}/ppt/slides/slide{i}.xml', f'ppt/slides/slide{i}.xml')
        z.write(f'{U}/ppt/slides/_rels/slide{i}.xml.rels', f'ppt/slides/_rels/slide{i}.xml.rels')
print(f'{out}: {n} slides')
