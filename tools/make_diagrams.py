#!/usr/bin/env python3
"""The sequence diagrams under doc/diagrams/, laid out from a flat table.

Run from the repository root:

    python3 tools/make_diagrams.py

Writes every diagram in DIAGRAMS below to doc/diagrams/<name>.svg and exits
non-zero if any of them failed a geometry check. Takes no arguments; the set of
diagrams is the data at the bottom of this file, not something a caller picks.

---------------------------------------------------------------------------
Why a generator, and not just the SVG
---------------------------------------------------------------------------

The first version of measure.svg was hand-written, and the reason it is not any
more has nothing to do with typing effort. A sequence diagram is a grid --
participants on fixed x centres, one row per message -- so every row's y is a
function of the height of everything above it. Inserting one row by hand means
recomputing the y of every row below it, and each row is an arrow, a label, and
possibly a note box, so it is four or five coordinates each. Nothing catches an
arithmetic slip in that; the file stays valid SVG and simply renders wrong.

So the split here is the one .inc files already make in this tree: the content
is a flat readable table (DIAGRAMS -- participants, and a row per message or
note), and the geometry is code. Adding a row means adding a line to a list,
which is the edit a person should have to make, and the layout follows.

The checks in check() are the other half of the argument, and they are what a
hand-written file cannot have. Eight note boxes were crowding the arrow label
below them on the first run -- every one of them already on screen in a render
that had been looked at twice, because 2px of overlap between a box edge and a
line of text reads as tight kerning rather than as a mistake. The frame-edge
check has the same story and is worse: a label with a dashed border struck
through it looks like a font artefact, so the eye files it as a rendering
problem and moves on.

That is the class of defect this is for. Not the gross errors -- those show up
the moment the PNG is opened -- but the ones that survive review because they
are indistinguishable from ordinary rendering noise.

---------------------------------------------------------------------------
Why not mermaid, or PlantUML
---------------------------------------------------------------------------

Mermaid was the real alternative and is the obvious one -- GitHub renders a
```mermaid fence natively, so a diagram could live in a README with no tooling
and no committed image at all. Two things ruled it out. Rendering one to a
committed .svg needs mermaid-cli and therefore a Node toolchain, which this
repository otherwise has no use for; and an inline fence cannot be opened on its
own, linked to, or scaled, which these diagrams need because they are wide. What
is left, a mermaid source *and* a generated SVG beside it, is two files saying
the same thing where only one of them renders -- so the other drifts.

PlantUML is the same trade with a Java runtime instead of a Node one.

The cost of a homegrown layout is that it draws the one shape this file has code
for -- a sequence diagram, with notes and one level of frame -- and nothing else.
That is deliberate: the moment a second shape is wanted, the honest move is to
reach for a real tool rather than grow this one into one.

---------------------------------------------------------------------------
On the paths written inside a diagram
---------------------------------------------------------------------------

A note box that says "core/verbs/interlock.hpp" is a reference like any other,
and README.md section 7 makes those a rule. tools/check_references.py scans the
emitted .svg for exactly that reason -- not this file, even though this file is
where a person writes them. It cannot scan this one: check_references.py's own
allowlist is a list of paths that must *not* resolve, so pointing it at .py
sources makes it fail on itself. Checking the artifact rather than the source is
the compromise, and it is a real one -- a path fixed here is only checked once
this has been re-run and the .svg committed.
"""

import pathlib
import sys

# ---------------------------------------------------------------------------
# The look. Colours are picked to read on both a light and a dark page
# background -- every diagram paints its own white ground rather than
# inheriting one, because GitHub serves these to a dark-theme reader unchanged.
# ---------------------------------------------------------------------------
MONO = "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"
SANS = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"

INK      = "#22303c"    # arrows, and any text meant to be read first
MUTED    = "#5b6b7a"    # subtitles, the footer
LIFELINE = "#b9c4cf"
RETURN   = "#71818f"    # a dashed return, and its label
NOTE_F   = "#fffbeb"
NOTE_S   = "#d6bf7a"
FRAME_S  = "#a8762a"

MSG_FS, NOTE_FS, BOX_FS, SUB_FS = 11.5, 11.0, 12.0, 8.5

#
# Monospace advance width per point of font size. 0.605 is a shade wider than
# Menlo's real 0.6022, so every width computed from it is a slight
# over-estimate -- which is the direction that keeps text inside its box rather
# than one pixel outside it.
#
ADVANCE = 0.605

#
# The vertical grid. TOP is where the lifelines start (and so how much room the
# title block gets); the rest are the step from one row to the next.
#
# GAP is the largest of them and the one with a reason: a note box is followed
# by an arrow whose *label* sits 19px above the arrow line, so the clearance
# between a note and the next arrow is GAP - 19, not GAP. At the obvious 20 that
# is 1px, which check() rejects.
#
TOP, MSG_H, NOTE_LINE, NOTE_PAD, GAP = 152, 36, 15, 11, 34


def escape( text):
    return text.replace( "&", "&amp;").replace( "<", "&lt;").replace( ">", "&gt;")


