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
    WIRE_INSTRUMENT( DcP1, Matrix, "Matrix2", 20)
    WIRE_INSTRUMENT( DcP2, Matrix, "Matrix2", 21)
    WIRE_INSTRUMENT( DcP3, Matrix, "Matrix2", 24)
    WIRE_INSTRUMENT( DcP4, Matrix, "Matrix2", 25)
    WIRE_INSTRUMENT( AcP1, Matrix, "Matrix2", 22)
END_INSTRUMENT_WIRING

CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 3, Mux, "Mux1", 3)
    WIRE_CONNECTOR( A, 1, 6, Mux, "Mux1", 4)
    WIRE_CONNECTOR( A, 1, 2, Mux, "Mux2", 1)
    WIRE_CONNECTOR( A, 1, 4, Mux, "Mux1", 5)
    WIRE_CONNECTOR( A, 1, 5, Mux, "Mux1", 6)
    WIRE_CONNECTOR( A, 3, 1, Mux, "Mux2", 2)
    WIRE_CONNECTOR( A, 3, 3, Mux, "Mux2", 3)
    WIRE_CONNECTOR( A, 3, 5, Mux, "Mux2", 4)
    WIRE_CONNECTOR( A, 3, 7, Mux, "Mux2", 5)
END_CONNECTOR_WIRING
