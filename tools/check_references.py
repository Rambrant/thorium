#!/usr/bin/env python3
"""Every file path named in a comment has to name a file that is there.

This tree documents itself by cross-reference: "see core/verbs/measure.hpp",
"see hal/src/verbs/route.cpp for the same shape", "dut/tests/test_wiring_
coverage.cpp is where the two files are checked against each other". README.md
section 7 makes that a rule -- comments carry the *why*, including the rejected
alternatives -- and the reference is how a reader gets from one half of an
argument to the other. A path that no longer resolves is therefore not a typo
in a comment; it is a broken link in the documentation, and in the generated
reference beside this file it is a broken link in the literal sense.

Which is why this exists rather than being left to review. One directory
reshuffle (moving src/ and tests/ into per-topic subfolders) broke 83 of them
across 50 files at a stroke, and every one of those was invisible: the code
compiled, the tests passed, and nothing said a word until somebody followed a
reference and found nothing at the end of it. A rename is exactly the kind of
edit that does this, and exactly the kind nobody re-greps after.

Run from the repository root:

    python3 tools/check_references.py

Exits non-zero on anything unresolved, so it can gate a CI job.

---------------------------------------------------------------------------
The allowlist, and why there is one rather than a cleverer rule
---------------------------------------------------------------------------

Some comments name a path *on purpose* that is not there -- "include/hal/
ac6834b.hpp, not include/ac6834b.hpp" contrasts against a path that must not
exist, "there used to be a separate rig/instrument_id.inc" is history, and
README.md's recipe for adding a script names the file the reader is about to
write. No pattern separates those from a stale reference, because the
difference is entirely in what the sentence means. So they are listed, one by
one, each with the reason it is there.

An entry that stops being needed is an error too, not a shrug. An allowlist
nobody prunes is how a checker quietly stops checking, and the entries below
are cheap to delete.

Keyed by (file, the reference text) rather than by line number: a reference
survives edits above it, and a line number does not.

---------------------------------------------------------------------------
What this deliberately does not do
---------------------------------------------------------------------------

It does not check that a reference points at something *relevant* -- only that
the file exists. "See core/verbs/trace.hpp" pointing at a real but unrelated
header is a mistake this cannot see, and reading is still the only thing that
catches it.

It does not parse comments. A path-shaped token anywhere in a scanned file is
checked, code and prose alike, which costs nothing here because an #include
resolves the same way a comment reference does.

It has no dependencies beyond the standard library. It once lived beside a
generated-reference (Doxygen) experiment under docs/ and was written to
outlive it; when that experiment was dropped, moving this one file here was
the whole of the migration.
"""
import os
import re
import sys

#
# Scanned for references, and offered as reference targets. Both lists are
# deliberately the same shape of thing -- the files a person writes -- so a
# generated artifact is neither checked nor citable.
#
SCANNED_SUFFIXES = ( '.cpp', '.hpp', '.inc', '.md', '.txt', '.cmake', '.sh')
TARGET_SUFFIXES  = ( 'hpp', 'cpp', 'inc', 'cmake', 'sh', 'md')

#
# Generated output, vendored sources and build trees. Skipped as directories
# rather than filtered afterwards, so a large generated tree costs nothing.
#
SKIPPED_DIRECTORIES = { '.git', '.idea', 'third_party', '__pycache__'}

SKIPPED_PREFIXES = ( 'build', 'cmake-build')

#
# A path-shaped token: at least one directory component, then a filename with
# one of the suffixes above.
#
REFERENCE = re.compile(
    r'\b((?:[A-Za-z0-9_.-]+/)+[A-Za-z0-9_.-]+\.(?:' + '|'.join( TARGET_SUFFIXES) + r'))\b')