def layout( diagram):
    """Turn a diagram's rows into placed shapes, top to bottom.

    Returns ( placed, frames, bottom) -- everything downstream needs
    coordinates only, so both the emitter and the checks work off this and
    cannot disagree about where something ended up.
    """
    width  = diagram[ "Width"]
    lanes  = { part[ 0]: part[ 1] for part in diagram[ "Parts"]}
    placed = []
    frames = []
    #
    # A stack, not one slot: the run lifecycle nests a per-test frame inside a
    # per-group one, and the two are the same shape at two depths.
    #
    opened = []
    y      = TOP + 28

    for row in diagram[ "Rows"]:
        kind = row[ 0]

        if kind == "frame":
            #
            # A frame's label sits in a tab on its top edge, so the first row
            # inside it has to clear the tab as well as the edge.
            #
            # An inner frame carries its own span. Nothing derives it from the
            # arrows inside: the span says which participants the frame is a
            # statement about, and a derived one would shift every time a row
            # moved.
            #
            span = row[ 2] if len( row) > 2 else diagram[ "FrameSpan"]

            y += 10
            opened.append(( y, row[ 1], span))
            y += 46

        elif kind == "endframe":
            top, label, span = opened.pop()
            frames.append(( top, y + 8, label, span))
            y += 36

        elif kind == "msg":
            _, source, target, arrow, text = row
            placed.append(( "msg", y, lanes[ source], lanes[ target], arrow, text))
            y += MSG_H

        elif kind == "note":
            _, over, lines = row
            box_width  = 2 * NOTE_PAD + max( len( line) for line in lines) * NOTE_FS * ADVANCE
            box_height = 2 * NOTE_PAD + NOTE_LINE * len( lines)
            #
            # Centred on its participant, then pushed back inside the canvas.
            # A note may cover a lifeline it is not about -- it is opaque, and
            # that is ordinary in a sequence diagram -- but it may not fall off
            # the edge, which is what the clamp is for.
            #
            # MARGIN, not the canvas's own 8: a frame drawn around this note has
            # to fit outside it and still be on the page, so the widest note
            # leaves room for one. Ten pixels here and frames ran off the edge.
            #
            MARGIN = 24

            left = max( MARGIN, min( lanes[ over] - box_width / 2, width - box_width - MARGIN))
            placed.append(( "note", y, left, box_width, lines, box_height))
            y += box_height + GAP

        else:
            raise AssertionError( f"unknown row kind {kind!r}")

    assert not opened, "a frame was opened and never closed"

    #
    # The authored span says which participants a frame is a statement about,
    # and it is written in those terms -- but a note box is sized from its own
    # text and is routinely wider than the participant it hangs off, so an
    # authored span cannot also be a promise to contain one. So it is a
    # minimum: each frame is widened here to hold whatever it encloses.
    #
    # Doing it rather than complaining about it, because the alternative was
    # hand-tuning a number per frame against a width nobody computes by eye --
    # which is exactly the arithmetic this generator exists to stop doing. The
    # check that survives is the one this cannot satisfy on its own: a frame
    # that has to grow past the edge of the canvas.
    #
    PAD = 12

    widened = []

    for frame_top, frame_bottom, label, ( left, right) in frames:
        for item in placed:
            if not ( frame_top < item[ 1] < frame_bottom):
                continue

            if item[ 0] == "note":
                low, high = item[ 2], item[ 2] + item[ 3]
            else:
                low, high = min( item[ 2], item[ 3]), max( item[ 2], item[ 3])

            left  = max( 10, min( left,  low  - PAD))
            right = min( width - 10, max( right, high + PAD))

        widened.append(( frame_top, frame_bottom, label, ( round( left), round( right))))

    #
    # Then outwards, so a frame still encloses the ones nested in it after both
    # have grown. Sorted by height: an enclosing frame is always the taller.
    #
    # This is why check() has nothing to say about nesting. A frame drawn
    # outside the one it nests in would read as two unrelated regions rather
    # than a loop inside a loop, and that was a check here until this pass
    # made it unreachable -- which is the better answer of the two, and the
    # one README.md section 7 asks for: where a property can be established by
    # construction, establish it, and do not keep an assertion that can no
    # longer fail. The canvas-edge check below is the one this cannot satisfy
    # on its own.
    #
    for outer in sorted( range( len( widened)), key = lambda i: widened[ i][ 0] - widened[ i][ 1]):
        o_top, o_bottom, o_label, ( o_left, o_right) = widened[ outer]

        for inner in range( len( widened)):
            i_top, i_bottom, _label, ( i_left, i_right) = widened[ inner]

            if inner != outer and o_top < i_top and o_bottom > i_bottom:
                o_left  = max( 10, min( o_left,  i_left  - PAD))
                o_right = min( width - 10, max( o_right, i_right + PAD))

        widened[ outer] = ( o_top, o_bottom, o_label, ( o_left, o_right))

    return placed, widened, y + 6


