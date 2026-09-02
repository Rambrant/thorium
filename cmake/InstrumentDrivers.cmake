#
# Generates the driver-header include list that
# hal/topology/active_instruments.hpp opens before it expands the rig's
# instrument table.
#
# That header declares the rig's instrument globals from rig/instrument.inc,
# so the driver headers behind the table's type column have to be visible
# first -- and the preprocessor cannot produce them from the table itself: a
# macro expansion cannot emit an #include directive, and the table is read a
# second time by hal/driver/instrument.hpp (inside plain hal, which every
# driver depends on) where those headers must NOT appear at all.
#
# The alternative was a hand-written list beside the table -- six #include
# lines in rig/, one in dev/rig/ -- which is a second statement of what the
# type column already says. It could not desync silently (a missing header is
# a compile error on the row that needs it), but it was still a line to add
# with the row and a line to forget when the row leaves. The type column is
# already the single reviewable list of what this rig has; this file makes it
# the only one.
#
# Same shape as cmake/CriteriaVariants.cmake beside it, and for the same
# reason: one list, validated once, reaching the compiler as a generated file
# rather than as something a person keeps in step by hand.
#

#
# The convention this reads, stated once: a driver package named <ns> holds
# include/hal/<ns>.hpp and declares its types in namespace hal::<ns> (see
# instruments/README.md and any driver's own CMakeLists.txt, which says the
# same thing from the other side). So the namespace qualifier on a row's type
# column -- keysight_n6701a in keysight_n6701a::Direct -- names the header
# that row needs, and nothing else in the row has to.
#
# That is also why the type column is qualified at all rather than relying on
# a "using namespace" somewhere: it is the one token that says which driver a
# row is, and it is worth reading as such.
#
set(THORIUM_INSTRUMENT_DRIVER_HEADER_FORMAT "hal/<namespace>.hpp")

#
# file(WRITE) unconditionally rewrites, which updates the mtime whether or not
# anything changed -- and this is a file every rig-coupled translation unit
# #includes. Compared against what's on disk first, so a no-op reconfigure is
# genuinely a no-op for the build. (The twin of cmake/CriteriaVariants.cmake's
# own _thorium_write_if_changed; duplicated rather than shared because the two
# modules are otherwise independent and neither includes the other.)
#
function(_thorium_write_drivers_if_changed path content)
    set(existing "")

    if(EXISTS "${path}")
        file(READ "${path}" existing)
    endif()

    if(NOT existing STREQUAL "${content}")
        file(WRITE "${path}" "${content}")
        message(STATUS "Generated: ${path}")
    endif()
endfunction()

