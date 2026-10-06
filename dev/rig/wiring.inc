// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's wiring: one route, from the switch unit's meter to one
// terminal on its matrix.
//
// Dmm2's leads are the 34980A's Analog Bus, which reaches the slot 1 card's
// Matrix 2 through its bus relay; the desk's one point, dut::DeskTerminal, is a
// column of that matrix. Every other instrument on the desk is wired to nothing
// the switching reaches -- reached over its own USB address and not through a
// relay, which is the ordinary state of an instrument on a desk -- so
// hal::InstrumentWiring::find() still throws for those, and correctly: there
// is no route to ask for. One of them is cabled onto the terminal, though --
// the supply's DcP7, permanently -- which SOURCE_WIRING records. TAP_WIRING is
// empty, for the reason given there.
//
// Written out as four blocks even where empty, and that is not a formality:
// hal::instrumentWiring, hal::connectorWiring, hal::sourceWiring and
// hal::tapWiring are defined by these expansions and declared elsewhere, so a
// missing block is a link error rather than an empty table.
//
// -- How to read a row ------------------------------------------------------------
//
//   Slot1 .. Slot5         the 34980A's cards, by the slot they sit in
//                          (devices.inc): Slot1-4 are 34932A matrices, Slot5
//                          the 34941A RF mux.
//
//   ROW_COLUMN( Slot1, 5, 1)
//                          the crosspoint joining R5 to C1 on the slot 1 card
//                          -- channel 501. A 34932A is two 4 x 16 matrices
//                          sharing one row axis: R1-R4 are its Matrix 1, R5-R8
//                          its Matrix 2. The column is the matrix's own, C1-C16
//                          on each, and the row says which matrix it is on.
//                          The C<n> terminals are where a cable goes.
//
//   ANALOG_BUS( Slot1, 1)  the relay that joins the mainframe's Analog Bus 1
//                          (ABus1, the internal DMM's input) to that card --
//                          channel 921. ABus1-4 land on Matrix 2's rows 5-8,
//                          one bus per row, which is why only Matrix 2 rows can
//                          reach the meter.
//
//   The 34932T, as printed
//                          one block per card, split in two halves, each with
//                          two screw plints of Hi / Lo pairs. The left half is
//                          marked "Matrix 1", the right "ABUS Matrix 2" (its
//                          rows are the ones the Analog Buses reach):
//
//                            Matrix 1                    ABUS Matrix 2
//                            R1 R2  C1  C2  C3  C4       C1  C2  C3  C4   R5 R6
//                            R3 R4  C5  C6  C7  C8       C5  C6  C7  C8   R7 R8
//
//                            R1 R2  C9  C10 C11 C12      C9  C10 C11 C12  R5 R6
//                            R3 R4  C13 C14 C15 C16      C13 C14 C15 C16  R7 R8
//
//                          The rows are on both plints of a half -- the upper
//                          plint's R5 and the lower's are the same row. So a
//                          terminal is named here as the print names it:
//                          "ABUS Matrix 2 C1" is ROW_COLUMN( Slot1, 5..8, 1),
//                          the upper plint's first column on the right half,
//                          and "Matrix 1 C1" a different pair of screws
//                          altogether, ROW_COLUMN( Slot1, 1..4, 1).
//
//   WIRE_CONNECTOR( A, 1, 1, ...)
//                          a DUT point's place on the fixture, as rack,
//                          connector, pin of a VPC receiver -- on the
//                          production bench, the pin a test adapter lands on.
//                          This desk has no receiver, so here it is only the
//                          name dev/dut/adapter.inc gives the one terminal.
//                          Which point it is stands in the row's comment.
//
//
// -- The switch unit's own meter, onto the matrix -------------------------------
//
// Dmm2 is the 34980A's internal DMM, and its input is the mainframe's Analog
// Bus 1. Nothing to cable: the bus is the backplane. Closing the slot 1
// card's ABus1 relay puts the meter on that card's R5 (the first row of its
// ABUS Matrix 2 half), and from there any of that half's C1-C16 is one
// crosspoint away (see CONNECTOR_WIRING below).
//
INSTRUMENT_WIRING
    WIRE_INSTRUMENT( Dmm2, ANALOG_BUS( Slot1, 1))   // the meter's input, ABus1, onto R5 of the slot 1 card -- no cable, backplane only
END_INSTRUMENT_WIRING

//
// -- The desk's one terminal --------------------------------------------------
//
// dut::DeskTerminal (dev/dut/adapter.inc) is C1 of the slot 1 card's
// 34932T, on its right half, marked "ABUS Matrix 2" -- the Hi and Lo screw
// terminals at C1 on the upper plint. DcP7's output is cabled there and stays
// (see SOURCE_WIRING below).
//
// A routed reading at the point closes ANALOG_BUS( Slot1, 1) and the
// crosspoint below -- R5 onto C1 -- reads the meter, and opens both.
//
CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 1, ROW_COLUMN( Slot1, 5, 1))   // dut::DeskTerminal: slot 1 card's 34932T, ABUS Matrix 2 C1 Hi/Lo -- DcP7 is cabled here
END_CONNECTOR_WIRING

//
// -- The cable that stays on the desk ------------------------------------------
//
// DcP7 -- the EDU36311A's output 3 (Psu1, 30 V / 1 A) -- is cabled from its
// front-panel + and - straight onto dut::DeskTerminal: slot 1 card's 34932T,
// ABUS Matrix 2 C1, Hi and Lo. No relay between them, so no path: a source
// row is only the landing (see hal::SourceWiring).
//
// Recorded rather than left to a comment because the build checks it both
// ways (dev/dut/adapter.inc declares the terminal a SOURCE_POINT to match, and
// dev/dut/tests/test_dev_dut.cpp holds the two together),
// and because the electrical interlock reads it: a low-impedance reading
// routed to the terminal is refused while DcP7 is on. With the supply off it
// is a cold wire on a column nothing else uses -- SwitchUnitDmm's "onto
// nothing" check closes C2 for that reason.
//
SOURCE_WIRING
    WIRE_SOURCE( DcP7, A, 1, 1)   // DcP7 (Psu1 output 3) + / - onto dut::DeskTerminal: slot 1 card's 34932T, ABUS Matrix 2 C1 Hi / Lo
END_SOURCE_WIRING

//
// The fourth table, and the only empty one.
//
// A WIRE_TAP row is
// (instrument, VPC pin) with no path at all (see hal::TapWiring) -- an
// instrument's leads cabled straight onto a pin -- and nothing on this desk is:
// its one point, dut::DeskTerminal, is reached through the switch unit's
// matrix, not by a lead bolted onto it.
//
// That makes this the table to write in if this desk ever grows a fixture.
// Today the order dev/dut/adapter.inc gives is card, then wiring row, then
// POINT, because a point had to be routed to be covered. It no longer does:
// dut/tests/test_wiring_coverage.cpp accepts isWired() *or* isTapWired(), so
// a POINT here plus one WIRE_TAP row below is a complete, checked
// declaration with no switching hardware anywhere in it. What that buys is
// the at( ...) half of the framework in the dev loop -- the coverage check,
// point-keyed session and journal keys, and the electrical interlock if the
// meter and a supply ever share a pin. What it still does not buy is the
// fabric: path composition and hal::SwitchFabric need a real card, and one
// card is still what dev/README.md says to put on the desk for them.
//
TAP_WIRING
END_TAP_WIRING
