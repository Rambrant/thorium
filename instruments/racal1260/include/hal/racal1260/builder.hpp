#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/quantities/bytes.hpp"
#include "core/driver/describe.hpp"
#include "core/verbs/transfer.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/builder.hpp"
#include "hal/topology/bundle.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/racal1260/vocabulary.hpp"

//
// Rs232Config + Rs232Builder
//
// Part of hal/racal1260.hpp -- include that, not this.
//

namespace hal::racal1260
{
    //
    // What one Setup/Write/Read/Connect call on this port boils down to.
    //
    // Every field optional, meaning "leave whatever is already configured"
    // -- the same convention core::MeasureSetup uses on the sensing side, and
    // for the same reason: a bare Ser1.rs232() has to be a valid thing to hand
    // Write or Disconnect, which have no interest in the baud rate at all.
    //
    // No location here, unlike the settings: which DUT interface this port is
    // routed to is chosen per Connect call (see connectDriver below), not
    // stored on the config. That is the difference between this instrument and
    // hal::keysight_edu36311a::DcSource, whose output is cabled to one pin and so has nothing to
    // choose -- a switched port can reach any interface the fabric wires up,
    // and which one it reached is a fact about a moment in the script rather
    // than about the instrument.
    //
    struct Rs232Config
    {
        Racal1260 &                                Instrument;
        std::optional<unsigned>                    BaudRate{};
        std::optional<unsigned>                    WordLength{};
        std::optional<Parity>                      ParityMode{};
        std::optional<StopBits>                    Stop{};

        //
        // What a Read stops at, and how long it waits. Both belong to the
        // *read*, not to the port's framing, but they ride on the same config
        // because a builder chain is what a script has in its hand at the call
        // site -- Ser1.rs232().terminator( "\r") reads better than a second
        // argument to Read whose meaning has to be remembered.
        //
        core::Bytes                                Terminator{};
        std::optional<std::chrono::milliseconds>   Timeout{};
    };

    //
    // The fluent chain a script builds before handing it to a verb. The
    // copy-modify-return shape is hal::ConfigBuilder's (see
    // hal/driver/builder.hpp), shared with every other driver's builder and
    // written the same way as core::Port's setup builders, so "how do I set X"
    // reads the same whether X is sourced, sensed or framed.
    //
    class Rs232Builder : public ConfigBuilder<Rs232Builder, Rs232Config>
    {
        public:
            explicit Rs232Builder( Racal1260 & instrument) : ConfigBuilder( Rs232Config{ instrument }) {}

            [[nodiscard]]
            auto baudRate( const unsigned rate) const
            {
                return with( &Rs232Config::BaudRate, rate);
            }

            [[nodiscard]]
            auto wordLength( const unsigned bits) const
            {
                return with( &Rs232Config::WordLength, bits);
            }

            [[nodiscard]]
            auto parity( const Parity mode) const
            {
                return with( &Rs232Config::ParityMode, mode);
            }

            [[nodiscard]]
            auto stopBits( const StopBits bits) const
            {
                return with( &Rs232Config::Stop, bits);
            }

            //
            // What a Read stops at. The terminator is deliberately still
            // *included* in what Read hands back, rather than stripped: whether
            // a reply ended the way the protocol says is a thing a criterion
            // may well want to check, and a driver that quietly removed the
            // evidence would make "the DUT answered OK" and "the DUT answered
            // OK and then stopped talking" indistinguishable. Stripping it is
            // one call at the script's own discretion -- see
            // core::Bytes::before.
            //
            [[nodiscard]]
            auto terminator( core::Bytes value) const
            {
                return with( &Rs232Config::Terminator, std::move( value));
            }

            [[nodiscard]]
            auto timeout( const std::chrono::milliseconds limit) const
            {
                return with( &Rs232Config::Timeout, limit);
            }
    };
} // namespace hal::racal1260
