#pragma once

#include <string_view>


namespace core
{
    //
    // A single named connection on a DUT/adapter, carrying its physical
    // location as a compile-time value (a template parameter), not runtime
    // data. This is what lets Measure() catch a misspelled point name -- a
    // real "no such member" compile error, exactly like CRIT's protection
    // against a misspelled criterion id -- see hal/adapter.hpp's ADAPTER/POINT
    // macros, which build these.
    //
    // A point deliberately does NOT declare which quantity is measured at it.
    // It names a pin on the device; what you measure there is a property of
    // the instrument port you point at it, not of the pin. This tag used to
    // carry a QuantityKind too, and Measure() required the port's quantity to
    // match it -- which made Measure( Dmm1.current(), at( Output5V)) a compile
    // error purely because the pin had been labelled "Voltage". Inrush current
    // or ripple frequency at a supply pin are ordinary things to want, so that
    // was an over-constraint, not a safety check: it forbade legitimate
    // measurements and protected against nothing a wrong reading wouldn't make
    // obvious immediately.
    //
    // If some port genuinely must never be pointed at some pin -- a source that
    // would damage the device, say -- that is a real constraint worth
    // expressing, but it belongs between the *instrument* and the pin, and it
    // is not this one.
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
    // needs to be a compile-time value the way Loc does.
    //
    template<auto Loc>
    struct AdapterPointTag
    {
        static constexpr auto LocationValue = Loc;

        std::string_view Name;
        std::string_view Description;
    };
} // namespace core
