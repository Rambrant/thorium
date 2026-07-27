// ReSharper disable once CppMissingIncludeGuard

//
// This rig's instrument identities -- the enumerators of hal::InstrumentId
// (see hal/instrument.hpp). Deliberately the thinnest possible fact: just
// names, nothing about what type backs each one or how it's wired -- those
// are instrument.inc's and wiring.inc's jobs respectively, both right here
// alongside this file. Splitting identity out into its own one-line-per-
// name file (rather than folding it into instrument.inc) is what lets
// hal/instrument.hpp -- a generic header, shared by every rig that links
// this library -- generate InstrumentId's enumerators via
// THORIUM_INSTRUMENT_IDS without also knowing each name's concrete driver
// type or constructor arguments.
//
// Two independent expansions read this same list, the same INSTRUMENT_ID-
// redefined-twice pattern hal/safing.cpp uses on instrument.inc: once to
// declare the enum's actual enumerators (hal/instrument.hpp), once to build
// its to_string() (hal/src/instrument.cpp). Adding a new instrument to this
// rig means adding it here, to instrument.inc, and to wiring.inc (if it's
// switched) -- three facts, three files, none of them inferable from the
// others.
//
INSTRUMENT_ID( Osc1)
INSTRUMENT_ID( Dmm1)
INSTRUMENT_ID( Dmm2)
INSTRUMENT_ID( DcP1)
INSTRUMENT_ID( DcP2)
INSTRUMENT_ID( DcP3)
INSTRUMENT_ID( DcP4)
INSTRUMENT_ID( AcP1)