def check( diagram, placed, frames):
    """Everything a rendered diagram can get wrong that is still valid SVG.

    Each of these fired for real while the first diagram was being written, so
    none is hypothetical. Returns a list of complaints.
    """
    width   = diagram[ "Width"]
    lanes   = { part[ 0]: part[ 1] for part in diagram[ "Parts"]}
    faults  = []

    #
    # A participant box has to hold its own two labels, and not touch its
    # neighbour. Both are easy to break by renaming a participant, which is a
    # content edit that looks like it cannot affect the layout.
    #
    for left_part, right_part in zip( diagram[ "Parts"], diagram[ "Parts"][ 1:]):
        if left_part[ 1] + left_part[ 2] > right_part[ 1] - right_part[ 2] - 6:
            faults.append( f"participants {left_part[ 0]}/{right_part[ 0]} closer than 6px")

    for name, centre, half_width, _fill, _stroke, title, subtitle in diagram[ "Parts"]:
        widest = max( len( title) * BOX_FS * ADVANCE, len( subtitle) * SUB_FS * ADVANCE)
        if widest > 2 * half_width - 10:
            faults.append( f"participant {name} is labelled wider than its box")

    for index, item in enumerate( placed):
        if item[ 0] == "note":
            _, top, left, box_width, lines, box_height = item

            if left < 8 or left + box_width > width - 8:
                faults.append( f"note {lines[ 0][ :30]!r} runs off the canvas")

            #
            # The one that fired eight times: an arrow label is drawn above its
            # line, so a note sized to its own text can still collide with the
            # label of the row after it. See GAP.
            #
            if index + 1 < len( placed) and placed[ index + 1][ 0] == "msg":
                if placed[ index + 1][ 1] - 19 < top + box_height + 3:
                    faults.append( f"note {lines[ 0][ :30]!r} crowds the label below it")
        else:
            _, top, source_x, target_x, _arrow, text = item
            low, high  = min( source_x, target_x), max( source_x, target_x)
            text_width = len( text) * MSG_FS * ADVANCE
            text_left  = ( source_x + target_x) / 2 - text_width / 2
            text_right = text_left + text_width

            if text_left < 6 or text_right > width - 6:
                faults.append( f"label {text[ :30]!r} runs off the canvas")

            #
            # A label may sit over the two lifelines its arrow joins. Reaching a
            # third one means it has grown long enough to be read as belonging
            # to a participant it has nothing to do with.
            #
            for name, lane_x in lanes.items():
                if low < lane_x < high:
                    continue
                if lane_x < low - 1 and text_left < lane_x + 6:
                    faults.append( f"label {text[ :30]!r} reaches lifeline {name}")
                if lane_x > high + 1 and text_right > lane_x - 6:
                    faults.append( f"label {text[ :30]!r} reaches lifeline {name}")

    #
    # A dashed return answering nothing. This is the one defect in the list that
    # is not about pixels: the arrow is drawn perfectly and the diagram is
    # simply wrong about what happened, which means it survives every look at
    # the render and is only caught by reading the rows against each other.
    #
    # The rule is a call stack, but a forgiving one. These diagrams deliberately
    # leave most returns off -- a post() to the journal is drawn one way because
    # nothing about the answer is worth a row -- so an omitted return is not a
    # fault, and a return is matched against the innermost *open* call from the
    # participant it points at, not only against the top of the stack. Anything
    # above that match was a call whose return was left out, and is dropped.
    #
    # What cannot be excused is a return with no open call under it at all:
    # nobody is waiting for that value, so the arrow is claiming a frame of
    # control that was never entered.
    #
    stack = []

    for row in diagram[ "Rows"]:
        if row[ 0] != "msg":
            continue

        _, source, target, arrow, text = row

        if arrow == "call":
            stack.append(( source, target))
            continue

        for depth in range( len( stack) - 1, -1, -1):
            if stack[ depth] == ( target, source):
                del stack[ depth:]
                break
        else:
            faults.append( f"return {text[ :30]!r} answers no call from {target}")

    #
    # Two arrows whose labels are close enough to read as one line. This is what
    # the pair of returns either side of a frame edge did.
    #
    messages = [ item for item in placed if item[ 0] == "msg"]
    for earlier, later in zip( messages, messages[ 1:]):
        if 0 < later[ 1] - earlier[ 1] < 26:
            faults.append( f"arrows {earlier[ 5][ :20]!r} / {later[ 5][ :20]!r} nearly overlap")

    #
    # An arrow label sitting on a frame's dashed edge. This is the one that is
    # invisible in review rather than merely unnoticed: the label renders, the
    # edge renders through it, and the result reads as a font artefact -- the
    # first draft had "QuantityVariant" with the frame's bottom edge struck
    # through it and it looked like a rendering bug, not a layout one.
    #
    for frame_top, frame_bottom, label, ( left, right) in frames:
        if left < 8 or right > width - 8:
            faults.append( f"frame {label[ :24]!r} runs off the canvas")

        for item in placed:
            if item[ 0] != "msg":
                continue

            label_top, label_bottom = item[ 1] - 19, item[ 1] - 6

            for edge in ( frame_top, frame_bottom):
                if label_top - 4 <= edge <= label_bottom + 4:
                    faults.append(
                        f"label {item[ 5][ :24]!r} sits on frame {label[ :20]!r}'s edge")

            if frame_top - 14 < item[ 1] < frame_top + 22:
                faults.append( f"frame {label[ :24]!r} tab collides with an arrow")

    return faults


