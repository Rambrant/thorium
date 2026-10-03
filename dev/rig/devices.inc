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
// Named for the slot they sit in, because that is what a person standing at
// the mainframe can find: Slot1-4 are the four 34932A dual 4 x 16 matrices
// (34932T terminal blocks), Slot5 the 34941A quad 1 x 4 RF multiplexer. Not
// Matrix1-4, which they were called first: each 34932A holds a Matrix 1 and a
// Matrix 2 of its own (rows 1-4 and 5-8), so "Matrix1 row 5" read as a
// contradiction where "Slot1 row 5" reads as a place. The model column says
// what each card is; the name only has to say where.
//
SWITCH_DEVICES
    SWITCH_DEVICE( Swu1, Keysight34932A, Slot1, Usb( "MY53154781"), Card( 1))   // dual 4 x 16 matrix, 34932T terminal block
    SWITCH_DEVICE( Swu1, Keysight34932A, Slot2, Usb( "MY53154781"), Card( 2))   // dual 4 x 16 matrix, 34932T terminal block
    SWITCH_DEVICE( Swu1, Keysight34932A, Slot3, Usb( "MY53154781"), Card( 3))   // dual 4 x 16 matrix, 34932T terminal block
    SWITCH_DEVICE( Swu1, Keysight34932A, Slot4, Usb( "MY53154781"), Card( 4))   // dual 4 x 16 matrix, 34932T terminal block
    SWITCH_DEVICE( Swu1, Keysight34941A, Slot5, Usb( "MY53154781"), Card( 5))   // quad 1 x 4 RF multiplexer, SMA
END_SWITCH_DEVICES
