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
    // DcP1/DcP2 have no entry here at all -- they're hal::N6701ADirect,
    // wired straight through with no isolation relay, nothing for
    // Connect/Disconnect to close or open (see hal::N6701A's own comment
    // on DirectWiring). Only DcP3/DcP4 (hal::N6701ARelay) have a real
    // fixed channel.
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
    // TODO(bench): these two channel numbers are the next free Mux1 channels,
    // chosen rather than read off a rig -- as is every assignment in this file
    // (there is no physical bench yet; see SOURCE_WIRING's own TODO below).
    // They are singled out only because they were once the sole invented pair
    // in an otherwise-real table. What they now say is consistent with the
    // rest: BackupSupply_2/BatterySupply are cabled rails (SOURCE_WIRING
    // below) that also carry a tap, exactly as BackupSupply on A-1-6 does.
    WIRE_CONNECTOR( A, 1, 7, HOP( Mux, "Mux1", 7))
    WIRE_CONNECTOR( A, 1, 8, HOP( Mux, "Mux1", 8))
    WIRE_CONNECTOR( A, 3, 1, HOP( Mux, "Mux2", 2))
    WIRE_CONNECTOR( A, 3, 3, HOP( Mux, "Mux2", 3))
    WIRE_CONNECTOR( A, 3, 5, HOP( Mux, "Mux2", 4))
    WIRE_CONNECTOR( A, 3, 7, HOP( Mux, "Mux2", 5))
END_CONNECTOR_WIRING

//
// Which VPC pin each fixed-wired source instrument's output is cabled onto
// -- the third static fact, and the one that is not a fabric path at all
// (see hal::SourceWiring's own comment in hal/wiring.hpp). A pin listed here
// is reached by a cable, not by the mux: it gets no WIRE_CONNECTOR entry
// above, and its dut/adapter.inc counterpart is a SOURCE_POINT rather than a
// POINT. dut/tests/test_wiring_coverage.cpp enforces both directions.
//
// TODO(bench): assignments below are taken from what this repo already says,
// not read off a rig -- there is no physical bench yet. Each row matches the
// pairing suite/scripts/rig_power_on.cpp states in its own comments and
// corroborates with the voltage it applies: DcP1 and DcP2 are brought up at
// 28 V against points described as "28Vdc backup supply" and "28Vdc
// secondary backup supply", DcP3 at 24 V against "24Vdc secondary backup
// supply", and AcP1 as a three-phase wye against the four AcInput lines.
// Re-check when a real rig and DUT exist.
//
// A landing pin is NOT thereby unreachable: all eight below also keep their
// WIRE_CONNECTOR rows above. A rail is cabled so the *fabric* never carries
// its load current, which says nothing against a high-impedance mux tap onto
// the same pin -- and reading the rail where it arrives at the DUT is a
// stricter check than the supply's own readback, which cannot see cable drop
// or a bad contact (see rig_power_on.cpp, which spells out that distinction).
// dut/tests/test_wiring_coverage.cpp allows the combination for that reason.
//
// DcP4 appears nowhere below, deliberately: this rig declares it (see
// rig/instrument.inc) but nothing drives it and no adapter point corresponds
// to it -- a spare module in the mainframe. hal::SourceWiring::findAll()
// returns empty for it rather than throwing, which is exactly that case.
//
SOURCE_WIRING
    WIRE_SOURCE( DcP1, A, 1, 5)   // dut::BackupSupply,   28Vdc
    WIRE_SOURCE( DcP2, A, 1, 7)   // dut::BackupSupply_2, 28Vdc
    WIRE_SOURCE( DcP3, A, 1, 8)   // dut::BatterySupply,  24Vdc
    // AcP1 lands on four pins, one row each -- the connector-side counterpart
    // of its four WIRE_INSTRUMENT rows above, and grouped on the adapter side
    // as the dut::AcInput bundle.
    WIRE_SOURCE( AcP1, A, 3, 1)   // dut::AcInput::A, phase A
    WIRE_SOURCE( AcP1, A, 3, 3)   // dut::AcInput::B, phase B
    WIRE_SOURCE( AcP1, A, 3, 5)   // dut::AcInput::C, phase C
    WIRE_SOURCE( AcP1, A, 3, 7)   // dut::AcInput::N, neutral/ground return
END_SOURCE_WIRING
