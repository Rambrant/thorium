// ReSharper disable once CppMissingIncludeGuard

//
// This rig's fixed, concrete switching-device list: every matrix card, LF mux
// and RF selector its signal chain is built from, each with the kind of
// hardware it is and the address the PC commands it at. The sibling of
// rig/instrument.inc, and read the same way -- twice, once to generate
// hal::SwitchDeviceId's enumerators and once to carry the kind/address of each
// (see hal/switch_device.hpp's own comment on why the second read cannot be
// replaced by reflection the way most of this codebase's re-#include spots
// were).
//
// These are deliberately not instruments, even though they are addressable
// hardware the PC talks to just as an instrument is. A switching device
// measures nothing and sources nothing: no script ever names one, only
// wiring.inc does, and hal::InstrumentId is what a *reading* is identified by.
// See hal/switch_device.hpp for that argument in full, and hal/README.md for
// the naming this rig settled on -- driver for the code, instrument for the
// hardware a script names, switch device for the plumbing it doesn't.
//
// What this list is for, beyond the addresses: before it existed,
// wiring.inc named its cards in bare strings that nothing checked, so a
// mistyped "Matrix22" produced a working-looking route to nowhere, and the
// same crosspoint written with two different kinds counted as two independent
// relays. Both are now spelling mistakes the compiler makes -- a channel can
// only be on a card this file declares.
//
// Only three devices, for the plain reason that this rig's wiring.inc uses
// three. The name Matrix2 does suggest a Matrix1 somewhere in the rack; if it
// is real, declare it here even with nothing wired to it (a declared device
// nothing routes through costs nothing, exactly as DcP4 is a declared spare
// among the instruments). This rig has no RF selector at all, which is why
// SwitchDeviceKind::RfMux is a kind no row here uses -- the kind exists in
// generic hal because rigs have them, not because this one does.
//
// TODO(bench): the addresses are invented, as every address and channel in
// this directory is -- there is no physical bench yet. The story they tell is
// at least a coherent one: the three cards are modules in one Racal 1260 VXI
// chassis, so they share that chassis's GPIB address and differ in secondary
// address -- the same "one box, several endpoints" shape DcP1..DcP4 have with
// their mainframe, and the first real use of hal::Gpib's secondary field.
// Note this also settles a question rig/instrument.inc raises about Ser1: if
// that RS232 port turns out to be a module in this same chassis rather than a
// port on the PC, its address becomes another secondary on Gpib( 0, 7) and
// it stops being this repo's hal::Serial example.
//
SWITCH_DEVICES
    SWITCH_DEVICE( Matrix, Matrix2, Gpib( 0, 7, 1))
    SWITCH_DEVICE( Mux,    Mux1,    Gpib( 0, 7, 2))
    SWITCH_DEVICE( Mux,    Mux2,    Gpib( 0, 7, 3))
END_SWITCH_DEVICES
