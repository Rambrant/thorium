# docs/api/ — the generated reference

An **experiment**, not a commitment. Two files, no source changes anywhere:
deleting this directory and its `.gitignore` entry removes it completely.

```bash
doxygen docs/api/Doxyfile        # from the repository root -> docs/api/html/index.html
```

The output is generated, so it isn't committed — only the `Doxyfile` and the
filter are.

## What it does, and the one trick in it

Doxygen only treats `///`, `//!` and `/** */` as documentation. Every comment in
this tree is a plain `//` block, so `EXTRACT_ALL = YES` on its own yields a
complete index of entities with no prose attached to any of them.

`comment_filter.py` bridges that as an `INPUT_FILTER`: it rewrites whole-line
`//` comments to `///` in the stream Doxygen parses. Nothing on disk changes,
and the source browser deliberately shows the unfiltered file
(`FILTER_SOURCE_FILES = NO`).

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
  [hal/wiring.hpp](../../framework/hal/include/hal/wiring.hpp)); Doxygen documents
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
