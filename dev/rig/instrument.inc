// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's instrument list: a meter, a supply, a scope, a generator
// and a switch unit.
//
// Read exactly the way rig/instrument.inc is -- twice, once by
// hal/topology/active_instruments.hpp to declare the global and once by
// hal/driver/instrument.hpp to generate hal::InstrumentId's enumerators -- so see
// that file's own comment for the mechanism. What is worth saying here is
// what makes this list different from the bench's.
//
// Few rows, because a driver is developed against one instrument at a time.
// The point of this deployment is real instruments on a desk with a LAN cable
// to them: the full Measure/Verify/journal chain running against hardware that
// actually answers, without a rack, a VPC adapter or a DUT. The meter came
// first; the supply is the second instrument, and cost what this comment
// always said it would -- rows, and nothing else.
//
// That meter is an EDU34450A, and this row changed when the bench's Dmm1 did
// -- both for the same reason, which is that it is the meter actually on hand.
// The change is worth more here than on the bench, because here it is the
// whole deployment: a dev build compiles against exactly the drivers this file
// names -- its type column is what pulls a driver header in at all, via
// cmake/InstrumentDrivers.cmake -- so the drivers under development
// are the only drivers in the binary. See instruments/keysight_edu34450a/ for
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
//     run_scripts --address DeskDmm=lan:dev-dmm-7
//
// or THORIUM_ADDRESS_DeskDmm in the shell that runs it, which a desk that always
// has the same meter can set once. An override beats the pool as well as this
// column, so it is still the one thing that always wins. Either way the run's
// own header says which box answered and where the address came from -- see
// hal/verbs/preflight.hpp.
//
// The flag is checked against this meter's own back panel rather than against
// this row: an EDU34450A has LAN and USB, so --address DeskDmm=usb:<serial> is
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
//
// -- The supply ---------------------------------------------------------------
//
// DcP5, DcP6, DcP7: the three outputs of one Keysight EDU36311A, one row per
// output exactly as rig/instrument.inc has them -- and under the bench's
// names, for the reason Dmm1 is Dmm1: a script should move between the two
// deployments unchanged. See dev/suite/scripts/supply_outputs.cpp.
//
// DirectOutput for all three, where the bench has RelayOutput for 2 and 3:
// the relay type exists so Connect/Disconnect can move an isolation relay, and
// there is no relay card on this desk. That difference is invisible to a
// script that only Applies, Removes and reads back, which is all these do.
//
// One box, Psu1, for all three -- the three outputs are one unit behind one
// interface (see hal::keysight_edu36311a::EDU36311A::Buses) -- so one address,
// written on each row and held equal by the build (hal/topology/boxes.hpp).
// One flag moves all three:
//
//     run_scripts --address Psu1=usb:<serial>
//
// or THORIUM_ADDRESS_Psu1 in the shell. Not pooled, because this desk has one
// supply; POOL( Psu1, ...) would acquire one for all three outputs if it had a
// shelf of them.
//
// -- The scope ----------------------------------------------------------------
//
// Osc1, the bench's name for it: a DSOX1202G, two channels, LAN or USB (see
// hal::keysight_dsox1202g::DSOX1202G::Buses). Its scripts read the
// front-panel probe-compensation output, see dev/suite/scripts/scope_probe_comp.cpp.
//
// -- The waveform generator ---------------------------------------------------
//
// Wfg1, the bench's name for it: a Keysight (formerly Agilent) 33522B, two
// channels, LAN, USB or GPIB (see hal::keysight_33522b::Wfg33522B::Buses). USB
// to start with; LAN is one --address away. Its scripts are checked by the
// scope -- generator channel 1 cabled to scope channel 1, 2 to 2 -- see
// dev/suite/scripts/wfg_into_scope.cpp.
//
// -- The switch unit ----------------------------------------------------------
//
// Swu1: a Keysight (formerly Agilent) 34980A mainframe, GPIB, LAN or USB (see
// hal::keysight_34980a::Chassis::Buses) -- USB to start with. Four 34932A
// matrices (34932T terminal blocks) in slots 1-4 and a 34941A RF multiplexer in
// slot 5. Its scripts need nothing wired to it, see
// dev/suite/scripts/swu_self_check.cpp.
//
// An instrument row, which a chassis was not until this desk needed it to be:
// preflight checks its identity, --address and THORIUM_ADDRESS_Swu1 reach it,
// and safing opens its relays -- after every source, in safeRig()'s second
// pass (see hal::RelayHoldingInstrument). Declared last anyway, which costs
// nothing and reads the way safing runs.
//
//
// Dmm2 is the same box's other face: the 34980A's internal DMM, fitted on this
// desk's unit. Same box name and address as Swu1, which the build holds them to
// (hal/topology/boxes.hpp) -- unless the row says Simulated{}, which a unit
// without the DMM would, and --address=Dmm2=sim does for one run -- so the
// two share one session -- the switching and
// the meter as two families on one connection (see hal::BoxConnection). It
// measures the Analog Buses; a signal reaches it by closing a matrix crosspoint
// onto a Matrix 2 row and that row's bus relay. See
// dev/suite/scripts/swu_dmm.cpp.
//
INSTRUMENTS
    INSTRUMENT( DeskDmm, keysight_edu34450a::EDU34450A,     Dmm1, Usb( "CN65510018"))   // fixes the row's kind; the shelf in pools.inc is what an attached run actually opens
    INSTRUMENT( Psu1,    keysight_edu36311a::DirectOutput1, DcP5, Usb( "CN65100272"))   // 6 V / 5 A
    INSTRUMENT( Psu1,    keysight_edu36311a::DirectOutput2, DcP6, Usb( "CN65100272"))   // 30 V / 1 A
    INSTRUMENT( Psu1,    keysight_edu36311a::DirectOutput3, DcP7, Usb( "CN65100272"))   // 30 V / 1 A
    INSTRUMENT( Scope1,  keysight_dsox1202g::DSOX1202G,     Osc1, Usb( "CN64504143"))   // 
    INSTRUMENT( Wfg1,    keysight_33522b::Wfg33522B,        Wfg1, Usb( "MY52803613"))   //
    INSTRUMENT( Swu1,    keysight_34980a::Chassis,          Swu1, Usb( "MY53154781"))   // 
    INSTRUMENT( Swu1,    keysight_34980a::InternalDmm,      Dmm2, Usb( "MY53154781"))   // the same box's internal DMM -- see above
END_INSTRUMENTS
