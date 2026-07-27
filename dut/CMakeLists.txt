# No library target here: dut/ holds only DUT-specific data (device_x_profile.inc,
# criteria_*.inc), not code -- see README.md. The tests below exercise that
# data directly against hal's generic mechanism (hal::Adapter etc), so they
# link against hal (which pulls in core transitively) rather than a "dut"
# library that no longer exists.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut hal
        tests/test_device_x_profile.cpp
    )

    # test_device_x_profile.cpp reaches dut/device_x_profile.inc by a plain
    # quoted #include, repo-root-relative.
    target_include_directories(dut_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
