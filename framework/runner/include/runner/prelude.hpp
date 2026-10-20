#pragma once
//
// Everything a test script body is written against that does not depend on
// which deployment it was built for -- the hal verbs, the rig's instrument
// globals, the verdict verbs and the criteria vocabulary. A deployment's own
// prelude (suite/prelude.hpp, dev/suite/prelude.hpp) includes this and adds the
// two things only it can name: its scripts.hpp, and its DUT's adapter.
//
// This used to be written out twice, once per deployment, and the two copies
// differed in exactly those two paths. What is left in each deployment's file is
// those paths and nothing else, so adding a verb here reaches every deployment
// at once instead of drifting between them.
//
// Nothing here names rig/, dut/ or suite/ by path. hal/topology/
// active_instruments.hpp and core/criteria/active_criteria.hpp reach the rig's
// instrument table and the criteria variant tables through compile definitions
// (THORIUM_INSTRUMENT_TABLE, THORIUM_CRITERIA_VARIANT_TABLES), which is why this
// can live in framework/ at all. Like those, it is only meant to be included from
// a script translation unit: the using-declarations below are deliberate and
// confined to one -- core/catalog/active_test_catalog.hpp does not include this
// file, so nothing outside a script is affected.
//
// The rig: Dmm1, Osc1, DcP5.. as globals a script addresses by name, and the
// verbs they are pointed through -- Measure, the Apply/Connect/Disconnect/Remove
// sourcing counterparts, and the triggered-capture verbs Arm/Await/Fetch.
// at(...) comes with them.
//
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/verbs/source.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/acquire.hpp"
#include "hal/verbs/trace.hpp"
//
// The verdict verbs, unqualified -- so a script writes Verify( ...) next to
// Measure( ...), rather than core::Verify( ...) next to a bare Measure. Fail
// comes with it: it is what a script writes where a check could not be made at
// all, so it appears in the same bodies and on the same footing as Verify (see
// core/criteria/verify.hpp).
//
#include "core/verbs/verify.hpp"

using core::Verify;
using core::Fail;
//
// The tolerances -- every variant merged into the one table a script names
// (FS_Supply_1::FS_Supply_5V0, ...) -- and with them the unqualified predicate
// and quantity vocabulary (EQ, MASK, _V, _A) that active_criteria.hpp
// deliberately promises to whatever includes it. A script writing an ad-hoc
// EQ( 12.0_V).epsilon( 0.05_V) needs no further include of its own.
//
#include "core/criteria/active_criteria.hpp"
//
// The adapter's macros, but not an adapter: ADAPTER/POINT live here so a DUT's
// adapter.inc can be included right after this header without the deployment's
// prelude having to remember that hal/topology/adapter.hpp has to come first.
//
#include "hal/topology/adapter.hpp"
