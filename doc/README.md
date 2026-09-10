# doc/ -- the diagrams

Prose carries the rationale in this tree; every directory's README says why its
own contents are the way they are, and that is deliberately where the reasoning
lives. A diagram earns a place here only when the thing it shows is an *order* or
a *shape* that prose states correctly but a reader still has to assemble in their
head -- which is a much smaller set than it sounds, and is why there are few files
in this directory rather than one per module.

The test scripts are the clearest example of what does *not* belong here.
`suite/scripts/supply_rail_script.cpp` is two `Measure` calls and two `Verify`
calls; a diagram of it would be longer than the script and say less, and it would
be a second copy of the script kept in step by nothing. A script that reads like
its own test spec is already the picture.

## What is here

Four, in the order to read them: one verb end to end, the run that calls it, and
two verb families whose ordering is their whole content.

| | |
|---|---|
| [`diagrams/measure.svg`](diagrams/measure.svg) | What one `Measure( Dmm1.voltage(), at( dut::Output5V))` does, from the call site to the rows in the log. Linked from the README's "How it is all connected", whose block shows where each piece comes from but not the order -- and the order is where the surprise is: `core::ISession` sits *above* the fabric and the driver and reaches them only through the `liveRead` callback, which a scripted or replayed run never invokes. |
| [`diagrams/lifecycle.svg`](diagrams/lifecycle.svg) | How a run starts and how it stops -- address resolution, the preflight, the traceability header, then the three nested brackets (`RUN_SETUP`, a group's `SETUP`, a test) and their teardowns, ending at `hal::safeRig()`. Its one idea: every teardown is a destructor constructed *before* the setup it answers for, which is prose in three separate READMEs and a picture here. |
| [`diagrams/source.svg`](diagrams/source.svg) | `Connect, Apply ... Remove, Disconnect` -- why the sequence nests rather than mirrors, and what happens when you write it the other way round. Nothing refuses hot switching, because it is a wear argument rather than a correctness one; what the framework does instead is ask the driver whether its output was live at the moment the contact moved, and put the answer in the log where the acceptance suite reads it back. |
| [`diagrams/capture.svg`](diagrams/capture.svg) | `Arm`, cause the event, `Await` -- the one observation whose stimulus the script itself causes, and therefore the only one that cannot be a plain `Measure`. Shows why `Arm` blocks until the instrument is *armed* rather than *told to arm*, and why the capture's yes/no is checked before anything is read out of the acquisition. |

## The diagrams are generated, and the table is the source

These are laid out by [`tools/make_diagrams.py`](../tools/make_diagrams.py) and
committed as SVG. To change one, edit the table at the bottom of that file and
re-run it from the repository root:

```bash
python3 tools/make_diagrams.py
```

The split is the one the `.inc` tables already make in this tree: the content is
flat and readable -- a participant list, and one row per message or note -- and
the geometry is code. A row is a line in a list, and everything below it moves on
its own.

The first version of `measure.svg` was hand-written, and the reason it is not any
more is not typing effort. A sequence diagram is a grid, so every row's `y`
depends on the height of everything above it; inserting one row by hand means
recomputing four or five coordinates for every row below it, and nothing catches
an arithmetic slip in that -- the file stays valid SVG and simply renders wrong.

The generator also checks its own output, and that is the part worth keeping. It
rejects a note box that crowds the arrow label beneath it, a label long enough to
reach a lifeline its arrow has nothing to do with, a participant labelled wider
than its box, and a label sitting on a frame's dashed edge. Those are the defects
that survive review rather than the ones that don't: 2px of overlap between a box
edge and a line of text reads as tight kerning, and a label with a dashed border
struck through it looks like a font artefact. Both were on screen in renders that
had been looked at twice. Gross errors need no tool -- they are obvious the moment
the image is opened.

Mermaid was the real alternative, and a `mermaid` fence renders on GitHub with no
tooling at all. It was passed over because rendering one to a committed SVG needs
a Node toolchain this repository otherwise has no use for, and an inline fence
cannot be opened on its own, linked to, or scaled -- which these need, being wide.
A mermaid source *and* a generated SVG beside it is two files saying the same
thing where only one renders, so the other drifts. PlantUML is the same trade with
a Java runtime.

What that costs is generality: the generator draws the shapes it has code for and
nothing else. That is deliberate. The moment a third shape is wanted, the honest
move is a real diagramming tool, not growing this one into one.

## Paths inside a diagram are references too

The convention in the root README's §7 -- "a comment that names a file is a link,
and it has to resolve" -- applies to the text inside these files, and
`tools/check_references.py` scans `.svg` for exactly that reason. So

```bash
python3 tools/check_references.py
```

from the repository root will tell you when a diagram still names
`core/verbs/measure.hpp` after that header has moved. Fix it in
`tools/make_diagrams.py` and re-run that -- the `.svg` is generated, so a path
edited in the image itself is undone by the next run. Which is also the weakness
of checking the artifact: the complaint names a line in a file nobody edits, and
a path corrected in the generator is not checked until the SVG is regenerated and
committed. `tools/make_diagrams.py` says why it is done that way round.

Nothing checks that a diagram's *content* still matches the code it describes,
which is the same exposure every README in this tree has, and the same answer:
the diagram names the files it draws, so a reader who doubts it knows where to
look.
