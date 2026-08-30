#!/usr/bin/env bash
#
# Build the generated reference, refusing to build it out of broken links.
#
# Usage: docs/api/build.sh          (from anywhere -- it finds the repo root)
#
# Two steps, and the order is the whole reason this script exists rather than
# the bare `doxygen docs/api/Doxyfile` the README used to give:
#
#   1. check_references.py -- every file path named in a comment names a file
#      that is there. This tree cross-references itself constantly ("see
#      core/verbs/measure.hpp"), Doxygen renders those comments verbatim, and a
#      path that no longer resolves becomes a dead end in the output. Running
#      it first means the reference is never published with links that were
#      already broken on disk.
#
#   2. doxygen itself.
#
# The check is a separate, dependency-free script rather than something wired
# into the Doxyfile, because Doxygen has no pre-run hook and because the check
# is worth more than this experiment is: it guards the comments, which are the
# documentation whether or not anything generates HTML from them. If docs/api/
# is ever dropped (see README.md beside this file), check_references.py moves
# to tools/ and this wrapper goes away with the rest.
#
# --skip-check exists for the one honest case -- wanting to *look at* the
# reference while references are mid-repair. It is not for CI.
#
set -eu

cd "$( dirname "$0")/../.."

SKIP_CHECK=no

for arg in "$@"; do
    case "$arg" in
        --skip-check) SKIP_CHECK=yes ;;
        *) echo "usage: docs/api/build.sh [--skip-check]" >&2; exit 2 ;;
    esac
done

if [ "$SKIP_CHECK" = no ]; then
    python3 docs/api/check_references.py
fi

if ! command -v doxygen >/dev/null 2>&1; then
    echo "doxygen is not installed -- the reference check above still ran." >&2
    exit 1
fi

doxygen docs/api/Doxyfile

echo "reference written to docs/api/html/index.html ($( wc -l < docs/api/doxygen.warn | tr -d ' ') warnings in docs/api/doxygen.warn)"
