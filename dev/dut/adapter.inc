// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench has no DUT, and one point: a terminal the switching reaches.
//
// ADAPTER opens the fixed `dut` struct every script's at( ...) resolves through
// (see hal/topology/adapter.hpp), so it is here even with nothing much in it.
// A POINT names a place a rig can reach -- dev/rig/tests and the bench's
// dut/tests/test_wiring_coverage.cpp hold every point to a wiring row that
// reaches it -- and there are two ways a rig can: a connector-wiring row
// through the fabric, which is how the point below is reached, or a tap row
// saying an instrument's leads are cabled straight onto the pin (see
// hal::TapWiring), which needs no card at all.
//
// Most of what a dev script measures is still the instrument itself, with no
// at( ...) and nothing closed -- see core::MeasureEngine's point-free
// operator(). The point below is for the one reading that is routed: the
// switch unit's meter, through its matrix, at the terminal the supply is
// cabled onto.
//
//
// -- The desk's one point -------------------------------------------------------
//
// One terminal the switching can reach: C1 on the "ABUS Matrix 2" half of the
// slot 1 34932A's 34932T terminal block, which dev/rig/wiring.inc routes from
// the 34980A's own meter. Not a DUT -- the desk still has none -- but a named place,
// which is all a point has to be for at( ...) to reach it.
//
// A SOURCE_POINT, because DcP7 is cabled onto it and stays there -- the one
// WIRE_SOURCE row in dev/rig/wiring.inc, which dev/dut/tests/test_dev_dut.cpp
// holds this declaration to, in both directions (as the bench's own
// dut/tests/test_wiring_coverage.cpp does for its points). Still measurable like any
// point; what it says is that something drives it.
//
ADAPTER( "Dev bench -- an instrument on a desk, no adapter and no DUT")
    SOURCE_POINT( DeskTerminal, A, 1, 1, "Slot 1 34932T, ABUS Matrix 2 C1 Hi/Lo -- DcP7 is cabled here")
END_ADAPTER