def render( diagram, placed, frames, bottom):
    width  = diagram[ "Width"]
    height = bottom + 30 + len( diagram[ "Footer"]) * 16
    out    = []

    out.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}"'
        f' viewBox="0 0 {width} {height}" font-family="{SANS}">')

    out.append( '  <defs>')
    out.append(
        f'    <marker id="call" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7"'
        f' markerHeight="7" orient="auto-start-reverse">'
        f'<path d="M 0 0 L 10 5 L 0 10 z" fill="{INK}"/></marker>')
    out.append(
        f'    <marker id="ret" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8"'
        f' markerHeight="8" orient="auto-start-reverse">'
        f'<path d="M 0 1 L 9 5 L 0 9" fill="none" stroke="{RETURN}" stroke-width="1.7"/></marker>')
    out.append( '  </defs>')

    #
    # An explicit ground rather than a transparent one: a transparent SVG picks
    # up the reader's page colour, and every ink colour here is chosen against
    # white.
    #
    out.append( f'  <rect width="{width}" height="{height}" fill="#ffffff"/>')

    out.append( f'  <text x="22" y="36" font-size="17.5" font-weight="600" fill="{INK}">'
                f'{escape( diagram[ "Title"])}</text>')
    for index, line in enumerate( diagram[ "Subtitle"]):
        out.append( f'  <text x="22" y="{58 + index * 18}" font-size="12.5" fill="{MUTED}">{line}</text>')

    for _name, centre, _half_width, _fill, _stroke, _title, _subtitle in diagram[ "Parts"]:
        out.append( f'  <line x1="{centre}" y1="{TOP}" x2="{centre}" y2="{bottom}"'
                    f' stroke="{LIFELINE}" stroke-width="1.4" stroke-dasharray="3 4"/>')

    #
    # Drawn after the lifelines and before the notes: a frame is a region, so it
    # belongs behind anything with an opaque fill.
    #
    for frame_top, frame_bottom, label, ( left, right) in frames:
        out.append( f'  <rect x="{left}" y="{frame_top}" width="{right - left}"'
                    f' height="{frame_bottom - frame_top}" fill="none" stroke="{FRAME_S}"'
                    f' stroke-width="1.2" stroke-dasharray="6 4" rx="4"/>')
        tab_width = 12 + len( label) * 6.15
        out.append( f'  <rect x="{left}" y="{frame_top}" width="{tab_width:.0f}" height="20"'
                    f' fill="{FRAME_S}" rx="3"/>')
        out.append( f'  <text x="{left + 6}" y="{frame_top + 14}" font-size="10.5"'
                    f' font-weight="600" fill="#ffffff">{escape( label)}</text>')

    for _name, centre, half_width, fill, stroke, title, subtitle in diagram[ "Parts"]:
        out.append( f'  <rect x="{centre - half_width}" y="{TOP - 54}" width="{half_width * 2}"'
                    f' height="54" rx="5" fill="{fill}" stroke="{stroke}" stroke-width="1.3"/>')
        out.append( f'  <text x="{centre}" y="{TOP - 32}" text-anchor="middle" font-size="{BOX_FS:g}"'
                    f' font-weight="600" fill="{INK}" font-family="{MONO}">{escape( title)}</text>')
        out.append( f'  <text x="{centre}" y="{TOP - 14}" text-anchor="middle" font-size="{SUB_FS:g}"'
                    f' fill="{MUTED}" font-family="{MONO}">{escape( subtitle)}</text>')

    for item in placed:
        if item[ 0] == "msg":
            _, top, source_x, target_x, arrow, text = item
            towards = 1 if target_x > source_x else -1
            stroke  = INK if arrow == "call" else RETURN
            dashed  = '' if arrow == "call" else ' stroke-dasharray="6 4"'
            out.append( f'  <line x1="{source_x + towards * 2}" y1="{top}"'
                        f' x2="{target_x - towards * 7}" y2="{top}" stroke="{stroke}"'
                        f' stroke-width="1.5"{dashed} marker-end="url(#{arrow})"/>')
            out.append( f'  <text x="{( source_x + target_x) / 2:.0f}" y="{top - 8}"'
                        f' text-anchor="middle" font-size="{MSG_FS:g}" fill="{stroke}"'
                        f' font-family="{MONO}">{escape( text)}</text>')
        else:
            _, top, left, box_width, lines, box_height = item
            out.append( f'  <rect x="{left:.0f}" y="{top}" width="{box_width:.0f}"'
                        f' height="{box_height}" rx="4" fill="{NOTE_F}" stroke="{NOTE_S}"'
                        f' stroke-width="1.1"/>')
            for index, line in enumerate( lines):
                if not line:
                    continue
                out.append( f'  <text x="{left + NOTE_PAD:.0f}"'
                            f' y="{top + NOTE_PAD + 11 + index * NOTE_LINE}" font-size="{NOTE_FS:g}"'
                            f' fill="{INK}" font-family="{MONO}">{escape( line)}</text>')

    out.append( f'  <line x1="22" y1="{bottom + 10}" x2="{width - 22}" y2="{bottom + 10}"'
                f' stroke="{LIFELINE}" stroke-width="1"/>')
    for index, line in enumerate( diagram[ "Footer"]):
        out.append( f'  <text x="22" y="{bottom + 30 + index * 16}" font-size="11"'
                    f' fill="{MUTED}">{escape( line)}</text>')

    out.append( '</svg>')

    return "\n".join( out) + "\n"


# ---------------------------------------------------------------------------
# The content. Everything above is mechanism; everything below is the table.
#
# A participant is ( key, x centre, half width, fill, stroke, name, where it
# lives) -- and the fills say which layer it belongs to: blue for a test
# script, slate for framework/core, sand for framework/hal and the drivers.
#
# A row is one of
#     ( "msg",  from, to, "call" | "ret", label)
#     ( "note", over participant, [ lines])
#     ( "frame", label) ... ( "endframe",)
# and they are laid out in the order written.
# ---------------------------------------------------------------------------
SCRIPT_FILL, SCRIPT_LINE = "#e7f0fb", "#2b6cb0"
CORE_FILL,   CORE_LINE   = "#eef3f7", "#3d5666"
HAL_FILL,    HAL_LINE    = "#fdf4e4", "#8a5a00"

