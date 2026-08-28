// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's instrument list: one meter, and nothing else.
//
// Read exactly the way rig/instrument.inc is -- twice, once by
// dev/rig/active_instruments.hpp to declare the global and once by
// hal/driver/instrument.hpp to generate hal::InstrumentId's enumerators -- so see
// that file's own comment for the mechanism. What is worth saying here is
// what makes this list different from the bench's.
//
// One row, because a driver is developed against one instrument at a time.
// The point of this deployment is a real L4411A on a desk with a LAN cable to
// it: the full Measure/Verify/journal chain running against hardware that
// actually answers, without a rack, a VPC adapter or a DUT. A second
// instrument is a second row and costs nothing, but nothing here needs one.
//
// The address is a placeholder for whichever meter is on the desk, and unlike
// rig/instrument.inc's provisional GPIB addresses this one is expected to be
// edited rather than confirmed -- it names one developer's bench, not a rack
// whose wiring a reviewer can check. Lan( ...) rather than Simulated{}
// deliberately: this deployment exists to talk to a real instrument, and
// Simulated{} here would say the opposite (see hal/driver/address.hpp on which of
// those two an absent instrument should be). A build with nothing plugged in
// still runs -- --inject and --skeleton detach the bench, and an attached run
// with no instrument fails at the reading, which is the honest outcome.
//
// Dmm1, not DevDmm: a script written here should be movable to the bench
// suite and back, and the instrument names are the vocabulary that makes that
// true.
//
INSTRUMENTS
    INSTRUMENT( L4411A, Dmm1, Lan( "dev-dmm"))   // TODO(bench): the hostname of the meter on your desk
END_INSTRUMENTS
