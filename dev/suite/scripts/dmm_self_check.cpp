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
// One check, deliberately. A dev script is a probe for whether the driver
// answers and what it answers, so the useful thing to add here is not more
// assertions on one reading but another reading: a second quantity off the same
// meter, a reading before and after a mode change. Those are one line each.
//
auto dmmSelfCheck() -> void
{
    const auto reference = Measure( Dmm1.voltage());

    Verify( DEV_Dmm_1::DEV_Dmm_Ref, reference);
}
