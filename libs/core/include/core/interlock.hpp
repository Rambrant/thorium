#pragma once

#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/journal.hpp"
#include "core/quantity_kind.hpp"

namespace core
{
    //
    // The electrical interlock: the two ways a route can be electrically
    // wrong, and the two different answers they get.
    //
    // ---------------------------------------------------------------------
    // Why two answers and not one
    // ---------------------------------------------------------------------
    //
    // Both hazards were named together, in one line of README.md §1 -- "an
    // ammeter across a driven rail, or opening a relay under load" -- and
    // treating them as one thing is the mistake this file exists to avoid.
    // They differ in what goes wrong, and therefore in who gets to decide.
    //
    // Hot switching -- a relay moving while the source on it is energised --
    // is a *wear* argument. The contacts arc, erode, and eventually weld, so
    // a relay that only ever moves cold outlives one that does not by orders
    // of magnitude; nothing is destroyed today. core/source.hpp has said for
    // as long as Connect/Disconnect have existed that this is deliberately
    // not refused, and gives the counterexample that makes refusing wrong: a
    // safety interlock dropping a connection the instant it is asked to must
    // not first wait out a supply's ramp-down. That call belongs to whoever
    // is writing the sequence.
    //
    // So hot switching is *recorded*, never refused. What was missing was
    // never the veto -- it was that the choice left no trace. The rule is
    // stated in eight comments across this tree and enforced nowhere, so a
    // script that got the order wrong wore a relay out silently, and a script
    // that broke the rule on purpose was indistinguishable from it. Now the
    // Connect/Disconnect event carries it (see detail::postSourceEvent in
    // core/describe.hpp), which makes a deliberate hot switch a thing the log
    // says you did and an accidental one a thing a reader can find.
    //
    // A low-impedance tap onto a live rail is not wear. Routing a DMM's
    // current function to a pin a supply is cabled onto puts a shunt across
    // that rail, through matrix and mux relays chosen to carry signals -- the
    // outcome is a short circuit and destroyed hardware, not a shortened
    // contact life, and there is no sequence in which it is the cheaper cost.
    // That one throws, before anything closes.
    //
    // ---------------------------------------------------------------------
    // Why the second hazard is reachable at all
    // ---------------------------------------------------------------------
    //
    // Because a landing pin is deliberately also tapped. core::PointKind
    // records that a rail is cabled onto a pin and forbids nothing about
    // measuring there, on the explicit grounds that a *high-impedance* tap
    // onto that same pin is an ordinary and valuable check -- reading what
    // actually arrives at the DUT's pin, cable drop and bad contacts
    // included, as distinct from the supply's own readback. That reasoning is
    // right, and dut/tests/test_wiring_coverage.cpp says every source point
    // on this rig is in fact tapped.
    //
    // What it justifies, though, is the high-impedance case only, and the
    // routed Measure overload admits any port at any pin. This is the hole
    // that leaves, and no wider than that: a voltage tap at a live rail stays
    // exactly as legal as core::PointKind says it is.
    //
    // ---------------------------------------------------------------------
    // What this deliberately is not
    // ---------------------------------------------------------------------
    //
    // Not a safety guarantee, for the same reason hal::safeRig() is not one:
    // anything depending on software on this machine still running is
    // defeated by the failures that matter. It is a guard against a script
    // asking for something electrically wrong, not against a relay failing
    // closed or a cable in the wrong socket.
    //
    // And there is no override, because the correct escape hatch already
    // exists and is the bench procedure anyway: Remove the source, then
    // measure. energisedSourceAt (below) answers "nothing live here" the
    // moment the rail is off, so a script that de-energises first is never
    // refused. An opt-out flag would only ever be reached for by a script
    // that wanted the short.
    //

