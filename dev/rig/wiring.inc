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
// is no route to ask for. SOURCE_WIRING and TAP_WIRING are empty below, each
// for its own reason.
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
//                          the crosspoint joining row 5 to column 1 on the slot
//                          1 card -- channel 501. A 34932A is two 4 x 16
//                          matrices sharing one row axis: rows 1-4 are its
//                          Matrix 1, rows 5-8 its Matrix 2. Columns 1-16 are
//                          the terminals a cable goes to.
//
//   ANALOG_BUS( Slot1, 1)  the relay that joins the mainframe's Analog Bus 1
//                          (ABus1, the internal DMM's input) to that card --
//                          channel 921. ABus1-4 land on Matrix 2's rows 5-8,
//                          one bus per row, which is why only Matrix 2 rows can
//                          reach the meter.
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
// card's ABus1 relay puts the meter on that card's row 5 (its Matrix 2's first
// row), and from there any column of row 5 is one crosspoint away (see
// CONNECTOR_WIRING below).
//
INSTRUMENT_WIRING
    WIRE_INSTRUMENT( Dmm2, ANALOG_BUS( Slot1, 1))   // the meter's input, ABus1, onto row 5 of the slot 1 card -- no cable, backplane only
END_INSTRUMENT_WIRING

//
// -- The desk's one terminal --------------------------------------------------
//
// dut::DeskTerminal (dev/dut/adapter.inc) is column 1 of the slot 1 card,
// on its 34932T terminal block -- the screw terminal for Matrix 2 column 1.
// That is where a cable goes: SwitchUnitWired puts DcP7's output there.
//
// A routed reading at the point closes ANALOG_BUS( Slot1, 1) and the
// crosspoint below -- row 5 onto column 1 -- reads the meter, and opens both.
//
CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 1, ROW_COLUMN( Slot1, 5, 1))   // dut::DeskTerminal: slot 1 card's 34932T, Matrix 2 column 1 terminal -- cable DcP7 here
END_CONNECTOR_WIRING

SOURCE_WIRING
END_SOURCE_WIRING

//
// The fourth table, and empty like the other three -- but for a different
// reason, which is the only interesting thing about it.
//
// Empty for a different reason from SOURCE_WIRING. A WIRE_TAP row is
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
