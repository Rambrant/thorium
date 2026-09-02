# No library target here, exactly as rig/CMakeLists.txt has none: this
# directory holds the dev bench's data -- one instrument, no cards, no wiring --
# and the tests that check it is the bench it says it is.
#
# Reached by add_subdirectory(${THORIUM_RIG_DIR} ...) from the top-level
# CMakeLists.txt, so the target name below is `rig_tests` for whichever
# deployment is selected. One deployment per build directory means the two never
# collide; see the top-level file's own comment on why that is deliberate.
#
# Linked against hal_rig for the same reason the bench rig's tests are: these
# files include hal/topology/active_instruments.hpp and name Dmm1 and
# hal::safeRig().
if(BUILD_TESTING_LAYERS)
    add_layer_tests(rig hal_rig ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    # So test_dev_rig.cpp can reach this deployment's own wiring.inc by a plain
    # repo-root-relative #include, the way rig/tests/test_wiring_uniqueness.cpp
    # reaches the bench's. THORIUM_WIRING_TABLE is PRIVATE to hal_rig and
    # deliberately not the route in -- see framework/hal/CMakeLists.txt.
    target_include_directories(rig_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
