#include "scripts/scripts.hpp"

#include "dsl/criterion.hpp"
#include "dsl/predicates.hpp"
#include "dsl/verify.hpp"

// A criteria table, declared once at file scope and reused across runs.
// This mirrors the original prototype's GROUP/CRIT usage, now against real
// register addresses on dut::Device.
GROUP(FS_Fuse_6, "Check of Fuses @ Register CB30")

    CRIT(FS_Fuse_01, dsl::BAND(0x0Fu, 0x05u), "Low nibble of fuse register must be 0x5")
    CRIT(FS_Fuse_02, dsl::EQ(0xF5u), "Fuse register must read exactly 0xF5")

END_GROUP

namespace scripts {

bool fuse_register_script(dut::Device& device)
{
    bool all_passed = true;

    const auto fuse_value = device.read_fuse_register();
    all_passed &= dsl::Verify(FS_Fuse_6::FS_Fuse_01, fuse_value);
    all_passed &= dsl::Verify(FS_Fuse_6::FS_Fuse_02, fuse_value);

    // Ad-hoc check (no CRIT constant declared up front) against a measured
    // quantity. Note we compare against .value() (a raw double) rather than
    // a core::Voltage directly -- EqPredicate's tolerance logic operates on
    // primitive floating-point types, so quantities get unwrapped at the
    // point they're checked.
    const auto voltage = device.measure_output_voltage();
    all_passed &= dsl::Verify("Voltage", "Vout", "Supply voltage",
                               dsl::EQ(12.0).within(0.05), voltage.value());

    return all_passed;
}

}  // namespace scripts
