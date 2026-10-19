#pragma once

#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/quantities/format.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

#include "hal/keysight_33522b/builder.hpp"
#include "hal/keysight_33522b_waveform_generator.hpp"

//
// Channel<N> and its entry points
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
    //
    // One of the 33522B's two output channels, narrowed at compile time via
    // WaveformGenerator::channel<N>(). It is the one place a channel number is written
    // in a script, and each of its methods opens a chain for one shape:
    //
    //     Apply( Wfg1.channel<2>().sine().frequency( 1_kHz).amplitude( 2_V));
    //     Remove( Wfg1.channel<2>().sine());
    //
    // There is deliberately no config() here, only on the builder -- so
    // Apply( Wfg1.channel<1>()), an Apply naming an output and no waveform at
    // all, is "no matching function" rather than a call that does nothing.
    //
    // A thin, transient view over WaveformGenerator &, and nothing it returns holds a
    // reference to *it*: every method builds a WaveformBuilder bound to the
    // real instrument, with N copied into the config by value. That is the
    // half of the scope driver's channel arrangement worth keeping and the
    // half worth not -- hal::keysight_dsox1202g::Channel's own comment records
    // a dangling-reference bug from a Port that bound to the view, and its
    // remaining sharp edge (which channel is current is instrument state) does
    // not exist here, because a waveform generator has nothing to read back
    // and so needs no Port at all.
    //
    // The methods are declared here and defined below Wfg33522B: every one of
    // them needs the instrument to be a complete type.
    //
    template<unsigned N>
        requires ValidChannel<N>
    class Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit Channel( WaveformGenerator & instrument) : mInstrument( instrument) {}

            [[nodiscard]] auto sine()     const -> WaveformBuilder<Sine>;
            [[nodiscard]] auto square()   const -> WaveformBuilder<Square>;
            [[nodiscard]] auto ramp()     const -> WaveformBuilder<Ramp>;
            [[nodiscard]] auto triangle() const -> WaveformBuilder<Triangle>;
            [[nodiscard]] auto pulse()    const -> WaveformBuilder<Pulse>;
            [[nodiscard]] auto noise()    const -> WaveformBuilder<Noise>;
            [[nodiscard]] auto dc()       const -> WaveformBuilder<Dc>;

        private:
            WaveformGenerator & mInstrument;
    };

    // ---------------------------------------------------------------------
    // Channel<N>'s entry points
    // ---------------------------------------------------------------------
    //
    // Below Wfg33522B rather than inside Channel, because every one of these
    // needs the instrument to be a complete type.
    //

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::sine() const -> WaveformBuilder<Sine>
    {
        return WaveformBuilder<Sine>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::square() const -> WaveformBuilder<Square>
    {
        return WaveformBuilder<Square>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::ramp() const -> WaveformBuilder<Ramp>
    {
        return WaveformBuilder<Ramp>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::triangle() const -> WaveformBuilder<Triangle>
    {
        return WaveformBuilder<Triangle>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::pulse() const -> WaveformBuilder<Pulse>
    {
        return WaveformBuilder<Pulse>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::noise() const -> WaveformBuilder<Noise>
    {
        return WaveformBuilder<Noise>{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::dc() const -> WaveformBuilder<Dc>
    {
        return WaveformBuilder<Dc>{ mInstrument, N };
    }
} // namespace hal::keysight_33522b
