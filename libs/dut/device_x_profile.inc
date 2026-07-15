// ReSharper disable once CppMissingIncludeGuard

//
// Device X on its standard adapter. Each named point (matching the test
// spec) maps to where it lands on the VPC90 connector array and what kind
// of quantity is expected there. Which instrument actually measures it, and
// by which matrix/mux path, is a separate rig-level fact -- see
// hal::RouteTable -- not something this data knows about.
//
ADAPTER( DeviceX_StdAdapter, "Device X on standard adapter")
    POINT( "5VOutput",  A, 1, 3, Voltage, "5Vdc supply port")
    POINT( "3V3Output", A, 1, 6, Voltage, "3.3Vdc supply port")
    POINT( "ClkProbe",  A, 1, 2, Voltage, "Config clock probe")
    POINT( "Vout",      A, 1, 4, Voltage, "12V rail probe")
END_ADAPTER
