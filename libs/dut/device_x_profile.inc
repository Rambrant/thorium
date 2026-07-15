// ReSharper disable once CppMissingIncludeGuard

//
// Device X on its standard adapter. Each named point maps to where it lands
// on the VPC90 connector array and what kind of quantity is expected there.
// Which instrument actually measures it, and by which matrix/mux path, is a
// separate rig-level fact -- see hal::InstrumentWiring/hal::ConnectorWiring
// -- not something this data knows about.
//
// Identifiers here can't reuse the spec label verbatim where it starts with
// a digit (C++ identifiers can't start with a digit) -- Output5V/Output3V3
// stand in for the spec's "5VOutput"/"3V3Output".
//
ADAPTER( DeviceX_StdAdapter, "Device X on standard adapter")
    POINT( Output5V,  A, 1, 3, Voltage, "5Vdc supply port")
    POINT( Output3V3, A, 1, 6, Voltage, "3.3Vdc supply port")
    POINT( ClkProbe,  A, 1, 2, Voltage, "Config clock probe")
    POINT( Vout,      A, 1, 4, Voltage, "12V rail probe")
END_ADAPTER