MEASURE = {
    "Width": 1160,
    "Title": "What one Measure( ...) actually does",
    "Subtitle": [
        "From the call site to the rows in the log. This is ordering, not data flow -- and the order is the part the README's block does not show:",
        "the session sits <tspan font-style=\"italic\">above</tspan> the fabric and the driver, and reaches them only through a callback it is free never to invoke.",
    ],
    #
    # The frame has to reach from the engine to the driver, since the whole of
    # the live path is inside it. Fixed rather than derived from the rows: it is
    # a statement about which participants the callback can touch, and deriving
    # it from whichever arrows happen to be in there today would make it drift
    # every time a row moved.
    #
    "FrameSpan": ( 196, 996),
    "Parts": [
        ( "script",   88, 78, SCRIPT_FILL, SCRIPT_LINE, "a test script",       "suite/scripts/*.cpp"),
        ( "measure", 300, 88, CORE_FILL,   CORE_LINE,   "core::MeasureEngine", "core/verbs/measure.hpp"),
        ( "session", 540, 80, CORE_FILL,   CORE_LINE,   "core::ISession",      "core/session/session.hpp"),
        ( "fabric",  748, 80, HAL_FILL,    HAL_LINE,    "hal::SwitchFabric",   "hal/fabric/switch_fabric.hpp"),
        ( "driver",  916, 72, HAL_FILL,    HAL_LINE,    "the driver",          "core::Port -> instrument"),
        ( "journal",1068, 70, CORE_FILL,   CORE_LINE,   "core::Journal",       "core/journal/journal.hpp"),
    ],
    "Rows": [
        ( "msg", "script", "measure", "call", "Measure( Dmm1.voltage(), at( dut::Output5V))"),
        ( "note", "measure", [ 'key = "Output5V"  -- the point\'s name, and the port\'s',
                               'qualifier when it has one ("Output5V.Vmin")']),
        ( "msg", "measure", "session", "call", "fetch( key, kind, liveRead)"),
        ( "note", "session", [ 'fetch( key, "Dmm1", QuantityKind::Voltage, liveRead)',
                               "",
                               "The session decides where the value comes from. Only LiveSession",
                               "calls liveRead. ScriptedSession, a replay and PlaceholderSession",
                               "answer from an injection, a recording or a zero -- and nothing",
                               "in the frame below happens at all."]),
        ( "frame", "live only -- inside the liveRead callback"),
        #
        # A call, not a return: the session *invokes* the callback the engine
        # handed it, and the QuantityVariant at the bottom of the frame is what
        # that invocation returns. Drawn dashed it read as fetch() answering,
        # which left the two returns below it answering nothing.
        #
        ( "msg", "session", "measure", "call", "liveRead()"),
        ( "note", "measure", [ "the electrical interlock, before any path is composed:",
                               "energisedSourceAt( Loc) throws when a source holds this",
                               "pin live and the reading needs a dead node",
                               "                             core/verbs/interlock.hpp"]),
        ( "note", "measure", [ "tapWiring.taps( instrumentId)?",
                               "On a direct-wired rig this confirms the instrument's own",
                               "leads are on this very pin, and skips all of the fabric",
                               "below -- a tapped reading is not a degenerate route."]),
        ( "note", "measure", [ "path = instrumentWiring.find( instrumentId)",
                               "     + connectorWiring.find( Loc)",
                               "     + both sense paths, when SensePath::Required",
                               "Composed at the moment of reading, never stored.",
                               "                            hal/topology/wiring.hpp"]),
        ( "msg", "measure", "fabric", "call", "connect( path)"),
        ( "note", "fabric", [ "close( id) for each element, in order.",
                              "Reference-counted, so this cannot disturb a",
                              "path another instrument is holding open."]),
        ( "msg", "measure", "driver", "call", "port.rawMeasure()"),
        ( "note", "driver", [ "the driver's own SCPI, over io::ScpiSession",
                              "and io::Transport -- CONFigure..., then READ?"]),
        ( "msg", "driver", "measure", "ret", "Voltage"),
        ( "note", "measure", [ "An UnmeasurableReading is caught here and turned into",
                               "the value whenUnmeasurable() gives -- inside the pair",
                               "below, so a normal failure still opens the relays."]),
        ( "msg", "measure", "fabric", "call", "disconnect( path)"),
        ( "note", "fabric", [ "open( id) each -- and a relay truly opens only",
                              "once every close() on it has been matched, so a",
                              "supply parked on this point keeps its own shut."]),
        ( "msg", "measure", "session", "ret", "QuantityVariant{ Voltage }"),
        ( "endframe",),
        ( "msg", "session", "measure", "ret", "QuantityVariant"),
        ( "note", "session", [ "On the way out, a RecordingSession files the value",
                               "for replay -- a strict ordered sequence, not a report."]),
        ( "note", "measure", [ "asQuantity<Voltage>()  -- the type erasure ends here, so",
                               "everything downstream, the log included, has the real type"]),
        ( "msg", "measure", "journal", "call", "post( JournalRecord)"),
        ( "note", "journal", [ "post({ Verb::Measure, key, detail, instrument, value, unit })",
                               "",
                               "onEvent -> every registered sink, in registration order:",
                               "ConsoleSink, then SarifSink and RtfSink when the run",
                               "was asked for artifacts."]),
        ( "msg", "measure", "script", "ret", "Voltage"),
    ],
    "Footer": [
        "One Measure() is exactly one journal event, whether the value came from an instrument, an injection or a recording -- so a script's log says what the",
        "script measured, not how the run was set up. The Verify( FS_Supply_1::FS_Supply_5V0, rail) that follows is a second, separate event.",
        "Solid arrow: a call.   Dashed: what it returns.",
    ],
}


