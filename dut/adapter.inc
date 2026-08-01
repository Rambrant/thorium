// ReSharper disable once CppMissingIncludeGuard

//
// Device X on its standard adapter. Each named point maps to where it lands
// on the VPC90 connector array and what kind of quantity is expected there.
// Which instrument actually measures it, and by which matrix/mux path, is a
// separate rig-level fact -- see hal::InstrumentWiring/hal::ConnectorWiring
// -- not something this data knows about.
//
// A handful of these points are also where a fixed-wired source instrument
// (hal::N6701A/hal::DcP1..DcP4, hal::Ac6677A/hal::AcP1) permanently lands --
// Output5V and Input24V look like DC rails, AcInput_A/B/C/N look like a
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
ADAPTER( DeviceX, "Device X on standard adapter")
    POINT( Output5V,   A, 1, 3, Voltage, "5Vdc supply port")
    POINT( Output3V3,  A, 1, 6, Voltage, "3.3Vdc supply port")
    POINT( ClkProbe,   A, 1, 2, Voltage, "Config clock probe")
    POINT( Vout,       A, 1, 4, Voltage, "12V rail probe")
    POINT( Input24V,   A, 1, 5, Voltage, "24Vdc input supply port")
    POINT( AcInput_A,  A, 3, 1, Voltage, "AC input, phase A")
    POINT( AcInput_B,  A, 3, 3, Voltage, "AC input, phase B")
    POINT( AcInput_C,  A, 3, 5, Voltage, "AC input, phase C")
    POINT( AcInput_N,  A, 3, 7, Voltage, "AC input, neutral")
END_ADAPTER
