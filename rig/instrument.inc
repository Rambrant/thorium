// ReSharper disable once CppMissingIncludeGuard

//
// This rig's fixed, concrete instrument list -- included from two
// different places, each with INSTRUMENT defined for its own purpose:
// rig/active_instruments.hpp declares each id below as an actual global,
// and hal/instrument.hpp keeps only each id to generate
// hal::InstrumentId's enumerators. Both read this exact list, so an
// instrument added here can never desync from its own identity enumerator
// -- there is nothing left to desync from, since the enumerator comes from
// this file too now.
//
// hal/instrument.hpp's own comment explains why THAT read can't be
// replaced by reflection the way the rest of this codebase's macro-
// redefinition-and-re-#include spots were: reflection needs the enum to
// already exist, and generating it is this read's entire job. Contrast
// hal/src/safing.cpp, which used to read this file a third time
// (INSTRUMENT redefined again, to call .safe() on each) -- it no longer
// needs this file at all, reflecting over hal::InstrumentTag-derived
// globals instead (see that struct's own comment in hal/instrument.hpp),
// because InstrumentId already existed by the time IT ran.
//
// See rig/active_instruments.hpp's own comment for the INSTRUMENTS/
// INSTRUMENT/END_INSTRUMENTS macros this expands through, and rig/wiring.inc
// for the sibling fact (which channel each of these instruments is wired
// to).
//
// DcP1..DcP4 are the N6701A mainframe's four module slots, each its own
// hal::N6701A instance -- see that class's own comment in hal/n6701a.hpp
// for why the channel number is a constructor argument here, and why
// that's a different fact from which InstrumentWiring/wiring.inc entry
// they get.
//
// DcP1/DcP2 are wired straight through with no isolation relay at all;
// DcP3/DcP4 each have one -- see hal::N6701A's own comment on
// DirectWiring/RelayIsolated for what that means for Connect/Disconnect.
// Matches this rig's real bench fact: some DC rails switchable, some not,
// same instrument type either way.
//
//
// Ser1 is the one instrument here that is neither a meter nor a source: an
// RS232 port routed to the DUT through the same matrix everything else uses
// (see hal::Racal1260, and dut::Console for the interface it talks to). It
// takes no constructor argument beyond its id and address -- one instance is
// one port, the same way one hal::L4411A is one meter.
//
// The third column is how the *PC* reaches each instrument -- the control
// side, as against the signal side wiring.inc describes (see hal/address.hpp
// for the distinction, and rig/active_instruments.hpp for why it is a fixed
// column rather than one more constructor argument). Which bus kinds a given
// row may use is fixed by its driver, not by this file: an L4411A is an LXI
// box with no GPIB connector, so Gpib( ...) on one of the Dmm rows is a
// compile error rather than a run that dies on its first reading.
//
// DcP1..DcP4 all carry the same address, and that repetition is the point:
// they are four modules behind one mainframe interface, so there is one
// address and four slots, not four addresses. If a second shared box ever
// appears here, the thing to add is a named constant above this table for
// each chassis -- not a slot field on the address.
//
// TODO(bench): every address below is invented, exactly as every channel in
// wiring.inc is -- there is no physical bench yet. They are internally
// consistent (one mainframe address for the four DC modules, LXI hostnames for
// the three LAN instruments) and no more than that. Re-check against a real
// rack. Deliberately not hal::Simulated{}, which would read as a decision
// rather than a gap -- see that type's own comment in hal/address.hpp.
//
// Ser1's row is Serial on purpose, and for a reason that is not a claim about
// the bench: it is this table's one worked example of a PC-attached serial
// device, kept deliberately so that the three bus kinds a rig is likely to
// meet -- a GPIB box, an LXI hostname, a device path -- are all exercised by
// something real here rather than only described in hal/address.hpp.
//
// The underlying bench fact is still open, and the driver says so: it accepts
// Serial *or* Gpib, because a matrix-routed RS232 port can be either a port on
// this PC with its conductors cabled into the matrix or a serial module in the
// switching chassis commanded over the chassis bus (see hal::Racal1260's own
// constructor comment). If it turns out to be the latter, this row becomes the
// chassis's bus address and that constraint narrows to Gpib -- and this table
// loses its Serial example, which is a reason to write one somewhere else, not
// a reason to keep this row wrong.
//
INSTRUMENTS
    INSTRUMENT( L4411A,       Dmm1, Lan( "bench-dmm1"))
    INSTRUMENT( L4411A,       Dmm2, Lan( "bench-dmm2"))
    INSTRUMENT( DSO8064A,      Osc1, Lan( "bench-osc1"))
    INSTRUMENT( N6701ADirect, DcP1, Gpib( 0, 14), 1)
    INSTRUMENT( N6701ADirect, DcP2, Gpib( 0, 14), 2)
    INSTRUMENT( N6701ARelay,  DcP3, Gpib( 0, 14), 3)
    INSTRUMENT( N6701ARelay,  DcP4, Gpib( 0, 14), 4)
    INSTRUMENT( Ac6834B,      AcP1, Gpib( 0, 5))
    INSTRUMENT( Racal1260,    Ser1, Serial( "/dev/ttyUSB0"))
END_INSTRUMENTS
