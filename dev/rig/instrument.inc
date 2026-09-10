// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's instrument list: one meter, and nothing else.
//
// Read exactly the way rig/instrument.inc is -- twice, once by
// hal/topology/active_instruments.hpp to declare the global and once by
// hal/driver/instrument.hpp to generate hal::InstrumentId's enumerators -- so see
// that file's own comment for the mechanism. What is worth saying here is
// what makes this list different from the bench's.
//
// One row, because a driver is developed against one instrument at a time.
// The point of this deployment is a real meter on a desk with a LAN cable to
// it: the full Measure/Verify/journal chain running against hardware that
// actually answers, without a rack, a VPC adapter or a DUT. A second
// instrument is a second row and costs nothing, but nothing here needs one.
//
// That meter is an EDU34450A, and this row changed when the bench's Dmm1 did
// -- both for the same reason, which is that it is the meter actually on hand.
// The change is worth more here than on the bench, because here it is the
// whole deployment: a dev build compiles against exactly the drivers this file
// names -- its type column is what pulls a driver header in at all, via
// cmake/InstrumentDrivers.cmake -- so the driver under development
// is the only driver in the binary. See instruments/keysight_edu34450a/ for
// what the model is, and dev/suite/scripts/dmm_self_check.cpp for the reading
// this deployment takes from it -- including capacitance, which is the one
// function the L4411A this row used to name does not have at all.
//
// The address is a placeholder for whichever meter is on the desk -- it names
// one developer's bench, not a rack whose wiring a reviewer can check.
// Lan( ...) rather than Simulated{} deliberately: this deployment exists to
// talk to a real instrument, and Simulated{} here would say the opposite (see
// hal/driver/address.hpp on which of those two an absent instrument should
// be).
//
// This row used to say the placeholder was "expected to be edited rather than
// confirmed", and that is no longer the advice. Editing it means a
// permanently dirty working tree and a live chance of committing your own
// desk's hostname into the deployment.
//
// Two things replaced that, and the first is why this column no longer decides
// anything on an attached run. dev/rig/pools.inc declares the shelf of meters
// this desk draws from, and a pool outranks a row's own address -- so an
// attached run walks dev-dmm-1, -2, -3 in order and takes the first that
// answers, and what is written here is never opened. See that file: it is also
// where the reason the bench next door must never have one is written down.
//
// The second is for the meter that is on none of them:
//
//     run_scripts --address Dmm1=lan:dev-dmm-7
//
// or THORIUM_ADDRESS_Dmm1 in the shell that runs it, which a desk that always
// has the same meter can set once. An override beats the pool as well as this
// column, so it is still the one thing that always wins. Either way the run's
// own header says which box answered and where the address came from -- see
// hal/verbs/preflight.hpp.
//
// The flag is checked against this meter's own back panel rather than against
// this row: an EDU34450A has LAN and USB, so --address Dmm1=usb:<serial> is
// accepted here too, and a bus it has no connector for is refused at startup
// exactly as it would fail to compile in this column (see hal::BackPanel).
//
// A build with nothing plugged in still runs, and the two ways it can go are
// worth keeping straight. --inject and --skeleton detach the bench, so no
// address is ever opened and the run completes against described readings. An
// *attached* run fails at startup rather than at the first reading: the
// preflight opens every reachable instrument before the first script, so a
// desk with none of the pooled meters on it gets
//
//     Preflight failed: no free instrument for Dmm1: all 3 candidates in its
//     pool were busy or unreachable
//
// immediately, with nothing else attempted. That is the honest outcome and an
// earlier one than before, but it is a change worth knowing about if you were
// used to watching the chain run and fail at the Measure.
//
// Dmm1, not DevDmm: a script written here should be movable to the bench
// suite and back, and the instrument names are the vocabulary that makes that
// true.
//
INSTRUMENTS
    INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Lan( "dev-dmm"))   // fixes the row's kind; the shelf in pools.inc is what an attached run actually opens
END_INSTRUMENTS
