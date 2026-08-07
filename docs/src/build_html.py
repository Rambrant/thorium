# -*- coding: utf-8 -*-
"""Same outline -> a self-contained browser deck, brand assets base64-embedded."""
import base64, os
from html import escape
from outline import DECK, FOOTER

SK = os.environ['SK']
def b64(name):
    with open(f'{SK}/assets/{name}', 'rb') as f:
        return base64.b64encode(f.read()).decode()

WARM, COOL, MARK = b64('gradient_warm.png'), b64('gradient_cool.png'), b64('knowit_asterisk.png')

# layout number -> section class
CLS = {1: 'title', 6: 'divider', 14: '', 15: '', 17: 'conclusion', 19: 'quote'}

def runs(rs):
    out = []
    for text, bold, accent in rs:
        t = escape(text)
        if accent: t = f'<span class="a">{t}</span>'
        if bold:   t = f'<strong>{t}</strong>'
        out.append(t)
    return ''.join(out)

def block(content, tag='ul'):
    """(lvl, runs, style) -> bulleted <li>, un-bulleted <li>, or a code block."""
    if isinstance(content, str):
        return ''.join(f'<p class="sub">{escape(l)}</p>' for l in content.split('\n') if l)
    items, out, code_buf = [], [], []

    def flush_code():
        if code_buf:
            out.append('<pre>' + '\n'.join(escape(l) for l in code_buf) + '</pre>')
            code_buf.clear()

    def flush_items():
        if items:
            out.append(f'<{tag}>' + ''.join(items) + f'</{tag}>')
            items.clear()

    for _lvl, rs, style in content:
        blank = len(rs) == 1 and rs[0][0] == ''
        if style == 'code':
            flush_items(); code_buf.append(rs[0][0]); continue
        flush_code()
        if blank:      items.append('<li class="gap"></li>')
        elif style == 'note': items.append(f'<li class="note">{runs(rs)}</li>')
        else:          items.append(f'<li>{runs(rs)}</li>')
    flush_code(); flush_items()
    return ''.join(out)

sections = []
for n, (layout, f) in enumerate(DECK):
    cls = ' '.join(x for x in [CLS[layout], 'active' if n == 0 else ''] if x)
    parts = [f'<section class="{cls}">']
    if layout not in (1, 6, 17, 19):
        parts.append('<div class="mark"></div>')
    if 'kicker' in f: parts.append(f'<div class="kicker">{escape(f["kicker"])}</div>')
    # On the Conclusion layout the "title" slot is a small corner label, not the
    # headline (the headline is the first content paragraph) -- see build.py.
    if 'title' in f:
        tag = 'div class="kicker"' if layout == 17 else 'h1'
        parts.append(f'<{tag}>{escape(f["title"])}</{tag.split()[0]}>')
    if 'sub'    in f: parts.append(''.join(f'<p class="sub">{escape(l)}</p>' for l in f['sub'].split('\n')))
    if 'quote'  in f: parts.append('<blockquote>' + '<br>'.join(escape(l) for l in f['quote'].split('\n')) + '</blockquote>')
    if 'attrib' in f: parts.append(f'<div class="cite">{escape(f["attrib"])}</div>')
    if 'body'   in f: parts.append(block(f['body']))
    if 'left'   in f:
        parts.append('<div class="cols">'
                     f'<div class="col">{block(f["left"])}</div>'
                     f'<div class="col">{block(f["right"])}</div>'
                     '</div>')
    if layout not in (1, 17):
        parts.append(f'<div class="foot">{escape(FOOTER)}</div>')
    parts.append(f'<div class="num">{n+1} / {len(DECK)}</div>' if layout not in (1,17) else '')
    parts.append('</section>')
    sections.append('\n  '.join(p for p in parts if p))

