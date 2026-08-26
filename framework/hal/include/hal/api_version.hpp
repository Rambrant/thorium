#pragma once

//
// The version of the driver-facing hal API, and the gate a driver uses to say
// which version it was written against.
//
// An instrument driver under instruments/ is meant to be zipped, versioned and
// dropped into another rig's instruments/ as it stands (see
// instruments/README.md) -- which means a driver and the hal it is compiled
// against travel separately and can disagree. Without a gate, that disagreement
// surfaces as a failure somewhere deep inside a template instantiation: the
// driver names a member of a hal type that has since changed shape, and the
// error arrives from the middle of core::MeasureEngine with a page of
// substituted template arguments in front of it and nothing in it saying
// "wrong hal version". One THORIUM_REQUIRE_HAL_API line per driver turns that
// into a single readable diagnostic at the top of the driver's own header.
//
// Two numbers, not one, and the second is what keeps the gate from being a
// nuisance. THORIUM_HAL_API_VERSION rises on every change to the driver-facing
// API, additions included; THORIUM_HAL_API_OLDEST_SUPPORTED rises only when a
// change actually breaks drivers written against older versions. A driver
// asking for 1 against a hal at 5 that still supports 1 compiles, silently and
// correctly, which is the whole point -- a gate that failed on every additive
// bump would be turned off within a week. The pair spans the range this hal
// claims to serve, and a driver's number is one point that either falls in it
// or does not:
//
//     wanted > THORIUM_HAL_API_VERSION          this hal is too old for the driver
//     wanted < THORIUM_HAL_API_OLDEST_SUPPORTED this hal has dropped what the driver uses
//
// A macro pair rather than only the two constants below, because the version
// has to be answerable by the preprocessor. A driver spanning two hal versions
// writes `#if THORIUM_HAL_API_VERSION >= 3` around the part that differs, and
// an `inline constexpr int` cannot be asked that question. An integer rather
// than a semver string for the same reason: the preprocessor can compare it.
//
// Defined here in a header rather than handed in as a compile definition from
// framework/hal/CMakeLists.txt, unlike THORIUM_INSTRUMENT_TABLE and its three
// siblings -- and the difference is not stylistic. Those four are facts about
// the *linking rig*, which only the build knows. This is a fact about *these
// headers*, so it has to travel with them: a hal consumed as installed headers
// on an include path, with no Thorium::hal target in sight, still answers the
// question, and no stray -D can make the number disagree with the source it
// describes.
//
// The driver declares the number and hal checks it, rather than hal holding a
// list of known drivers and their versions. hal must not know its drivers --
// that is the two-target split's entire purpose (see this directory's
// CMakeLists.txt) -- and more practically, a dropped-in zip has to carry its
// own claim. A list here could only ever describe the drivers that were in the
// tree when it was written.
//
// Nothing derives these two numbers or checks them against the API they
// describe; they are a promise this library makes, kept by whoever changes a
// driver-facing header remembering to bump them. That is a real weakness and
// worth stating plainly rather than dressing up: the gate catches a driver and
// a hal that were honestly labelled and still don't match, which is the
// zip-distribution case it exists for. It cannot catch a breaking change that
// went in without a bump.
//
// Version log -- what a driver's number means:
//
//     1  The API as it stands when this gate was introduced: hal/address.hpp
//        (Gpib/Lan/Serial/Usb/Simulated, ReachableOver), hal/instrument.hpp
//        (InstrumentId, InstrumentTag), hal/wiring.hpp, hal/switch_device.hpp,
//        hal/switch_fabric.hpp, hal/bundle.hpp, hal/describe.hpp, and the
//        core/ API those reach. The five drivers in instruments/ are all at 1.
//
// A driver written today asks for THORIUM_HAL_API_VERSION's current value, as a
// literal -- never the macro itself, which would assert that this hal is
// compatible with this hal and pass everywhere.
//
#define THORIUM_HAL_API_VERSION           1
#define THORIUM_HAL_API_OLDEST_SUPPORTED  1

namespace hal
{
    //
    // The same two numbers as values. Defined from the macros, so the macros
    // stay the single source and the two spellings cannot drift -- and the
    // test in tests/test_api_version.cpp checks even that.
    //
    inline constexpr int kApiVersion                = THORIUM_HAL_API_VERSION;
    inline constexpr int kOldestSupportedApiVersion = THORIUM_HAL_API_OLDEST_SUPPORTED;

    //
    // The two halves of the gate, and their conjunction. THORIUM_REQUIRE_HAL_API
    // below is written in terms of the first two rather than repeating the
    // comparisons, which is the point of their existing at all: a static_assert
    // that fires prevents the translation unit holding it from compiling, so a
    // failing gate can never appear in a test suite that has to build. Asking
    // the same functions the macro asks is as close to testing the gate as a
    // test can get; a test that re-spelled `wanted <= kApiVersion` would only
    // be testing its own copy.
    //
    // Two predicates rather than one because the macro needs the halves
    // separate to give its two distinct messages -- see its own comment. The
    // conjunction is for everyone else, who wants the one answer.
    //
    // constexpr, not consteval, even though every use in this codebase is a
    // static_assert and so a constant expression already. consteval would
    // forbid the runtime call, and the runtime call is what lets the test walk
    // the whole claimed range instead of poking its two endpoints.
    //

    //
    // Has this hal reached the version the driver was written against? False
    // means the hal is the older of the two.
    //
    constexpr auto hasReachedApiVersion( const int writtenAgainst) -> bool
    {
        return writtenAgainst <= kApiVersion;
    }

    //
    // Does this hal still serve that version? False means the driver is the
    // older of the two, by more than this hal kept compatibility for.
    //
    constexpr auto stillSupportsApiVersion( const int writtenAgainst) -> bool
    {
        return writtenAgainst >= kOldestSupportedApiVersion;
    }

    constexpr auto apiVersionSupported( const int writtenAgainst) -> bool
    {
        return hasReachedApiVersion( writtenAgainst) && stillSupportsApiVersion( writtenAgainst);
    }
}

//
// One line in each driver's header, immediately after its hal includes:
//
//     THORIUM_REQUIRE_HAL_API( 1);
//
// Two asserts rather than one, so each direction says which side is behind --
// "update hal" and "update the driver" are different instructions, and one
// combined message would leave the reader to work out which they are being
// given. Neither message can quote the numbers (a static_assert message is a
// string literal, not a format), so both name the file to read them in.
//
// The third possible failure needs no assert and is already clear: a driver
// compiled against a hal old enough to predate this header at all fails on
// `#include "hal/api_version.hpp"`, naming the missing file.
//
// Fully qualified ::hal::, because this expands in a driver's header at file
// scope with no `using` in sight, and in a driver that may well have opened no
// namespace yet.
//
#define THORIUM_REQUIRE_HAL_API( writtenAgainst)                                      \
    static_assert( ::hal::hasReachedApiVersion( writtenAgainst),                      \
        "This instrument driver was written against a NEWER hal API than the one it " \
        "is being compiled against: update framework/hal, or install the driver package "  \
        "matching this hal. Both numbers are in hal/api_version.hpp.");               \
    static_assert( ::hal::stillSupportsApiVersion( writtenAgainst),                   \
        "This instrument driver was written against an OLDER hal API than this hal "  \
        "still supports: update the driver package. hal/api_version.hpp's version "   \
        "log says what changed.")