LIFECYCLE = {
    "Width": 1200,
    "Title": "How a run starts, and how it stops",
    "Subtitle": [
        "framework/runner/src/main.cpp from the first argument to the last log line. The startup order is what makes a failure legible; the shutdown",
        "order is what makes it safe -- and every teardown guard below is deliberately constructed <tspan font-style=\"italic\">before</tspan> the setup it answers for.",
    ],
    "FrameSpan": ( 40, 1010),
    "Parts": [
        ( "main",    110, 86, SCRIPT_FILL, SCRIPT_LINE, "main()",         "framework/runner"),
        ( "flight",  340, 88, HAL_FILL,    HAL_LINE,    "hal preflight",  "hal/verbs/preflight.hpp"),
        ( "journal", 560, 80, CORE_FILL,   CORE_LINE,   "core::Journal",  "core/journal/journal.hpp"),
        ( "hooks",   770, 82, SCRIPT_FILL, SCRIPT_LINE, "the hooks",      "suite/test_catalog.inc"),
        ( "script",  950, 74, SCRIPT_FILL, SCRIPT_LINE, "a test script",  "suite/scripts/*.cpp"),
        ( "safing", 1110, 76, HAL_FILL,    HAL_LINE,    "hal::safeRig",   "hal/verbs/safing.hpp"),
    ],
    "Rows": [
        ( "frame", "startup -- nothing has been touched yet", ( 40, 700)),
        ( "note", "main", [ "The --address flags go in first and $THORIUM_ADDRESS_* after",
                            "them, because bindAddresses takes the first override it finds",
                            "-- so a flag typed now beats an export a bench PC set months ago."]),
        ( "msg", "main", "flight", "call", "bindAddresses( plan)"),
        ( "note", "flight", [ "per row, in this order: an --address override, the site table,",
                              "this row's candidate pool, then the row's own third column.",
                              "Every candidate is checked against the driver's hal::BackPanel,",
                              "so a bus the instrument has no connector for is refused here."]),
        ( "msg", "main", "flight", "call", "contactInstruments( bindings)"),
        ( "note", "flight", [ "opens every reachable instrument, asks *IDN?, and acquires each",
                              "pooled row from its shelf. Skipped entirely on a detached run:",
                              "a run that must not touch hardware must not open a socket to",
                              "find out what is there."]),
        ( "note", "main", [ "runInfo = defaultRunInfo() + bannerLines( bindings)",
                            "The traceability header is assembled before anything is measured,",
                            "so a preflight that fails fails before any log exists -- which is",
                            "the right way round: a run that cannot say which instruments it",
                            "is using has nothing worth writing down."]),
        ( "msg", "main", "journal", "call", "add( ConsoleSink), add( SarifSink), add( RtfSink)"),
        ( "msg", "main", "journal", "call", "begin( runInfo)"),
        ( "endframe",),
        ( "note", "main", [ "hal::RigSafingGuard safeOnExit;",
                            "Constructed inside begin()/end(), so the Safe event its own",
                            "destructor posts lands in the log of the run it belongs to."]),
        ( "note", "main", [ "unmatchedSelection( options.Selection)",
                            "A --select naming a test this catalog has not got stops the run",
                            "here -- before RUN_SETUP powers a rig up to then not run what",
                            "was asked for."]),
        ( "note", "main", [ "TeardownGuard teardown{ Teardown};   <-- before the setup below"]),
        ( "msg", "main", "hooks", "call", "RUN_SETUP"),
        ( "note", "hooks", [ "rigPowerOn: relay closed dead, then energised, primary before",
                             "the alternates. It reads each source back, and a false stops",
                             "the run before the first script."]),
        ( "frame", "for each --repeat pass, for each group with something selected", ( 40, 1010)),
        ( "msg", "main", "journal", "call", "beginGroup( name)"),
        ( "note", "main", [ "TeardownGuard groupTeardown{ group.teardown};",
                            "Again before the group's own setup, and inside beginGroup so",
                            "whatever a hook posts is attributed to the group it brackets.",
                            "Per pass, not per run: --repeat=3 arms and disarms each group",
                            "three times."]),
        ( "msg", "main", "hooks", "call", "group SETUP"),
        ( "frame", "for each selected test", ( 76, 1010)),
        ( "msg", "main", "journal", "call", "beginTest( id)"),
        ( "msg", "main", "script", "call", "test.script()"),
        ( "note", "script", [ "Measure, Verify, Setup, Apply, Arm ... every verb posts to the",
                              "journal as it goes. measure.svg is one of them end to end.",
                              "",
                              "A script that throws unwinds straight past endTest to main's",
                              "handler: an exception out of a script means the rig is in an",
                              "unknown state, which is what the guards above are for."]),
        ( "msg", "main", "journal", "call", "endTest()"),
        ( "msg", "journal", "main", "ret", "passed"),
        ( "note", "journal", [ "Derived from the checks the script actually recorded, never",
                               "returned by the script -- so there is no flag to fold, and no",
                               "way for a script's answer to disagree with its own log. A",
                               "script that recorded no check cannot pass."]),
        ( "endframe",),
        ( "msg", "main", "hooks", "call", "group TEARDOWN"),
        ( "note", "hooks", [ "The guard's destructor, so it runs whether or not the group's",
                             "tests did -- and before endGroup closes the group it belongs to."]),
        ( "msg", "main", "journal", "call", "endGroup()"),
        ( "endframe",),
        ( "msg", "main", "hooks", "call", "RUN_TEARDOWN"),
        ( "note", "hooks", [ "rigPowerOff: the exact inverse of the power-up. Reached on every",
                             "path out, including the one where RUN_SETUP failed half way",
                             "through -- which is the whole of what constructing the guard",
                             "first buys, and why a restore written at the end of a script is",
                             "not the same thing."]),
        ( "msg", "main", "safing", "call", "~RigSafingGuard"),
        ( "note", "safing", [ "The crash path: everything off, every relay open, unordered and",
                              "unconditional. It exists to be correct when nothing else ran,",
                              "which is exactly why it cannot express a sequence -- and why",
                              "the ordered teardowns above are separate from it."]),
        ( "msg", "main", "journal", "call", "end( allPassed)"),
        ( "note", "journal", [ "the report, and both logs closed"]),
    ],
    "Footer": [
        "Every teardown here is a destructor, and every one of them is constructed before the setup it answers for. That is the single idea the shutdown half",
        "of this diagram is about: a teardown that only runs when its setup succeeded is not a teardown, it is the happy path with extra steps.",
        "Solid arrow: a call.   Dashed: what it returns.",
    ],
}

