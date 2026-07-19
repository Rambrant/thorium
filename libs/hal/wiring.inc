// ReSharper disable once CppMissingIncludeGuard

//
// This rig's two static wiring facts (see hal/wiring.hpp's own comment):
// which matrix/mux channel each instrument is hard-wired to, and which
// matrix/mux channel each VPC connector pin is hard-wired to. Composed
// together at measurement time -- see core/measure.hpp -- rather than
// stored per (instrument, pin) combination.
//
INSTRUMENT_WIRING
    WIRE_INSTRUMENT( Dmm1, HOP( Matrix, "Matrix2", 14))
    WIRE_INSTRUMENT( Dmm2, HOP( Matrix, "Matrix2", 16))
    WIRE_INSTRUMENT( Osc1, HOP( Matrix, "Matrix2", 10))
    WIRE_INSTRUMENT( DcP1, HOP( Matrix, "Matrix2", 20))
    WIRE_INSTRUMENT( DcP2, HOP( Matrix, "Matrix2", 21))
    WIRE_INSTRUMENT( DcP3, HOP( Matrix, "Matrix2", 24))
    WIRE_INSTRUMENT( DcP4, HOP( Matrix, "Matrix2", 25))
    // AcP1 is fixed-wired on four channels, not one -- phases A/B/C plus
    // the neutral/ground return, all closed and opened together (see
    // hal::Ac6677A's own comment on why the return is included, and
    // hal::InstrumentWiring::findAll() for how a script-facing Connect/
    // Disconnect gets all four back from one InstrumentId). Each is its
    // own single-hop entry here -- four independent Paths under one
    // InstrumentId, not one four-element Path -- see findAll()'s own
    // comment for why that distinction matters.
    WIRE_INSTRUMENT( AcP1, HOP( Matrix, "Matrix2", 22)) // phase A
    WIRE_INSTRUMENT( AcP1, HOP( Matrix, "Matrix2", 23)) // phase B
    WIRE_INSTRUMENT( AcP1, HOP( Matrix, "Matrix2", 26)) // phase C
    WIRE_INSTRUMENT( AcP1, HOP( Matrix, "Matrix2", 27)) // neutral/ground return
END_INSTRUMENT_WIRING

CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 3, HOP( Mux, "Mux1", 3))
    WIRE_CONNECTOR( A, 1, 6, HOP( Mux, "Mux1", 4))
    WIRE_CONNECTOR( A, 1, 2, HOP( Mux, "Mux2", 1))
    WIRE_CONNECTOR( A, 1, 4, HOP( Mux, "Mux1", 5))
    WIRE_CONNECTOR( A, 1, 5, HOP( Mux, "Mux1", 6))
    WIRE_CONNECTOR( A, 3, 1, HOP( Mux, "Mux2", 2))
    WIRE_CONNECTOR( A, 3, 3, HOP( Mux, "Mux2", 3))
    WIRE_CONNECTOR( A, 3, 5, HOP( Mux, "Mux2", 4))
    WIRE_CONNECTOR( A, 3, 7, HOP( Mux, "Mux2", 5))
END_CONNECTOR_WIRING
