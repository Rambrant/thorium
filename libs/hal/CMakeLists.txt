add_library(hal STATIC
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/wiring.cpp
    src/measure.cpp
    src/apply.cpp
    src/safing.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/active_instruments.hpp    # header-only
    include/hal/wiring.hpp         # header-only
    include/hal/adapter.hpp        # header-only
    include/hal/measure.hpp        # header-only
    include/hal/apply.hpp          # header-only
    include/hal/safing.hpp         # header-only
    include/hal/n6701a.hpp         # header-only
    include/hal/ac6677a.hpp        # header-only
    include/hal/l4411a.hpp         # header-only
    include/hal/dso8064.hpp        # header-only
    wiring.inc                     # content only; not compiled directly, see README.md
    instrument.inc                 # content only; not compiled directly, see README.md
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal PUBLIC core)

# src/measure.cpp/src/apply.cpp #include wiring.inc (this rig's fixed wiring
# data), and include/hal/active_instruments.hpp #includes instrument.inc (this
# rig's fixed instrument list), via a repo-root-relative path, the same
# convention libs/dut/*.inc already use -- see wiring.inc's own comment.
# src/safing.cpp #includes instrument.inc a second time, with INSTRUMENT
# redefined to safe each instance rather than declare it -- see that file.
target_include_directories(hal PRIVATE ${CMAKE_SOURCE_DIR})

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_wiring.cpp
        tests/test_source_instruments.cpp
        tests/test_call_syntax.cpp
        tests/test_safing.cpp
    )

    # tests/test_safing.cpp is the first hal test to drive this rig's actual
    # global instruments rather than locals of its own -- hal::safeRig()
    # takes no arguments, so there is no substitute rig to hand it (see
    # hal/safing.hpp). That means including hal/active_instruments.hpp,
    # which reaches instrument.inc by a repo-root-relative path, so this
    # test executable needs the same include directory the hal library
    # itself does above. Same reason scripts_tests takes it in
    # app/CMakeLists.txt.
    target_include_directories(hal_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
