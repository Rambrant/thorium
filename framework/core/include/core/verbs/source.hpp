#pragma once

#include "core/session/bench.hpp"
#include "core/driver/describe.hpp"
#include "core/journal/journal.hpp"

namespace core
{
    //
    // Apply/Remove: programs -- or disables -- a source instrument's own
    // output, exactly as if by hand at its front panel. Deliberately just
    // instrument I/O, with no idea the switching fabric exists: it used to
    // also route the fabric in the same call, but that coupled two facts
    // with very different timing (programming/ramping a supply can take a
    // while; opening or closing a relay is close to instantaneous) and two
    // very different reasons to call them separately -- e.g. programming a
    // supply well ahead of when it's actually switched onto the DUT, or
    // dropping a connection immediately for a safety interlock without
    // waiting on a slower output ramp-down. See core/verbs/route.hpp for the half
    // that moved out, which is also why these two files are separate: this
    // one needs no rig facts at all, and that one is generic over three of
    // them.
    //
    //     Connect( Dcp1.dc());
    //     Apply(   Dcp1.dc().voltage( 24_V).currentLimit( 7_A));
    //     ...
    //     Remove(     Dcp1.dc());
    //     Disconnect( Dcp1.dc());
    //
    // Apply/Remove can be called in either order relative to Connect/
    // Disconnect as far as this code is concerned -- neither reaches for the
    // fabric, so neither cares whether the DUT is actually wired up yet. The
    // hardware is not indifferent, and the order above is the one to copy:
    // the relay moves while the path is dead, at both ends of the sequence.
    //
    // Switching a relay with the circuit live is hot switching. On break it is
    // the well-known half -- current flowing through contacts that are parting
    // draws an arc, which erodes and eventually welds them, and it is what
    // hal::safeRig() is ordered to avoid (see hal/verbs/safing.hpp). On make it is
    // the same mechanism arriving from the other side: contacts bounce as they
    // close, and closing onto a live supply means each bounce breaks an
    // energised circuit again. So the sequence nests rather than mirrors --
    // Connect, Apply ... Remove, Disconnect -- and a relay that only ever moves
    // cold outlives one that does not by orders of magnitude.
    //
    // Deliberately not enforced here, and that is not an oversight: hot
    // switching is a wear argument, not a correctness one, and there are cases
    // where the wear is the cheaper cost. A safety interlock dropping a
    // connection the instant it is asked to should not first wait out a
    // supply's ramp-down. That call belongs to whoever is writing the sequence,
    // so these engines take no view -- see suite/scripts/rig_power_on.cpp and
    // rig_power_off.cpp for the same ordering written out as a rig's actual
    // power-up and power-down.
    //
    // Not enforced still means not enforced -- but it is no longer unrecorded.
    // Connect and Disconnect (core/verbs/route.hpp) ask the driver whether its output
    // is energised at the moment a contact moves, and say so in their own
    // journal event when it was. That does not narrow the choice this paragraph
    // describes; it stops the choice from being invisible, which is a different
    // thing and was the actual gap. Breaking the rule on purpose now reads as
    // something the run says you did, rather than as indistinguishable from
    // having got the order wrong. See core/verbs/interlock.hpp for why this hazard is
    // recorded where the ammeter-across-a-rail one is refused.
    //
    // Dispatch to the actual instrument driver happens via an ADL
    // customization point (an unqualified applyDriver/removeDriver call
    // inside operator() below), the same trick core/verbs/measure.hpp uses for
    // to_string(instrumentId): a hal-side overload for each concrete
    // builder type resolves via ADL, and this header stays free of any
    // hal:: dependency. Neither engine carries any state of its own any
    // more -- there's nothing left to inject -- but they stay classes
    // rather than bare functions for symmetry with Connect/Disconnect/
    // Measure.
    //
    // Every driver call below is conditional on a bench being attached, which
    // is the stimulus half of what core::ISession does for the observing verbs
    // -- see core/session/bench.hpp, which is where the whole argument is written down.
    // The short of it: a replayed run takes its readings from a file, and
    // energising a rail for real while doing so is the one thing it must not
    // do. The event is still posted either way, and says which happened.
    //
    class ApplyEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                if( bench().isAttached())
                {
                    applyDriver( builder.config());
                }

                //
                // Logged after the driver call, not before: the log should say
                // what the rig was actually told, and if applyDriver throws
                // (a real instrument refusing a setpoint) then nothing was
                // applied and nothing should claim it was. Same ordering in
                // every engine here, in core/verbs/route.hpp, and in
                // core::MeasureEngine.
                //
                detail::postSourceEvent( Verb::Apply, builder.config(), true);
            }
    };

    //
    // Setup: tell an instrument how to behave, without making it do anything.
    //
    // The distinction from Apply is the whole reason this verb exists, and it
    // is a bench distinction rather than a code one. Apply energises: after it,
    // the rig is doing something to the DUT that it was not doing before, and
    // the ordering rules in this file's comment above -- Connect, Apply ...
    // Remove, Disconnect -- exist because of that. Setup does not: configuring
    // a UART for 9600 8N1, or a scope for a timebase and a trigger level,
    // changes what a *later* verb will mean and changes nothing at the DUT's
    // pins. Collapsing the two would put "the port was configured" and "24 V is
    // now on the backup rail" behind one word in the log, and would drag
    // configuration into the hot-switching argument it has no part in.
    //
    //     Connect( Ser1.rs232(), at( dut::Console));
    //     Setup(   Ser1.rs232().baudRate( 9600).wordLength( 8).parity( Parity::None));
    //     Write(   Ser1.rs232(), "RD 30\r");
    //
    // It touches the fabric not at all, exactly like Apply/Remove, so it takes
    // no wiring and no point -- see this file's comment above on why that
    // separation was made.
    //
    // Dispatched through setupDriver, an ADL customization point of its own
    // rather than a flag on applyDriver. A driver that has settings but nothing
    // to energise (a serial port) writes only setupDriver; one that energises
    // but has nothing to pre-configure writes only applyDriver; and calling a
    // verb an instrument has no implementation for is "no matching function"
    // at compile time rather than a silently ignored call.
    //
    class SetupEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                if( bench().isAttached())
                {
                    setupDriver( builder.config());
                }

                // With settings -- the settings *are* what a Setup did.
                detail::postSourceEvent( Verb::Setup, builder.config(), true);
            }
    };

    class RemoveEngine
    {
        public:
            template<typename BuilderT>
            auto operator()( const BuilderT & builder) const -> void
            {
                if( bench().isAttached())
                {
                    removeDriver( builder.config());
                }

                // No settings -- see core::SourceDescription's own comment.
                detail::postSourceEvent( Verb::Remove, builder.config(), false);
            }
    };
} // namespace core
