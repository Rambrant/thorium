// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's switching hardware: the five cards of one 34980A.
//
// This is the file that says what kind of deployment this is, and for a long
// time it said "none": a desk with a meter on it had no matrix, no mux and no
// relay cards, so every reading here was an instrument readback and the routed
// half of the framework -- path composition, hal::SwitchFabric, the electrical
// interlock -- was not exercised by this deployment at all.
//
// It is now, and the cards below are why. They are the 34980A's switching
// faces, on box Swu1, and the fabric drives them: a routed reading at a point
// dev/rig/wiring.inc reaches closes real relays through the box's Chassis
// instrument row, reads, and opens them (see hal/fabric/rig_switching.hpp). The
// readings that need no route -- every other instrument on the desk -- are
// still point-free readbacks, and that is not a gap: those instruments' leads
// go nowhere the switching reaches.
//
//
// -- The switch unit's cards ----------------------------------------------------
//
// The 34980A in box Swu1 is a switching device as well as two instruments, and
// these are its switching faces: one row per card, on the same box -- so the
// same address, which the build holds them to (hal/topology/boxes.hpp) -- with
// the card's slot as its Card( n). The fabric moves these cards' relays through
// the instrument on box Swu1 that switches cards, the Chassis row in
// instrument.inc, down the box's one session (see hal/fabric/rig_switching.hpp).
//
// Matrix1-4 are the four 34932A dual 4 x 16 matrices (34932T terminal blocks);
// RfMux1 is the 34941A quad 1 x 4 RF multiplexer. Named for what they are, not
// for the bench's Racal cards: a script that routes through Matrix1 here is
// routing through a 34932A, whose channel numbers (<row><column>, one-based)
// are not the Racal 1260-45A's.
//
SWITCH_DEVICES
    SWITCH_DEVICE( Swu1, Keysight34932A, Matrix1, Usb( "MY53154781"), Card( 1))
    SWITCH_DEVICE( Swu1, Keysight34932A, Matrix2, Usb( "MY53154781"), Card( 2))
    SWITCH_DEVICE( Swu1, Keysight34932A, Matrix3, Usb( "MY53154781"), Card( 3))
    SWITCH_DEVICE( Swu1, Keysight34932A, Matrix4, Usb( "MY53154781"), Card( 4))
    SWITCH_DEVICE( Swu1, Keysight34941A, RfMux1,  Usb( "MY53154781"), Card( 5))
END_SWITCH_DEVICES
