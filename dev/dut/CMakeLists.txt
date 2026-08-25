# No library target, matching dut/CMakeLists.txt: this directory holds the dev
# deployment's DUT data -- which is an empty adapter and one criteria table --
# and the tests over it.
#
# Reached by add_subdirectory(${THORIUM_DUT_DIR} ...) from the top-level
# CMakeLists.txt, so the target is `dut_tests` for whichever deployment is
# selected.
#
# hal, not hal_rig, exactly as the bench deployment's dut_tests: what these
# files need is hal::Adapter and the wiring machinery, not an instrument global.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut hal ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_include_directories(dut_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