#
# instrumentTable: the rig's instrument.inc (THORIUM_INSTRUMENT_TABLE)
# instrumentDirs:  the driver package directories (THORIUM_INSTRUMENT_DIRS)
# outputDir:       where to write the generated header
#
# Sets THORIUM_INSTRUMENT_DRIVERS in the caller's scope, holding the absolute
# path of the generated file.
#
function(thorium_generate_instrument_drivers instrumentTable instrumentDirs outputDir)
    #
    # Only lines that actually invoke the macro. The REGEX pass is a coarse
    # filter -- it also catches the file's own prose, which mentions
    # INSTRUMENT(type, id, address, ...) more than once -- so each candidate is
    # stripped and re-checked below, where a comment line starts with // and
    # falls out.
    #
    file(STRINGS "${instrumentTable}" candidateLines REGEX "INSTRUMENT\\(")

    set(namespaces "")

    foreach(line IN LISTS candidateLines)
        string(STRIP "${line}" line)

        if(NOT line MATCHES "^INSTRUMENT\\(")
            continue()
        endif()

        #
        # The type column, up to its namespace qualifier. Checked rather than
        # assumed: an unqualified type (INSTRUMENT( L4411A, ...)) names no
        # driver package, so there is no header to derive and no honest guess
        # to make. Failing here says which row and what the row should have
        # said; failing later is a missing-include error inside a generated
        # file, pointing at neither.
        #
        if(NOT line MATCHES "^INSTRUMENT\\([ ]*([A-Za-z_][A-Za-z0-9_]*)::")
            message(FATAL_ERROR
                "${instrumentTable}: cannot tell which driver this row needs:\n"
                "    ${line}\n"
                "An INSTRUMENT row's type column must be qualified with its driver's "
                "namespace (keysight_n6701a::Direct, not Direct) -- that qualifier is "
                "what names the header the row compiles against "
                "(${THORIUM_INSTRUMENT_DRIVER_HEADER_FORMAT}).")
        endif()

        list(APPEND namespaces "${CMAKE_MATCH_1}")
    endforeach()

    if(NOT namespaces)
        message(FATAL_ERROR
            "${instrumentTable} declares no instruments: no INSTRUMENT( ...) row found. "
            "A rig with no instruments at all is not a rig this framework can build -- "
            "see rig/README.md. (dev/rig/instrument.inc is the smallest real one: a "
            "single meter.)")
    endif()

    # First-appearance order, which REMOVE_DUPLICATES preserves. Four DcP rows
    # share one driver and so one #include; the table's own order is what
    # decides where that include lands.
    list(REMOVE_DUPLICATES namespaces)

    set(includes "")

    foreach(driverNamespace IN LISTS namespaces)
        #
        # That the package is actually configured into this build, checked
        # against the directories the rig supplied rather than against a target
        # name derived from the namespace -- a driver package names its own
        # target (see framework/hal/CMakeLists.txt on the
        # THORIUM_INSTRUMENT_TARGETS property) and this must not start guessing
        # it back. The header path is the convention that is genuinely fixed.
        #
        # What this catches is the deployment mistake that actually happens: a
        # row naming a driver that THORIUM_INSTRUMENT_PACKAGES leaves out of
        # this build, which is otherwise a missing-include error in a generated
        # file naming neither the row nor the package list.
        #
        set(found "")

        foreach(instrumentDir IN LISTS instrumentDirs)
            if(EXISTS "${instrumentDir}/include/hal/${driverNamespace}.hpp")
                set(found "${instrumentDir}")
                break()
            endif()
        endforeach()

        if(NOT found)
            message(FATAL_ERROR
                "${instrumentTable} names driver '${driverNamespace}', but no configured "
                "instrument package provides include/hal/${driverNamespace}.hpp.\n"
                "Looked in: ${instrumentDirs}\n"
                "Either the package is missing from instruments/, or "
                "THORIUM_INSTRUMENT_PACKAGES excludes it from this build.")
        endif()

        string(APPEND includes "#include \"hal/${driverNamespace}.hpp\"\n")
    endforeach()

    #
    # Quoted "hal/<ns>.hpp", not the absolute path the check above resolved:
    # the include has to keep the spelling every call site uses, and it is
    # found the same way -- through the driver targets' own interface include
    # directories, which hal_rig carries. The absolute path is for the error
    # message, not for the build.
    #
    set(content
"//
// GENERATED by cmake/InstrumentDrivers.cmake -- do not edit.
//
// One #include per distinct driver named in the type column of
//
//     ${instrumentTable}
//
// in the order that table first names them. Regenerated on every reconfigure,
// and the table is a configure dependency, so editing a row is enough --
// there is no second list to keep in step.
//
// Read by hal/topology/active_instruments.hpp through
// THORIUM_INSTRUMENT_DRIVERS, immediately before it expands that same table
// into this rig's instrument globals.
//
${includes}")

    file(MAKE_DIRECTORY "${outputDir}")

    set(driversPath "${outputDir}/instrument_drivers.hpp")

    _thorium_write_drivers_if_changed("${driversPath}" "${content}")

    set(THORIUM_INSTRUMENT_DRIVERS "${driversPath}" PARENT_SCOPE)
endfunction()
