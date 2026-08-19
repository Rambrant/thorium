#
# Makes Doxygen see the comments this codebase actually has.
#
# Doxygen only treats ///, //! and /** */ as documentation; a plain // block is
# invisible to it. Every comment in this tree is a plain // block, so without
# this filter an EXTRACT_ALL run produces a complete index of entities with no
# prose attached to any of them.
#
# Run via INPUT_FILTER, so it rewrites only the stream Doxygen reads -- no file
# on disk is touched, and nothing here is a source change. Deleting this file
# and the Doxyfile beside it removes the whole experiment.
#
# Only whole-line comments are converted: a line whose first non-blank
# characters are //. Trailing comments after code are left alone, since /// on
# that position would document the *next* declaration rather than that line.
#
import sys

with open( sys.argv[ 1], encoding = 'utf-8', errors = 'replace') as source:
    for line in source:
        stripped = line.lstrip()

        if stripped.startswith( '//') and not stripped.startswith( ('///', '//!')):
            indent = line[ : len( line) - len( stripped)]

            sys.stdout.write( indent + '///' + stripped[ 2:])
        else:
            sys.stdout.write( line)
