#pragma once

#include <string_view>

#include "core/criterion.hpp"
#include "core/quantity.hpp"
#include "../../../core/include/core/verify.hpp"
#include "dut/rig_device.hpp"

namespace dsl
{
    //
    // Measure is the single point of contact for a test-script author. A script
    // says *what* it wants -- "check this criterion at this test point" -- and
    // Measure orchestrates the select-instrument / close-crosspoint / fetch
    // sequence via the DUT, then runs the criterion's predicate against the
    // reading. The author never touches an instrument, a crosspoint, or the rig.
    //
    // The dut::DeviceView is injected (constructed at the composition root), so
    // the dsl layer orchestrates measurement without selecting the concrete rig
    // or instruments -- keeping it unit-testable against a fake device.
    //
    //   Measure( device, FS_Supply::FS_Vout_01, "5Vdc_port");
    //
    // where FS_Vout_01 is a CRIT-declared criterion whose predicate is checked
    // against the measured voltage (in volts).
    //
    template<typename Predicate>
        requires core::PredicateFor< Predicate, double>
    auto Measure( dut::DeviceView &                  device,
                  const core::Criterion<Predicate> & criterion,
                  std::string_view                   testPoint ) -> bool
    {
        const auto reading = device.measure( testPoint);

        if( ! reading)
        {
            //
            // Point unknown or instrument unavailable: report as a failed check
            // rather than silently passing.
            //
            core::detail::reportResult( criterion.group, criterion.id, criterion.description, false);

            return false;
        }

        //
        // Predicates operate on primitive doubles (EqPredicate's tolerance logic
        // is defined for floating-point), so the quantity is unwrapped here.
        //
        return Verify( criterion, reading->value());
    }
} // namespace dsl
