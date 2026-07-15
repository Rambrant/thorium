#pragma once

#include <string_view>

#include "core/quantity_kind.hpp"

namespace core
{
    //
    // A single named connection on a DUT/adapter, carrying its physical
    // location and expected quantity kind as compile-time values (template
    // parameters), not runtime data. This is what lets Measure() catch a
    // misspelled point name (a real "no such member" compile error, exactly
    // like CRIT's protection against a misspelled criterion id -- see
    // hal/adapter.hpp's ADAPTER/POINT macros, which build these) and a
    // quantity mismatch (Measure(dmm1.current(), SomeVoltagePoint) simply
    // has no matching overload) without needing reflection.
    //
    // Generic over Loc's type via `auto`: this header has no idea what a
    // physical location coordinate looks like, only that it's some
    // structural value usable as a template parameter; hal::VpcLocation
    // (see hal/vpc_location.hpp) is simply the concrete coordinate type this
    // project's rig happens to use, not something this type depends on.
    //
    // Name/Description stay ordinary runtime string_views: Name exists so a
    // session (see core/session.hpp) has something to key a recording or an
    // injected value by, and Description is purely for humans -- neither
    // needs to be a compile-time value the way Loc/Kind do.
    //
    template<auto Loc, QuantityKind Kind>
    struct AdapterPointTag
    {
        static constexpr auto LocationValue = Loc;
        static constexpr auto KindValue     = Kind;

        std::string_view Name;
        std::string_view Description;
    };
} // namespace core
