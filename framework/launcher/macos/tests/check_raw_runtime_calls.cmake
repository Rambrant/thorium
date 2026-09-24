#
# The signature test (test_signatures.cpp) checks every entry in appkit.hpp.
# That is only the whole of the launcher's Objective-C if every message goes
# through an entry -- so this fails if any launcher source outside objc.hpp /
# objc.cpp sends a message, names a selector or looks up a class by string
# itself. A call made that way would be exactly the unchecked kind the
# signature test exists to rule out. It also bans <objc/objc.h>'s BOOL, which
# is the wrong type under GCC -- see below.
#
# Run by ctest as `cmake -DSOURCE_DIR=<macos/> -P check_raw_runtime_calls.cmake`.
#
if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.hpp")

set(allowed "${SOURCE_DIR}/objc.hpp" "${SOURCE_DIR}/objc.cpp")
set(forbidden "objc_msgSend|sel_registerName|sel_getUid|objc_getClass|NSSelectorFromString|NSClassFromString")
# And <objc/objc.h>'s BOOL, YES and NO, which are signed char under GCC where
# the arm64 ABI says bool -- see objc.hpp's Bool. objc::Bool/kYes/kNo instead.
string(APPEND forbidden "|(^|[^A-Za-z_])(BOOL|YES|NO)([^A-Za-z_]|$)")

set(offences "")
foreach(source IN LISTS sources)
    if(source IN_LIST allowed)
        continue()
    endif()

    file(STRINGS "${source}" lines REGEX "${forbidden}")
    foreach(line IN LISTS lines)
        # Comments may name them -- this file's own neighbours explain why
        # they are not called.
        string(STRIP "${line}" stripped)
        if(stripped MATCHES "^//")
            continue()
        endif()
        get_filename_component(name "${source}" NAME)
        string(APPEND offences "  ${name}: ${stripped}\n")
    endforeach()
endforeach()

if(offences)
    message(FATAL_ERROR
        "Raw Objective-C runtime calls outside objc.hpp -- route them through an "
        "appkit.hpp entry so the signature test checks them:\n${offences}")
endif()

list(LENGTH sources count)
message(STATUS "${count} launcher sources checked: every message goes through appkit.hpp")
