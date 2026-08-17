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
INSTRUMENTS
    INSTRUMENT( L4411A,       Dmm1)
    INSTRUMENT( L4411A,       Dmm2)
    INSTRUMENT( DSO8064,      Osc1)
    INSTRUMENT( N6701ADirect, DcP1, 1)
    INSTRUMENT( N6701ADirect, DcP2, 2)
    INSTRUMENT( N6701ARelay,  DcP3, 3)
    INSTRUMENT( N6701ARelay,  DcP4, 4)
    INSTRUMENT( Ac6834B,      AcP1)
END_INSTRUMENTS
