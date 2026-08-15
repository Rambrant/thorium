#pragma once

#include <string_view>


namespace core
{
    //
    // Whether anything can be *routed* to this point at all.
    //
    // Signal (the default, and what POINT declares) is the ordinary case: a
    // pin the switching fabric can reach, so any instrument port can be
    // pointed at it.
    //
    // Source says a source instrument's output is cabled straight onto this
    // pin -- it is a rail the bench drives, not just a pin the DUT presents
    // (see hal/adapter.hpp's SOURCE_POINT, and hal::SourceWiring for the
    // rig-side half naming *which* instrument lands there).
    //
    // Note what Source deliberately does NOT do: forbid measuring there.
    // That was this enum's first shape and it was wrong. A rail is cabled
    // rather than routed so the *fabric* never carries its load current --
    // but asking what voltage actually arrives at the DUT's pin, through a
    // high-impedance mux tap, is an ordinary and valuable check, and is
    // exactly what suite/scripts/rig_power_on.cpp documents as the right way
    // to ask that question (as distinct from the supply's own readback,
    // which measures at the instrument and cannot see cable drop or a bad
    // contact). Forbidding it would have banned the one measurement the
    // landing pin exists to make checkable.
    //
    // So a pin can be both cabled and tapped, and most here are. Whether a
    // given landing pin has a tap is a rig fact (a WIRE_CONNECTOR row), not
    // a DUT one, so it is not carried here and cannot be -- reachability
    // stays the runtime question core::MeasureEngine already treats it as.
    // What this kind carries is the fact that a rail is present, which is
    // true of the device on any bench.
    //
    // Read this against the QuantityKind that AdapterPointTag used to carry
    // and no longer does (see below), because the two look alike and the
    // first attempt at this enum repeated that mistake before being cut back
    // to what it is now. Declaring a pin "Voltage" forbade measurements that
    // are ordinary and correct -- inrush current at a supply pin, ripple
    // frequency at a rail -- and protected against nothing, since a reading
    // in the wrong quantity is obvious the moment it comes back. Source
    // earns its place only because it forbids nothing at all: it is a
    // recorded fact about the pin, checked against the rig's own table (see
    // dut/tests/test_wiring_coverage.cpp), not a restriction on what may be
    // asked of it.
    //
    // It is also deliberately the *kind* of point and not the identity of
    // the landing instrument. Which supply is cabled to which pin is a fact
    // about one bench, belongs in that rig's wiring.inc, and would drag
    // instrument names into DUT data that is otherwise instrument-blind
    // (see this type's own comment below on what a point does not know).
    // That a pin is a power input rather than something you probe is true
    // of the device on any bench, which is why this half lives here.
    //
    enum class PointKind
    {
        Signal,
        Source
    };

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
    // is not this one. PointKind above is that constraint, added later and
    // deliberately shaped the way this paragraph asked for: it says nothing
    // about what may be measured at a routable pin, only whether a pin is
    // routable at all.
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
    // Kind defaults to Signal so POINT's expansion (and every existing
    // AdapterPointTag<Loc> spelling) means exactly what it always did --
    // AdapterPointTag<Loc> and AdapterPointTag<Loc, PointKind::Signal> are
    // the same type, not two.
    //
    template<auto Loc, PointKind Kind = PointKind::Signal>
    struct AdapterPointTag
    {
        static constexpr auto LocationValue = Loc;
        static constexpr auto KindValue     = Kind;

        std::string_view Name;
        std::string_view Description;
    };

    //
    // Marks a group of points that are one physical interface rather than
    // several unrelated pins -- an RS232 console, an Ethernet pair set, a
    // three-phase input. See hal/adapter.hpp's BUNDLE/LINE macros, which
    // build these.
    //
    // An empty, non-virtual marker base, exactly like hal::InstrumentTag
    // (hal/instrument.hpp) and for the same reason: it costs nothing, and it
    // means the reflection walk over an adapter (dut/tests/
    // test_wiring_coverage.cpp) recurses into a nested group only where that
    // group explicitly opted in by inheriting this -- rather than guessing
    // from shape, which would sweep in any nested type that happened to
    // contain point-like members.
    //
    // Note what a bundle deliberately does NOT do: it does not make its
    // lines any less individual. Each LINE is an ordinary AdapterPointTag,
    // measurable on its own and checked by every per-pin rule exactly as a
    // top-level POINT is. Grouping records that the wires belong to one
    // interface; it does not introduce a second kind of thing that has to be
    // routed, covered, or verified differently. That is why this is a marker
    // and not a container type owning its points.
    //
    struct AdapterBundleTag {};
} // namespace core
