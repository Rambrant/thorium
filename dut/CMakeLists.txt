# No library target here: dut/ holds only DUT-specific data (adapter.inc,
# criteria_*.inc), not code -- see README.md. The tests below exercise that
# data directly against hal's generic mechanism (hal::Adapter etc), so they
# link against hal (which pulls in core transitively) rather than a "dut"
# library that no longer exists.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut hal
        tests/test_adapter.cpp
        tests/test_wiring_coverage.cpp
    )

    # test_adapter.cpp reaches dut/adapter.inc by a plain quoted #include,
    # repo-root-relative -- test_wiring_coverage.cpp reaches rig/wiring.inc
    # the same way, alongside it.
    target_include_directories(dut_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
