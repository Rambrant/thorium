# No library target here: rig/ holds only this rig's own data (instrument.inc,
# devices.inc, wiring.inc, active_instruments.hpp), not code -- see README.md.
# The same shape dut/CMakeLists.txt has, and for the same reason: a directory of
# data can still have tests, and those tests need a target somewhere.
#
# What lands in this one is the tests that need *this rig* rather than a
# mechanism: more than one instrument at a time, or the rig's own instrument
# globals. They used to sit in libs/hal/tests/, which made hal_tests link
# hal_rig and read as though the generic library owned them; see README.md's
# "tests" section for the file-by-file argument, and instruments/README.md for
# why a test naming two drivers cannot live in either driver's directory.
#
# Linked against hal_rig, not hal, and every one of the five files needs
# something from that half: test_safing.cpp expands THORIUM_ACTIVE_INSTRUMENTS
# and calls hal::safeRig(), and the other four drive the Apply/Connect/Measure
# engines. hal_rig carries the driver targets as PUBLIC link libraries, so the
# driver headers these files include come with it -- nothing to add here.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(rig hal_rig ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
