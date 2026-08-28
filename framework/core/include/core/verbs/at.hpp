#pragma once

namespace core
{
    //
    // A thin wrapper marking a value as a DUT-point argument at the call
    // site, mirroring the old test-spec syntax's "at=..." attribute. Not a
    // conversion helper -- its whole job is to look like a keyword in
    // source code, so a call like
    //
    //     Measure( Dmm1.voltage(),            at( Output5V));
    //     Measure( Osc1.channel<3>().vpp(),   at( ClkProbe));
    //
    // reads unambiguously as "at this DUT point", the same way _V/_A make a
    // bare literal read unambiguously as a Quantity.
    //
    // Measure is the only verb that takes one, and that is the point rather
    // than an omission: at() names which of many pins to route to, so it
    // belongs exactly where there is a choice. A source instrument has none --
    // DcP1/AcP1 are cabled straight to their pins (see hal::N6701A), so Apply/
    // Remove/Connect/Disconnect take a builder and nothing else -- and an
    // instrument readback like DcP1.measuredVoltage() never leaves the
    // instrument, so that Measure overload takes no point either.
    //
    // Measure takes At<...>, not AdapterPointTag<...> directly, so a bare
    // Output5V without at() is a compile error suggesting the fix.
    //
    template<typename PointT>
    struct At
    {
        PointT point;
    };

    //
    // The free function that actually reads as "at(point)" at the call
    // site. Deduces PointT so any AdapterPointTag (or eventually a
    // composite-point type) works with the same spelling.
    //
    template<typename PointT>
    [[nodiscard]]
    constexpr auto at( const PointT & point) -> At<PointT>
    {
        return At<PointT>{ point };
    }
} // namespace core