SOURCE = {
    "Width": 1180,
    "Title": "Connect, Apply, Remove, Disconnect -- and why they nest",
    "Subtitle": [
        "The relay moves while the path is dead, at both ends of the sequence. Nothing here enforces that: hot switching is a wear argument, not a",
        "correctness one -- so instead of refusing it, Connect and Disconnect ask the driver whether its output was live and put the answer in the log.",
    ],
    "FrameSpan": ( 150, 1140),
    "Parts": [
        ( "script",   95, 78, SCRIPT_FILL, SCRIPT_LINE, "a test script",       "suite/scripts/*.cpp"),
        ( "route",   320, 90, CORE_FILL,   CORE_LINE,   "Connect/Disconnect",  "core/verbs/route.hpp"),
        ( "source",  545, 84, CORE_FILL,   CORE_LINE,   "Apply/Remove",        "core/verbs/source.hpp"),
        ( "fabric",  740, 78, HAL_FILL,    HAL_LINE,    "hal::SwitchFabric",   "hal/fabric/switch_fabric.hpp"),
        ( "driver",  915, 72, HAL_FILL,    HAL_LINE,    "the driver",          "a supply, via ADL"),
        ( "journal",1080, 74, CORE_FILL,   CORE_LINE,   "core::Journal",       "core/journal/journal.hpp"),
    ],
    "Rows": [
        ( "note", "source", [ "The order the hardware cares about, written out as a rig's real",
                              "power-up in suite/scripts/rig_power_on.cpp:",
                              "",
                              "    Connect, Apply  ...  Remove, Disconnect",
                              "",
                              "It nests rather than mirrors, and both ends are the same",
                              "mechanism: contacts that part under current draw an arc, and",
                              "contacts closing onto a live supply bounce, breaking an",
                              "energised circuit on each bounce."]),
        ( "frame", "bringing a rail up", ( 40, 1140)),
        ( "msg", "script", "route", "call", "Connect( DcP5.dc())"),
        ( "msg", "route", "driver", "call", "isEnergised( config)?"),
        ( "msg", "driver", "route", "ret", "false -- cold, which is the point"),
        ( "note", "route", [ "core::detail::energisedNow asks it, through an unqualified",
                             "isEnergised -- so this header stays free of any hal:: type.",
                             "",
                             "Optional, because most configs Connect is called with are not",
                             "sources at all: asking whether a UART is live is not a question.",
                             "But optional in the one shape that cannot lose the check by",
                             "omission -- \"has an applyDriver\" is exactly \"Apply can energise",
                             "this\", so a config with one and no isEnergised is a static_assert",
                             "naming the missing member, not a quiet report of \"cold\".",
                             "",
                             "Not asked at all on a detached run: the driver call is guarded by",
                             "bench().isAttached(), so nothing to switch means nothing hot."]),
        ( "msg", "route", "fabric", "call", "connect( path)"),
        ( "msg", "route", "journal", "call", "post( Verb::Connect, hot = false)"),
        ( "msg", "script", "source", "call", "Apply( DcP5.dc().voltage( 5_V))"),
        ( "msg", "source", "driver", "call", "applyDriver( config)"),
        ( "note", "driver", [ "Only when bench().isAttached(). A replayed run takes its",
                              "readings from a file, and energising a rail for real while",
                              "doing so is the one thing it must not do -- but the event is",
                              "posted either way, and says which happened."]),
        ( "msg", "source", "journal", "call", "post( Verb::Apply)"),
        ( "note", "journal", [ "Posted after the driver call, never before: if applyDriver",
                               "throws, nothing was applied and nothing should claim it was."]),
        ( "endframe",),
        ( "frame", "and taking it back down -- the exact inverse", ( 40, 1140)),
        ( "msg", "script", "source", "call", "Remove( DcP5.dc())"),
        ( "msg", "source", "driver", "call", "removeDriver( config)"),
        ( "msg", "source", "journal", "call", "post( Verb::Remove)"),
        ( "msg", "script", "route", "call", "Disconnect( DcP5.dc())"),
        ( "msg", "route", "driver", "call", "isEnergised( config)?"),
        ( "msg", "driver", "route", "ret", "false"),
        ( "msg", "route", "fabric", "call", "disconnect( path)"),
        ( "msg", "route", "journal", "call", "post( Verb::Disconnect, hot = false)"),
        ( "endframe",),
        ( "note", "source", [ "Swap Apply above Connect and nothing refuses it. That is",
                              "deliberate -- a safety interlock dropping a connection the",
                              "instant it is asked to should not first wait out a supply's",
                              "ramp-down, and that call belongs to whoever writes the",
                              "sequence. What changed is that the choice is no longer",
                              "invisible: energisedNow() is asked at the moment the contact",
                              "moves, and a true lands in the event.",
                              "",
                              "Which is what lets the acceptance suite read it back out of a",
                              "run's own machine log -- see acceptance/test_acceptance.cpp,",
                              "NoShippedScriptOrHookMovesARelayUnderLoad. Getting the order",
                              "wrong is not a build error and not a failing script; it is a",
                              "line in the log that a test goes looking for."]),
    ],
    "Footer": [
        "Setup is the fourth verb of this family and is not on the diagram: it tells an instrument how to behave without making it do anything, so it moves no",
        "relay and energises nothing. core/verbs/interlock.hpp is why this hazard is recorded where an ammeter across a live rail is refused outright.",
        "Solid arrow: a call.   Dashed: what it returns.",
    ],
}

