// ReSharper disable once CppMissingIncludeGuard

//
// Device X on its standard adapter. Each named point maps to where it lands
// on the VPC90 connector array -- and to nothing else. A point is a pinout
// name; what is measured there is chosen at the Measure() call site by which
// instrument port is pointed at it, so the same pin can be read for voltage,
// current or frequency without being redeclared. Which instrument actually
// measures it, and by which matrix/mux path, is a separate rig-level fact --
// see hal::InstrumentWiring/hal::ConnectorWiring -- not something this data
// knows about.
//
// A handful of these points are also where a fixed-wired source instrument
// (hal::N6701A/hal::DcP1..DcP4, hal::Ac6677A/hal::AcP1) permanently lands --
// Output5V and BackupSupply look like DC rails, AcInput_A/B/C/N look like a
// three-phase-plus-neutral AC input, for instance. hal:: itself has no idea
// any of that -- DcP1's fixed matrix channel (hal/wiring.inc) is a bare
// SwitchElementId with no notion of "Output5V" attached (see hal::N6701A's
// own comment on why) -- so if that association matters to a reader here,
// it belongs as a comment on the specific POINT() below, once it's known
// which DcP/AcP instance is actually cabled to which pin on this rig. Not
// yet recorded below: worth confirming against the schematic before
// asserting it.
//
// Identifiers here can't reuse the spec label verbatim where it starts with
// a digit (C++ identifiers can't start with a digit) -- Output5V/Output3V3
// stand in for the spec's "5VOutput"/"3V3Output".
//
// ADAPTER takes only a description: the group it opens is always `dut`, since
// a build targets one DUT on one adapter (see hal/adapter.hpp's own comment).
// The DUT's name for the logs is THORIUM_DUT_NAME, set in
// libs/core/CMakeLists.txt -- not this file.
//
ADAPTER( "Device X on standard adapter")
    POINT( Output5V,        A, 1, 3, "5Vdc supply port")
    POINT( Output3V3,       A, 1, 6, "3.3Vdc supply port")
    POINT( ClkProbe,        A, 1, 2, "Config clock probe")
    POINT( Vout,            A, 1, 4, "12V rail probe")
    //
    // The three DC rails this bench drives into the DUT, rather than pins
    // the DUT presents -- DcP1/DcP2/DcP3 are cabled straight onto them (see
    // SOURCE_WIRING in rig/wiring.inc for which is which, and
    // suite/scripts/rig_power_on.cpp for the sequence that brings them up).
    // Still ordinary measurable points: each also has a mux tap, and reading
    // the rail at the DUT pin is a stricter check than the supply's own
    // readback, which cannot see cable drop or a bad contact.
    //
    SOURCE_POINT( BackupSupply,    A, 1, 5, "28Vdc backup supply")
    SOURCE_POINT( BackupSupply_2,  A, 1, 7, "28Vdc secondary backup supply")
    SOURCE_POINT( BatterySupply,   A, 1, 8, "24Vdc secondary backup supply")

    //
    // The one multi-wire interface on this adapter: four pins that are one
    // three-phase input, not four unrelated points that happen to be
    // adjacent. Written as four separate POINTs (AcInput_A/B/C/N) until
    // BUNDLE existed -- with the fact that they belong together carried
    // only by the shared name prefix, which nothing could check and nothing
    // could act on.
    //
    // Worth reading next to hal::AcP1's four WIRE_INSTRUMENT rows in
    // rig/wiring.inc, which are the instrument-side half of this same
    // interface -- three phases plus the neutral/ground return, closed and
    // opened as one unit by hal::InstrumentWiring::findAll(). The fabric
    // side has modelled this as one thing for a while; this is the pinout
    // side catching up.
    //
    // Still four ordinary points: each line is measurable on its own and
    // gets every per-pin check a top-level POINT does (see
    // core::AdapterBundleTag). SOURCE_LINEs rather than LINEs because AcP1
    // is cabled onto all four -- the same reason the DC rails above are
    // SOURCE_POINTs, and recorded the same way in rig/wiring.inc.
    //
    BUNDLE( AcInput, "Three-phase AC input")
        SOURCE_LINE( A,     A, 3, 1, "AC input, phase A")
        SOURCE_LINE( B,     A, 3, 3, "AC input, phase B")
        SOURCE_LINE( C,     A, 3, 5, "AC input, phase C")
        SOURCE_LINE( N,     A, 3, 7, "AC input, neutral")
    END_BUNDLE
END_ADAPTER
