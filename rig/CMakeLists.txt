# No library target here: rig/ holds only this rig's own data (instrument.inc,
# devices.inc, wiring.inc), not code -- see README.md.
# The same shape dut/CMakeLists.txt has, and for the same reason: a directory of
# data can still have tests, and those tests need a target somewhere.
#
# What lands in this one is the tests that need *this rig* rather than a
# mechanism: more than one instrument at a time, or the rig's own instrument
# globals. They used to sit in framework/hal/tests/, which made hal_tests link
# hal_rig and read as though the generic library owned them; see README.md's
# "tests" section for the file-by-file argument, and instruments/README.md for
# why a test naming two drivers cannot live in either driver's directory.
#
# Linked against hal_rig, not hal, and every one of these files needs something
# from that half: test_safing.cpp includes hal/topology/active_instruments.hpp
# and calls hal::safeRig(), test_interlock.cpp includes it and calls
# hal::energisedSourceAt(), and the rest drive the Apply/Connect/Measure
# engines. (No count here on purpose -- the last one went stale the first time a
# file was added.) hal_rig carries the driver targets as PUBLIC link libraries, so the
# driver headers these files include come with it -- nothing to add here.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(rig hal_rig ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    # test_wiring_uniqueness.cpp expands this rig's own wiring.inc -- the only
    # file here that checks the real table rather than building a small local
    # one -- and reaches it by a plain quoted #include, repo-root-relative, the
    # way dut/tests/test_wiring_coverage.cpp reaches the same file. Note it does
    # NOT go through THORIUM_WIRING_TABLE: that definition is PRIVATE to
    # hal_rig, deliberately (see framework/hal/CMakeLists.txt), and a test naming
    # this repo's own rig by path is stating a fact about this directory rather
    # than reaching for a generic mechanism.
    target_include_directories(rig_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
