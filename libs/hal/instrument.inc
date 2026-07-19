// ReSharper disable once CppMissingIncludeGuard

//
// This rig's fixed, concrete instrument list -- see hal/active_instruments.hpp's
// own comment for the INSTRUMENTS/INSTRUMENT/END_INSTRUMENTS macros this
// expands through, and hal/wiring.inc for the sibling fact (which channel
// each of these instruments is wired to).
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
INSTRUMENTS
    INSTRUMENT( L4411A,      Dmm1, Dmm1)
    INSTRUMENT( L4411A,      Dmm2, Dmm2)
    INSTRUMENT( DSO8064,     Osc1, Osc1)
    INSTRUMENT( N6701ADirect, DcP1, DcP1, 1)
    INSTRUMENT( N6701ADirect, DcP2, DcP2, 2)
    INSTRUMENT( N6701ARelay,  DcP3, DcP3, 3)
    INSTRUMENT( N6701ARelay,  DcP4, DcP4, 4)
    INSTRUMENT( Ac6677A,     AcP1, AcP1)
END_INSTRUMENTS