    //
    // Thrown by core::MeasureEngine when a reading would put a low impedance
    // onto an energised rail. A distinct type rather than std::runtime_error
    // so a caller can tell "this route is electrically wrong" from the
    // ordinary wiring-lookup failure thrown a few lines further on, which is
    // a missing table entry and says nothing about the rig's state.
    //
    // Deliberately not a core::UnmeasurableReading, which is the instrument
    // declining to answer and is an ordinary outcome the run carries on
    // through (see core/measure.hpp). This is the framework declining to ask,
    // and a script that asked has a bug in it -- so it unwinds, and
    // hal::safeRig() answers for whatever was live at the time.
    //
    class InterlockViolation : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    //
    // Whether a reading of this quantity requires the node it is taken at to
    // be dead.
    //
    // Two kinds qualify, for two different reasons, and it is worth keeping
    // them apart rather than calling them both "low impedance" and moving on:
    //
    //   - Current. An ammeter is a shunt. Routed onto a pin in parallel with
    //     a driven rail it is a short across that rail, and the fabric relays
    //     carrying it were sized for signals.
    //   - Resistance. An ohmmeter sources its own test current and reads the
    //     drop. An external source driving the same node makes the reading
    //     meaningless and can damage the input -- which is why "never measure
    //     resistance in a live circuit" is the first thing anyone is taught
    //     about a DMM's ohms range.
    //
    // Everything else -- Voltage, Frequency, Power, the scope's timebase
    // quantities -- taps high-impedance and stays permitted. Named for what
    // it requires of the node rather than for the port's impedance so the two
    // reasons above can sit under one predicate honestly, and shaped after
    // core::Port::requiresSensePath, which is the same kind of question asked
    // of a port's type.
    //
    [[nodiscard]]
    constexpr auto requiresDeadNode( const QuantityKind kind) -> bool
    {
        return kind == QuantityKind::Current || kind == QuantityKind::Resistance;
    }

    //
    // The Detail a hot-switched Connect/Disconnect event carries.
    //
    // Built here rather than at the two call sites so the wording is one
    // string a log consumer can match on, and so the make/break halves cannot
    // drift apart -- they are the same mechanism arriving from opposite sides
    // (contact bounce onto a live circuit, an arc drawn as contacts part) and
    // a reader wants to know which one happened.
    //
    [[nodiscard]]
    auto hotSwitchDetail( Verb method, std::string_view instrument) -> std::string;

    //
    // The message InterlockViolation carries. Everything a reader needs to
    // fix the script: which pin, what was being measured there with what,
    // and which source is holding the pin live.
    //
    [[nodiscard]]
    auto liveTapMessage( std::string_view point,
                         std::string_view instrument,
                         QuantityKind     kind,
                         std::string_view liveSource) -> std::string;

    //
    // "Apply can energise this config", which is the same question as "is this
    // config a source at all". Spelled as a concept rather than inline in the
    // probe below so the requirement can be asserted in *both* directions --
    // see libs/core/tests/test_interlock.cpp, and hal::SafeableInstrument in
    // hal/instrument.hpp, whose own tests make the same argument: a guard that
    // can only be demonstrated by breaking the build is a guard nobody checks.
    //
    template<typename ConfigT>
    concept EnergisableConfig = requires( const ConfigT & config)
    {
        applyDriver( config);
    };

    //
    // ...and "this config can say whether it currently is". Every
    // EnergisableConfig must also be one of these; nothing else need be.
    //
    template<typename ConfigT>
    concept EnergyReportingConfig = requires( const ConfigT & config)
    {
        { isEnergised( config) } -> std::convertible_to<bool>;
    };

    namespace detail
    {
        //
        // Is this config's instrument energised right now?
        //
        // An ADL customization point in the shape of applyDriver/
        // connectDriver (see core/source.hpp and core/route.hpp), and found
        // the same way -- so this header, like those, names nothing from hal.
        //
        // Optional, because most configs Connect/Disconnect are called with
        // are not sources at all and have no output to be energised: a serial
        // port's config (hal::Racal1260Config) has a setupDriver and no
        // applyDriver, and asking whether a UART is live is not a question.
        //
        // But optional in the one shape that cannot lose the check by
        // omission. "Has an applyDriver" is exactly "Apply can energise
        // this", so a config with one and no isEnergised is a source that
        // cannot say whether it is live -- which is not a config the
        // interlock should quietly wave through, it is a driver missing a
        // member. The static_assert makes it a one-line diagnostic naming
        // the requirement, the same trick hal::SafeableInstrument uses to
        // turn "this driver forgot safe()" into something readable.
        //
        // That is the whole reason this is not a bare
        // `if constexpr( requires { isEnergised( config); })`. That spelling
        // compiles for every config and silently reports "cold" for a source
        // whose driver author never wrote the member -- a safety check that
        // disappears on the exact input it exists for.
        //
        template<typename ConfigT>
        [[nodiscard]]
        auto energisedNow( const ConfigT & config) -> bool
        {
            if constexpr( EnergisableConfig<ConfigT>)
            {
                static_assert( EnergyReportingConfig<ConfigT>,
                               "a config Apply can energise must also answer isEnergised( config) --"
                               " Connect/Disconnect need it to tell a cold relay from a hot one;"
                               " see core/interlock.hpp");

                return isEnergised( config);
            }
            else
            {
                return false;
            }
        }
    } // namespace detail
} // namespace core