#
# Paths that are named on purpose and are not there. See this file's own
# comment above on why these are listed rather than detected.
#
ALLOWED = {
    ( 'CMakeLists.txt', 'ThoriumConfig.cmake/ThoriumConfigVersion.cmake'):
        'prose naming two files at once, not a path',
    ( 'CMakeLists.txt', 'cmake/ThoriumConfig.cmake'):
        'the real name is cmake/ThoriumConfig.cmake.in; the .in is on the line and this is its prefix',
    ( 'README.md', 'suite/scripts/thermal_ramp_script.cpp'):
        'section 3 recipe -- the file the reader is being told to create',
    ( 'framework/core/include/core/verbs/transfer.hpp', 'core/serial.hpp'):
        'names what this file is deliberately NOT called',
    ( 'framework/hal/include/hal/driver/instrument.hpp', 'rig/instrument_id.inc'):
        'history -- "there used to be a separate ..."',
    ( 'framework/runner/CMakeLists.txt', 'VARIANT/test_catalog.inc'):
        'a pattern with a variable in it, not a path',
    ( 'instruments/keysight_ac6834b/CMakeLists.txt', 'include/keysight_ac6834b.hpp'):
        'contrast -- "include/hal/ac6834b.hpp, not include/ac6834b.hpp"',
    ( 'instruments/keysight_dso8064a/CMakeLists.txt', 'include/keysight_dso8064a.hpp'):
        'contrast -- "include/hal/dso8064a.hpp, not include/dso8064a.hpp"',
    ( 'instruments/keysight_dsox1202g/CMakeLists.txt', 'include/keysight_dsox1202g.hpp'):
        'contrast -- "include/hal/dsox1202g.hpp, not include/dsox1202g.hpp"',
    ( 'instruments/keysight_edu34450a/CMakeLists.txt', 'include/keysight_edu34450a.hpp'):
        'contrast -- "include/hal/edu34450a.hpp, not include/edu34450a.hpp"',
    ( 'instruments/keysight_l4411a/CMakeLists.txt', 'include/keysight_l4411a.hpp'):
        'contrast -- "include/hal/l4411a.hpp, not include/l4411a.hpp"',
    ( 'instruments/keysight_n6701a/CMakeLists.txt', 'include/keysight_n6701a.hpp'):
        'contrast -- "include/hal/n6701a.hpp, not include/n6701a.hpp"',
    ( 'instruments/racal1260/CMakeLists.txt', 'include/racal1260.hpp'):
        'contrast -- "include/hal/racal1260.hpp, not include/racal1260.hpp"',
    ( 'rig/instrument.inc', 'InstrumentWiring/wiring.inc'):
        'prose pairing a type with a file, not a path',

    #
    # The three below are real gaps rather than deliberate spellings, listed so
    # this check can be green while they are outstanding. Each is a decision
    # somebody has to make, not an edit this file should hide.
    #
    ( 'dsl/CMakeLists.txt', 'src/test_case.cpp'):
        'the dsl/ layer is commented-out scaffolding -- delete the directory or write the file',
    ( 'dsl/CMakeLists.txt', 'tests/test_test_case.cpp'):
        'the dsl/ layer is commented-out scaffolding -- delete the directory or write the file',
    ( 'dut/criteria_production.inc', 'scripts/include/scripts/fuse_register_criteria.hpp'):
        'names scaffolding that has never existed in this repo -- repoint or drop the sentence',
}


def repository_files( root):
    """Every hand-written file under root, as repository-relative posix paths."""
    found = []

    for directory, subdirectories, names in os.walk( root):
        subdirectories[:] = [
            name for name in subdirectories
            if name not in SKIPPED_DIRECTORIES and not name.startswith( SKIPPED_PREFIXES)]

        for name in names:
            relative = os.path.relpath( os.path.join( directory, name), root)

            found.append( relative.replace( os.sep, '/'))

    return found


def unresolved( files, existing):
    """Every (file, line, reference) whose reference names nothing that is there.

    A reference resolves four ways, all of them spellings a reader would follow
    successfully: repository-relative, relative to the file it is written in,
    or -- the one worth spelling out -- as the tail of a real path. That last
    one is what makes 'hal/src/verbs/measure.cpp' resolve from inside
    framework/, which is how the framework refers to itself so that the
    references still work in the zip it is distributed as.
    """
    findings = []

    for path in sorted( files):
        if not path.endswith( SCANNED_SUFFIXES):
            continue

        with open( path, encoding = 'utf-8', errors = 'replace') as source:
            lines = source.read().split( '\n')

        directory = os.path.dirname( path)

        for number, line in enumerate( lines, 1):
            for match in REFERENCE.finditer( line):
                reference = match.group( 1)

                beside = os.path.normpath( os.path.join( directory, reference)).replace( os.sep, '/')

                if reference in existing or beside in existing:
                    continue

                if any( real.endswith( '/' + reference) for real in existing):
                    continue

                findings.append( ( path, number, reference))

    return findings


def main():
    root = os.path.dirname( os.path.dirname( os.path.abspath( __file__)))

    files    = repository_files( root)
    existing = set( files)

    os.chdir( root)

    findings = unresolved( files, existing)

    unexpected = [ f for f in findings if ( f[ 0], f[ 2]) not in ALLOWED]
    seen       = { ( path, reference) for path, _number, reference in findings}
    withered   = sorted( key for key in ALLOWED if key not in seen)

    for path, number, reference in unexpected:
        print( f'{path}:{number}: names {reference}, which is not in the tree')

    for path, reference in withered:
        print( f'{path}: allowlisted {reference} now resolves -- drop its entry in '
               f'tools/check_references.py')

    if unexpected or withered:
        print()

        sys.exit( f'{len( unexpected)} broken reference(s), '
                  f'{len( withered)} stale allowlist entr(y/ies)')

    print( f'every file reference resolves '
           f'({len( existing)} files scanned, {len( ALLOWED)} allowlisted)')


if __name__ == '__main__':
    main()
