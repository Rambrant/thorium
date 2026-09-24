# Makes header-only cpp-httplib available as httplib::httplib, for
# framework/console -- see framework/console/README.md's "History", which
# checked (rather than assumed) that cpp-httplib compiles clean under this
# project's own compiler and flags, -freflection -fcontracts included.
#
# Vendored into third_party/, the same as GoogleTest (see FetchGTest.cmake)
# and for a related but not identical reason. GoogleTest has to be vendored:
# its compiled .a would otherwise come from vcpkg's compiler, with an ABI
# (std::string) this project's GCC cannot link against. cpp-httplib has no
# such problem -- it is a single header, compiled by whichever translation
# unit includes it, so there is no prebuilt-elsewhere ABI to clash with. It is
# vendored anyway so that configuring framework/console touches the network
# never, matching the property framework/ already has (see FetchGTest.cmake's
# own note on that).
#
# No CMakeLists.txt of its own to add_subdirectory -- cpp-httplib does not
# ship a build, it ships a header -- so this defines the INTERFACE target
# directly rather than going through FetchContent_MakeAvailable.
#
# Guarded rather than assumed single-include: unlike FetchGTest.cmake (one
# include(), gated behind the top-level BUILD_TESTING_LAYERS option),
# framework/console is the only consumer today, so it includes this itself
# rather than relying on the top-level file to have done so first. A second
# add_subdirectory that also wants httplib::httplib will include this a
# second time, and add_library would otherwise refuse the redefinition.
if(TARGET httplib::httplib)
    return()
endif()

get_filename_component(THORIUM_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(httplib INTERFACE)
add_library(httplib::httplib ALIAS httplib)
target_include_directories(httplib INTERFACE
    "${THORIUM_ROOT}/third_party/cpp-httplib-0.18.5"
)

# Plain HTTP only -- this server never binds anything but 127.0.0.1 (see
# framework/console/README.md), so TLS buys nothing and OpenSSL/zlib are not
# dependencies this tree needs to acquire on three platforms for it.
#
# That means NOT defining CPPHTTPLIB_OPENSSL_SUPPORT / CPPHTTPLIB_ZLIB_SUPPORT
# at all, deliberately -- every guard in httplib.h is #ifdef, not #if, so
# defining either one to 0 to be explicit about "off" would turn it *on* and
# then fail to find OpenSSL/zlib. Absence is the only spelling of "off" this
# header understands.
