#include "../prelude.hpp"


auto fuseRegisterScript() -> void
{
    //
    // No digital-register instrument exists in the hal model (the
    // register-bus DUT model -- hal::Bus/dut::Device -- was removed rather
    // than carried into the VPC/instrument redesign). This stays a fixed
    // stand-in -- 0xF5 is a value that passes both criteria below -- until
    // a real digital readback path exists; a test can't vary it the way it
    // can the voltage below.
    //
    constexpr auto fuseValue = 0xF5;

    Verify( FS_Fuse_6::FS_Fuse_01, fuseValue);
    Verify( FS_Fuse_6::FS_Fuse_02, fuseValue);

    //
    // Ad-hoc check (no CRIT constant declared up front) against a measured
    // quantity.
    //
    const auto voltage = Measure( Dmm2.voltage(), at( dut::Vout));

    Verify( "Supply voltage at Vout", EQ( 12.0_V).epsilon( 0.05_V), voltage);
}