HTML = f'''<!doctype html>
<html lang="en"><meta charset="utf-8">
<title>Thorium — compile-time verified hardware testing</title>
<style>
  :root{{
    --bg-dark:#0B0B26; --bg-light:#FEFBE6; --ink-on-dark:#FEFBE6; --ink-on-light:#0B0B26;
    --accent:#9795FF; --link:#5E5BFF;
    --font:'Bagoss Standard Light','Hanken Grotesk','Helvetica Neue',Arial,sans-serif;
    --display:4.0rem; --h1:2.5rem; --h2:1.9rem; --h3:1.25rem; --body:1.1rem; --caption:0.78rem;
  }}
  *{{box-sizing:border-box;margin:0}}
  html,body{{height:100%}}
  body{{font-family:var(--font);background:#000;overflow:hidden}}
  .deck{{height:100vh;width:100vw;overflow:hidden}}
  section{{position:absolute;inset:0;height:100vh;width:100vw;display:none;
    padding:8vh 8vw;flex-direction:column;justify-content:center;
    background:var(--bg-dark);color:var(--ink-on-dark)}}
  section.active{{display:flex}}
  section.title,section.conclusion{{
    background:var(--bg-light) url('data:image/png;base64,{WARM}') center/cover;
    color:var(--ink-on-light);align-items:center;text-align:center}}
  section.divider{{
    background:var(--bg-light) url('data:image/png;base64,{COOL}') center/cover;
    color:var(--ink-on-light)}}
  .kicker{{font-size:var(--caption);letter-spacing:.14em;text-transform:uppercase;
    color:var(--accent);margin-bottom:.8rem}}
  section.divider .kicker{{color:var(--link)}}
  section.title h1{{font-size:var(--display);line-height:1.02}}
  h1{{font-size:var(--h1);font-weight:600;line-height:1.1;max-width:30ch}}
  section.conclusion h1{{max-width:26ch}}
  .sub{{font-size:var(--h3);margin-top:1.1rem;max-width:44ch;opacity:.95}}
  ul{{font-size:var(--body);max-width:40ch;margin-top:1.5rem;line-height:1.45;list-style:none}}
  li{{margin:.55rem 0;padding-left:1.1rem;position:relative}}
  li:not(.gap):before{{content:'';position:absolute;left:0;top:.62em;width:.34rem;height:.34rem;
    border-radius:50%;background:var(--accent)}}
  li.gap{{height:.7rem;margin:0;padding:0}}
  li.note{{padding-left:0}} li.note:before{{content:none}}
  section.conclusion ul{{max-width:52ch;list-style:none;font-size:var(--h3)}}
  section.conclusion li:first-child{{font-size:var(--h1);font-weight:600;line-height:1.1;
    margin-bottom:1.2rem;max-width:26ch}}
  section.conclusion li{{padding-left:0;margin:.2rem 0}}
  section.conclusion li:before{{content:none}}
  pre{{font-family:'Menlo','SF Mono',ui-monospace,monospace;font-size:.85rem;line-height:1.5;
    color:var(--accent);margin:.9rem 0 .2rem;white-space:pre}}
  .cols{{display:flex;gap:6vw;align-items:flex-start;width:100%}}
  .col{{flex:1 1 0;max-width:40ch}}
  .col ul{{max-width:none;margin-top:0}}
  .col>*:first-child{{margin-top:1.5rem}}
  strong{{font-weight:600}}
  .a{{color:var(--accent)}}
  section.divider .a{{color:var(--link)}}
  blockquote{{font-size:var(--h2);line-height:1.28;max-width:30ch;font-weight:600}}
  .cite{{font-size:var(--h3);color:var(--accent);margin-top:1.8rem;max-width:42ch}}
  .foot{{position:absolute;left:8vw;bottom:4vh;font-size:var(--caption);opacity:.75}}
  .num{{position:absolute;right:8vw;bottom:4vh;font-size:var(--caption);opacity:.55}}
  .mark{{position:absolute;right:6vw;top:6vh;width:2.2rem;height:2.2rem;
    background:url('data:image/png;base64,{MARK}') center/contain no-repeat}}
</style>
<div class="deck">
  {chr(10).join(sections)}
</div>
<script>
  const s=[...document.querySelectorAll('section')];let i=0;
  const show=n=>{{s[i].classList.remove('active');i=Math.max(0,Math.min(s.length-1,n));s[i].classList.add('active')}};
  addEventListener('keydown',e=>{{
    if(['ArrowRight','ArrowDown',' ','PageDown'].includes(e.key)){{e.preventDefault();show(i+1)}}
    if(['ArrowLeft','ArrowUp','PageUp'].includes(e.key)){{e.preventDefault();show(i-1)}}
    if(e.key==='Home')show(0); if(e.key==='End')show(s.length-1);
  }});
  addEventListener('click',()=>show(i+1));
</script>
</html>'''
open('Thorium.html','w',encoding='utf-8').write(HTML)
print('Thorium.html', len(HTML)//1024, 'KB,', len(DECK), 'slides')
