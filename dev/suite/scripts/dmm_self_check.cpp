#include "../prelude.hpp"

//
// The whole chain, on a bench with one instrument: a reading, a criterion, and
// a verdict -- catalog to journal to both log sinks, with nothing stubbed.
//
// Measure with no at( ...), which is the one thing that makes this deployment
// possible at all: the point-free overload reads the instrument over its own
// interface and touches no fabric (see core::MeasureEngine). Everything after
// it is identical to a bench script -- the same Verify, the same criteria
// lookup, the same journal events, the same RTF and SARIF rows.
//
// Two checks, and the second is this file's own advice taken: a dev script is a
// probe for whether the driver answers and what it answers, so what is worth
// adding is not more assertions on one reading but another reading -- a second
// quantity off the same meter. That is one line, and it was one line.
//
// Capacitance is the quantity worth spending it on, because it is the one this
// deployment could not take at all until the meter on the desk became an
// EDU34450A (see dev/rig/instrument.inc): the L4411A this bench used to name
// has no capacitance function, and core had no farad to carry one. So this
// reading exercises the whole new path end to end -- driver port, unit,
// QuantityKind, session key, criterion, both log sinks -- against hardware
// that actually answers.
//
// Both readings are point-free, which is what makes this deployment possible at
// all and matters more for the second than the first. A capacitance meter
// charges the node from its own current source, so core::requiresDeadNode
// names it (core/verbs/interlock.hpp) and a *routed* capacitance reading at a
// pin an energised supply is cabled onto is refused before anything closes.
// There is nothing to refuse here: no fabric, no source, no pin -- just a
// reference capacitor across the meter's own terminals, which is exactly the
// shape a driver-development check wants.
//
auto dmmSelfCheck() -> void
{
    const auto reference = Measure( Dmm1.voltage());
    const auto bulk      = Measure( Dmm1.capacitance());

    Verify( DEV_Dmm_1::DEV_Dmm_Ref,   reference);
    Verify( DEV_Dmm_1::DEV_Dmm_Bulk,  bulk);
}