CAPTURE = {
    "Width": 1140,
    "Title": "A single-shot capture: Arm, cause the event, Await",
    "Subtitle": [
        "suite/scripts/ac_dropout_script.cpp -- the one observation whose stimulus the script itself causes, which is why it cannot be a plain Measure.",
        "Every step is a separate verb because every step is a separate thing that can go wrong, and a log showing them in order is what tells them apart.",
    ],
    "FrameSpan": ( 150, 980),
    "Parts": [
        ( "script",   95, 78, SCRIPT_FILL, SCRIPT_LINE, "a test script",   "suite/scripts/*.cpp"),
        ( "acquire", 330, 88, CORE_FILL,   CORE_LINE,   "Arm/Await",       "core/verbs/acquire.hpp"),
        ( "session", 570, 80, CORE_FILL,   CORE_LINE,   "core::ISession",  "core/session/session.hpp"),
        ( "driver",  800, 78, HAL_FILL,    HAL_LINE,    "the scope",       "keysight_dsox1202g"),
        ( "journal", 1010, 74, CORE_FILL,  CORE_LINE,   "core::Journal",   "core/journal/journal.hpp"),
    ],
    "Rows": [
        ( "msg", "script", "acquire", "call", "Arm( Osc1.single().timeout( 2_s))"),
        ( "msg", "acquire", "driver", "call", "armDriver( config)"),
        ( "note", "driver", [ "Arm returns when the scope is *armed and ready to trigger*, not",
                              "when it has been told to arm. Those are different moments, and",
                              "the gap between them is where a single-shot test silently",
                              "fails: the event fires while the scope is still setting itself",
                              "up, nothing triggers, and the run reports a missing transient",
                              "that was in fact there."]),
        ( "msg", "acquire", "journal", "call", "post( Verb::Arm)"),
        ( "note", "script", [ "the event, and the script causes it:",
                              "",
                              "    Remove(     AcP1.ac());",
                              "    Disconnect( AcP1.ac());",
                              "",
                              "Remove before Disconnect, so the relay opens on a dead path",
                              "-- see source.svg. This is the very next line after Arm, which",
                              "is only safe because Arm blocked until the scope was ready."]),
        ( "msg", "script", "acquire", "call", "Await( Osc1.single())"),
        ( "msg", "acquire", "session", "call", "fetchFlag( key, instrument, livePoll)"),
        ( "note", "session", [ "The same seam Measure goes through, and for the same reason: a",
                               "live run polls the scope, and a scripted or replayed one",
                               "answers from its file and never touches the instrument. Which",
                               "is what lets this script's own unit test drive a capture that",
                               "never happened."]),
        ( "msg", "session", "acquire", "ret", "completed"),
        ( "msg", "acquire", "journal", "call", "post( Verb::Await, completed)"),
        ( "msg", "acquire", "script", "ret", "bool"),
        ( "note", "script", [ "Verify( FS_Transient_Captured, captured)  -- checked BEFORE",
                              "anything is read out of the acquisition, and the ordering is",
                              "the point rather than tidiness. With no capture the buffer",
                              "still holds whatever was in it beforehand, so a vmin() taken",
                              "here would return a real number about a real waveform -- just",
                              "not this one. It would be in tolerance, and it would mean",
                              "nothing."]),
        ( "frame", "only if the capture completed", ( 40, 700)),
        ( "note", "script", [ "const auto lowest = Measure( Osc1.channel<2>().vmin(),",
                              "                             at( dut::Output5V));",
                              "const auto dip    = baseline - lowest;",
                              "",
                              "An ordinary Measure from here on, through MeasureEngine rather",
                              "than through these two verbs -- see measure.svg. The reading",
                              "itself is inside the guard, not merely the arithmetic on it.",
                              "",
                              "Two readings at one pin is why a scope's ports carry a",
                              "qualifier: they key as Output5V.Vbase and Output5V.Vmin rather",
                              "than both as Output5V."]),
        ( "endframe",),
        ( "note", "script", [ "and if it did not, Fail( \"... not measured, the capture did not",
                              "complete\") -- the prose form rather than the criterion, because",
                              "nothing here measured how far the rail dipped, and naming the",
                              "criterion even as unchecked would attach this run's silence to",
                              "a requirement it has nothing to say about.",
                              "",
                              "Something is recorded either way, never nothing: a report in",
                              "which a check simply does not appear reads as a run that did",
                              "not need it."]),
    ],
    "Footer": [
        "Arm and Await are named for the operation rather than for an oscilloscope, and deliberately live in core: \"arm a capture, let the event happen, find",
        "out whether it landed\" is what a transient recorder does, what a digitizer does, and what a DMM in triggered burst mode does.",
        "Solid arrow: a call.   Dashed: what it returns.",
    ],
}

#
# Insertion order is the order they are written and the order to read them in:
# one verb end to end, then the run that calls it, then two verb families whose
# ordering is their whole content.
#
DIAGRAMS = {
    "measure":   MEASURE,
    "lifecycle": LIFECYCLE,
    "source":    SOURCE,
    "capture":   CAPTURE,
}


def main():
    #
    # Rooted off this file rather than the working directory, so the paths it
    # writes do not depend on where it was run from -- unlike
    # check_references.py, which is about the tree as the caller sees it.
    #
    destination = pathlib.Path( __file__).resolve().parent.parent / "doc" / "diagrams"
    destination.mkdir( parents = True, exist_ok = True)

    failed = False

    for name, diagram in DIAGRAMS.items():
        placed, frames, bottom = layout( diagram)
        faults                 = check( diagram, placed, frames)
        path                   = destination / f"{name}.svg"

        #
        # Written even when a check failed: a diagram you can look at is how you
        # work out what the complaint means.
        #
        path.write_text( render( diagram, placed, frames, bottom))

        print( f"{path.relative_to( destination.parent.parent)}  "
               f"{diagram[ 'Width']}x{bottom + 30 + len( diagram[ 'Footer']) * 16}")

        for fault in faults:
            print( f"    {fault}")
            failed = True

    if failed:
        print( "\nGeometry checks failed -- the SVG above was still written, so it can be looked at.")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit( main())
