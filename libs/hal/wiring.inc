// ReSharper disable once CppMissingIncludeGuard

//
// This rig's two static wiring facts (see hal/wiring.hpp's own comment):
// which matrix/mux channel each instrument is hard-wired to, and which
// matrix/mux channel each VPC connector pin is hard-wired to. Composed
// together at measurement time -- see core/measure.hpp -- rather than
// stored per (instrument, pin) combination.
//
INSTRUMENT_WIRING
    WIRE_INSTRUMENT( Dmm1, Matrix, "Matrix2", 14)
    WIRE_INSTRUMENT( Dmm2, Matrix, "Matrix2", 16)
    WIRE_INSTRUMENT( Osc1, Matrix, "Matrix2", 10)
END_INSTRUMENT_WIRING

CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 3, Mux, "Mux1", 3)
    WIRE_CONNECTOR( A, 1, 6, Mux, "Mux1", 4)
    WIRE_CONNECTOR( A, 1, 2, Mux, "Mux2", 1)
    WIRE_CONNECTOR( A, 1, 4, Mux, "Mux1", 5)
END_CONNECTOR_WIRING
