// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's wiring: one route, from the switch unit's meter to one
// terminal on its matrix.
//
// Dmm2's leads are the 34980A's Analog Bus, which reaches slot 1's Matrix 2
// through its bus relay; the desk's one point, dut::DeskTerminal, is a column
// of that matrix. Every other instrument on the desk is wired to nothing the
// switching reaches -- reached over its own USB address and not through a
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
//
// -- The switch unit's own meter, onto the matrix -------------------------------
//
// Dmm2 is the 34980A's internal DMM, and its leads are the mainframe's Analog
// Buses: its input is ABus1, which reaches a 34932A through that card's bus
// relay for a Matrix 2 row -- 921 puts row 5 on ABus1. So Dmm2's own path is
// that one relay on the card in slot 1, and a route to any column of Matrix 2
// row 5 is this plus the crosspoint (see CONNECTOR_WIRING below).
//
INSTRUMENT_WIRING
    WIRE_INSTRUMENT( Dmm2, HOP( Matrix1, 921))   // ABus1 onto row 5 of slot 1's Matrix 2
END_INSTRUMENT_WIRING

//
// -- The desk's one terminal --------------------------------------------------
//
// dut::DeskTerminal (dev/dut/adapter.inc): column 1 of slot 1's Matrix 2, on
// its 34932T terminal block. Reached from row 5 by crosspoint 501 -- so a
// routed reading there closes 921 and 501, reads ABus1, and opens them again.
// The "pin" A/1/1 is a name for that terminal; the desk has no VPC receiver.
//
CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 1, ROW_COLUMN( Matrix1, 5, 1))   // dut::DeskTerminal -- slot 1, Matrix 2 column 1
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
