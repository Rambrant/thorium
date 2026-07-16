#pragma once

namespace core
{
    //
    // A thin wrapper marking a value as a DUT-point argument at the call
    // site, mirroring the old test-spec syntax's "at=..." attribute. Not a
    // conversion helper -- its whole job is to look like a keyword in
    // source code, so a call like
    //
    //     Measure( Dmm1.voltage(), at( Output5V));
    //     Apply(   DcN6701A.dc(    at( Output5V)).voltage( 24_V));
    //
    // reads unambiguously as "at this DUT point", the same way _V/_A make a
    // bare literal read unambiguously as a Quantity. Measure/Apply/Remove
    // and their builders take At<...>, not AdapterPointTag<...> directly,
    // so a bare Output5V without at() is a compile error suggesting the
    // fix.
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
