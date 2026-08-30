# docs/api/ — the generated reference

An **experiment**, not a commitment. Four files, no source changes anywhere:
deleting this directory and its `.gitignore` entry removes it completely.

```bash
docs/api/build.sh                # -> docs/api/html/index.html
```

That checks every cross-reference in the tree and then runs Doxygen, refusing
to publish a reference whose links were already broken on disk — see
[the reference check](#the-reference-check) below. `doxygen docs/api/Doxyfile`
on its own still works and skips the check, as does `build.sh --skip-check`.

The output is generated, so it isn't committed — only the `Doxyfile`, the
filter, the check and the wrapper are.

## What it does, and the one trick in it

Doxygen only treats `///`, `//!` and `/** */` as documentation. Every comment in
this tree is a plain `//` block, so `EXTRACT_ALL = YES` on its own yields a
complete index of entities with no prose attached to any of them.

`comment_filter.py` bridges that as an `INPUT_FILTER`: it rewrites whole-line
`//` comments to `///` in the stream Doxygen parses. Nothing on disk changes,
and the source browser deliberately shows the unfiltered file
(`FILTER_SOURCE_FILES = NO`).

## The reference check

`check_references.py` is the one part of this directory that would be worth
keeping even if the rest is dropped, which is why it has no Doxygen dependency
and would move to `tools/` unchanged.

It asserts that every path named in a comment — `see core/verbs/measure.hpp`,
`see hal/src/verbs/route.cpp` — names a file that exists. That matters here
more than in most trees, because [README.md](../../README.md) §7 makes the
cross-reference the mechanism by which a decision's *why* is reachable from
the code it explains, and because Doxygen renders those comments verbatim: a
path that no longer resolves is a dead end in the source and in the generated
output both.

It exists because one directory reshuffle broke 83 of them across 50 files at
a stroke, silently — the build was green throughout.

A path resolves if it is repository-relative, relative to the file naming it,
or the tail of a real path. That last spelling is what lets `framework/` refer
to itself as `hal/src/verbs/measure.cpp`, so the references still work inside
the zip it ships as.

Paths that are deliberately absent — a driver's `include/hal/<driver>.hpp`
contrasted against the `include/<driver>.hpp` it is deliberately *not*; a
recipe naming the file you are about to write — are allowlisted one by one
with the reason. An allowlist entry that starts
resolving is an error too, so the list cannot rot into a checker that no longer
checks. Three entries currently record real gaps rather than deliberate
spellings; the file says which.

## What to judge it on

The prose renders — `core::ISession`'s essay arrives intact as its Detailed
Description. So the question is not whether it works but whether it earns its
keep, and the honest case against is that the headers already say all of this,
in an order chosen by a writer rather than by a class index, and that a test
writer's documentation is [README.md](../../README.md) and the deck in
[docs/](../README.md) instead.

The half a header genuinely cannot give you is navigation: cross-references,
who-calls-what, the inheritance and collaboration graphs. That is what
`SOURCE_BROWSER`/`REFERENCED_BY_RELATION`/`HAVE_DOT` are on for, and the part
worth a verdict.

## Known gaps

- **Macro-generated entities do not appear as entities.** `INSTRUMENT_WIRING`
  and its two siblings expand to functions and variables (see
  [hal/topology/wiring.hpp](../../framework/hal/include/hal/topology/wiring.hpp)); Doxygen documents
  the macro, not what it produces, so `hal::instrumentWiring`,
  `isInstrumentWired()` and the three builders exist here only as macro body
  text. Fixing it needs `MACRO_EXPANSION`/`EXPAND_AS_DEFINED` tuning,
  deliberately not done — the tuning is exactly the maintenance burden this
  experiment is meant to price.
- **`tests/` is excluded**, though those files carry as much rationale as the
  headers do.
- ~80 lines in `doxygen.warn`, all benign: ambiguous `to_string` overload
  references and unresolved `\link`-looking words in prose. The C++26
  reflection headers parse without complaint.
