#pragma once

#include <string_view>

namespace hal
{
    //
    // The fixed, static list of instruments this rig actually has. Several
    // instruments can share the same underlying type (e.g. Dmm1 and Dmm2
    // below are both hal::L4411A -- see hal/l4411a.hpp) and be interchanged
    // during a test -- InstrumentId is what a hal::RouteTable entry and a
    // recorded sample identify the instrument by, since two Dmms are two
    // distinct wiring facts even though they're the same C++ type.
    //
    // PowerSupply1/PowerSupply has been retired in favor of DcP1..DcP4/AcP1
    // below -- concrete, named source instruments with a real
    // Apply(...)/Remove(...) builder (see hal/n6701a.hpp, hal/ac6677a.hpp)
    // rather than the old ad-hoc setOutput()/enable() pair. DcP1..DcP4 name
    // the role ("DC power, channel N"), not the model -- the four channels
    // of one physical N6701A mainframe -- the same way Dmm1/Dmm2 don't
    // encode which literal DMM model they are; see hal::N6701A's own
    // comment in hal/n6701a.hpp for why the class itself is model-named
    // even though these ids aren't.
    //
    // Osc1 is likewise no longer the old generic hal::Oscilloscope
    // placeholder -- it's now a hal::DSO8064 (see hal/dso8064.hpp), the same
    // retirement hal::L4411A gave the old generic hal::Dmm, once the real
    // scope model plugged into this rig was known.
    //
    enum class InstrumentId
    {
        Osc1,
        Dmm1,
        Dmm2,
        DcP1,
        DcP2,
        DcP3,
        DcP4,
        AcP1
    };

    [[nodiscard]]
    auto to_string( InstrumentId id) -> std::string_view;
} // namespace hal
