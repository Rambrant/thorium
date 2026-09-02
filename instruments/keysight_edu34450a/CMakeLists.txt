# --- hal::keysight_edu34450a::EDU34450A -- Keysight EDU34450A 5.5-digit DMM ---
# One instrument driver, self-contained: its header, its tests, its README, and
# this file. Nothing outside this directory names any of them -- the tree above
# discovers this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can
# be zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# STATIC, and the first driver here that is -- which is exactly what the
# previous version of this comment said would happen: "a driver that grows a
# real .cpp (talking to a VISA library, say) becomes STATIC here and nothing
# outside this file needs to know."
#
# Every other driver in this tree is still INTERFACE and still correct to be.
# The rule has not changed: an instrument driver is a type whose Port-returning
# members are consumed as types by the call site (see core/driver/port.hpp), so
# those have to stay visible in the header, and they have. What moved into
# src/keysight_edu34450a.cpp is the half that is *not* a template -- the SCPI
# command table, the range and resolution rules, the overload check -- because
# none of it depends on which quantity type the answer gets wrapped in. See
# that file's own comment.
#
# Nothing outside this file needed to know, as promised: framework/hal's
# foreach over THORIUM_INSTRUMENT_TARGETS links whatever target this directory
# registers, and rig/, dev/ and the suite name none of it.
add_library(hal_keysight_edu34450a STATIC
    src/keysight_edu34450a.cpp
    include/hal/keysight_edu34450a.hpp   # header-only apart from the above
)

# include/hal/keysight_edu34450a.hpp, not include/keysight_edu34450a.hpp: this
# driver keeps its "hal/keysight_edu34450a.hpp" spelling at every call site. It
# *is* a hal driver -- it implements hal's instrument API and is useless without
# it -- so it stays under hal/ on the include path even though its own
# declarations live in the nested namespace hal::keysight_edu34450a.
#
# The directory, the header and the namespace all carry the same name, and it
# carries the manufacturer as well as the model: a model number is not unique
# across vendors, so a bare model name is a collision waiting for the second
# vendor to ship one.
#
# PUBLIC rather than INTERFACE now that this target has a translation unit of
# its own: src/keysight_edu34450a.cpp includes "hal/keysight_edu34450a.hpp"
# through this same path, so the directory has to reach the target's own compile
# as well as its consumers'.
target_include_directories(hal_keysight_edu34450a PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the hardware
# abstraction layer and nothing else. That is the whole point of the two-target
# split -- see framework/hal/CMakeLists.txt's own comment on it, and on the
# explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
target_link_libraries(hal_keysight_edu34450a PUBLIC Thorium::hal)

add_library(Thorium::hal_keysight_edu34450a ALIAS hal_keysight_edu34450a)

# How framework/hal learns this directory's target name, rather than deriving
# one from the directory name -- see its own comment on the property. Appended,
# so a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_keysight_edu34450a)

install(TARGETS hal_keysight_edu34450a
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    # Linked against this driver alone -- which transitively means hal and core
    # and nothing else. No hal_rig, no rig data, no other driver. That is worth
    # more than it looks: it is the build refusing to let this driver's tests
    # quietly start depending on some other instrument or on this rig's wiring,
    # which is exactly what would make the directory no longer packageable on
    # its own. If a test here ever genuinely needs the fabric or an Apply, it
    # belongs in rig/tests/ instead -- see rig/README.md.
    add_layer_tests(keysight_edu34450a hal_keysight_edu34450a ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
