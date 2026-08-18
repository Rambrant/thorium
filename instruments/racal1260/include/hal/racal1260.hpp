#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/apply.hpp"
#include "core/bytes.hpp"
#include "core/transfer.hpp"

#include "hal/bundle.hpp"
#include "hal/describe.hpp"
#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    //
    // Word framing, as the three settings a UART is actually configured with.
    // Enums rather than the bare ints and chars the legacy test language spelled
    // them as ("BaudRate=9600 StopBits=1 WordLength=8 Parity=NONE" in one
    // string), because every one of those was a runtime typo waiting to happen
    // in a language that could only discover it when the port refused to open.
    //
    enum class Parity
    {
        None,
        Even,
        Odd
    };

    //
    // 1.5 stop bits is a real RS232 framing and cannot be an int, which is the
    // reason this is an enum rather than a count -- StopBits::OnePointFive has
    // nowhere to go in an `int stopBits` field, and a driver that silently
    // rounded it would misframe every word.
    //
    enum class StopBits
    {
        One,
        OnePointFive,
        Two
    };

    [[nodiscard]]
    constexpr auto to_string( const Parity parity) -> std::string_view
    {
        switch( parity)
        {
            case Parity::None: return "none";
            case Parity::Even: return "even";
            case Parity::Odd:  return "odd";
        }

        return "none";
    }

    [[nodiscard]]
    constexpr auto to_string( const StopBits bits) -> std::string_view
    {
        switch( bits)
        {
            case StopBits::One:          return "1";
            case StopBits::OnePointFive: return "1.5";
            case StopBits::Two:          return "2";
        }

        return "1";
    }

    class Racal1260;

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
    // hal::N6701A, whose output is cabled to one pin and so has nothing to
    // choose -- a matrix-routed port can reach any interface the fabric wires
    // up, and which one it reached is a fact about a moment in the script
    // rather than about the instrument.
    //
    struct Racal1260Config
    {
        Racal1260 &                                Instrument;
        std::optional<unsigned>                    BaudRate;
        std::optional<unsigned>                    WordLength;
        std::optional<Parity>                      ParityMode;
        std::optional<StopBits>                    Stop;

        //
        // What a Read stops at, and how long it waits. Both belong to the
        // *read*, not to the port's framing, but they ride on the same config
        // because a builder chain is what a script has in its hand at the call
        // site -- Ser1.rs232().terminator( "\r") reads better than a second
        // argument to Read whose meaning has to be remembered.
        //
        core::Bytes                                Terminator;
        std::optional<std::chrono::milliseconds>   Timeout;
    };

    //
    // The fluent chain a script builds before handing it to a verb -- the same
    // "return *this by value, updated" shape as hal::N6701ABuilder and
    // core::Port's setup builders, so "how do I set X" reads the same whether X
    // is sourced, sensed or framed.
    //
    class Racal1260Builder
    {
        public:
            using Config = Racal1260Config;

            explicit Racal1260Builder( Racal1260 & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt, std::nullopt, core::Bytes{}, std::nullopt }
            {}

            [[nodiscard]]
            auto baudRate( const unsigned rate) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.BaudRate = rate;
                return copy;
            }

            [[nodiscard]]
            auto wordLength( const unsigned bits) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.WordLength = bits;
                return copy;
            }

            [[nodiscard]]
            auto parity( const Parity mode) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.ParityMode = mode;
                return copy;
            }

            [[nodiscard]]
            auto stopBits( const StopBits bits) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.Stop = bits;
                return copy;
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
            auto terminator( core::Bytes value) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.Terminator = std::move( value);
                return copy;
            }

            [[nodiscard]]
            auto timeout( const std::chrono::milliseconds limit) const -> Racal1260Builder
            {
                auto copy = *this;
                copy.mConfig.Timeout = limit;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    //
    // One RS232 port on a Racal 1260-series switching/instrumentation chassis,
    // routed to the DUT through the same matrix everything else on this rig
    // goes through.
    //
    // PLACEHOLDER NAME. This class is modelled on how a matrix-routed serial
    // resource behaves, not on a datasheet: the bench this repo describes does
    // not exist yet, and the legacy test script this was reconstructed from
    // named its serial resource only as "Rs.Normal Type=RS232", which says what
    // the ATE called it and nothing about which box provided it. Rename the
    // directory, the class and this file when the real model is known -- the
    // whole point of instruments/ being one directory per driver is that doing
    // so touches nothing outside it except rig/instrument.inc.
    //
    // Deliberately named after a model at all, placeholder or not, rather than
    // hal::Rs232Port. That was the retired hal::Dmm/hal::Oscilloscope mistake
    // (see hal::L4411A and hal::DSO8064 on how it went): a serial driver's
    // command set, its framing limits and its idea of a timeout are as
    // model-specific as any SCPI dialect, and a generic name promises an
    // interchangeability no real driver has.
    //
    // Routed, not cabled -- the opposite of hal::N6701A, and the reason this
    // driver has a connectDriver taking a destination at all. A console is a
    // signal-level interface: the matrix carries it perfectly well, there is no
    // load current to keep off the relays, and one port being switchable
    // between several DUT interfaces is exactly what a matrix is for. So
    // Connect takes the interface (see connectDriver below), and Setup/Write/
    // Read take none -- the route is already held open by then.
    //
    class Racal1260 : public InstrumentTag
    {
        public:
            explicit Racal1260( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // The one builder. Named for the electrical standard rather than
            // for the port number, because this class is one port -- a chassis
            // with several is several instances, exactly the way DcP1..DcP4 are
            // four hal::N6701A instances (see rig/instrument.inc).
            //
            [[nodiscard]]
            auto rs232() -> Racal1260Builder
            {
                return Racal1260Builder{ *this };
            }

            //
            // Drop the port to a known idle state -- see hal::safeRig() in
            // hal/safing.hpp for who calls this and why it takes no arguments
            // and reads no state.
            //
            // A serial port is not passive the way a DMM is, which is why this
            // has a body where hal::L4411A's is empty: its transmit line is an
            // output, driven to a defined level, and on a real port it stays
            // driven after the last byte has gone. Safing therefore discards
            // anything still queued rather than letting a half-written command
            // finish arriving at a DUT nobody is supervising any more -- the
            // same reasoning as hal::N6701A zeroing its setpoint rather than
            // merely disabling its output.
            //
            // The framing is deliberately left alone. With nothing queued and
            // nothing being read there is nothing for a baud rate to affect,
            // and a port that comes back at the framing a test left it at is
            // easier to diagnose than one that silently reverted.
            //
            auto safe() -> void
            {
                mPendingWrite = core::Bytes{};
                mConnected    = false;
            }

            // --- Test/simulation hooks -- real hardware has no such setters ---

            //
            // What the simulated DUT will answer with. A queue rather than one
            // value, because a console dialogue is a sequence and a driver test
            // that could only ever model one exchange would not be modelling a
            // dialogue at all.
            //
            auto queueReply( core::Bytes reply) -> void
            {
                mReplies.push_back( std::move( reply));
            }

            [[nodiscard]]
            auto lastWrite() const -> const core::Bytes &
            {
                return mLastWrite;
            }

            [[nodiscard]]
            auto isConnected() const -> bool
            {
                return mConnected;
            }

            [[nodiscard]]
            auto baudRate() const -> std::optional<unsigned>
            {
                return mBaudRate;
            }

            [[nodiscard]]
            auto parity() const -> std::optional<Parity>
            {
                return mParity;
            }

            // --- What the ADL customization points below actually call ---

            auto configurePort( const Racal1260Config & config) -> void
            {
                //
                // value_or is wrong here and the loop of ifs is the point: an
                // unset field means "leave what is already configured" (see
                // Racal1260Config), so a Setup that named only the baud rate
                // must not reset the parity to some default the builder never
                // chose.
                //
                if( config.BaudRate)   { mBaudRate   = config.BaudRate;   }
                if( config.WordLength) { mWordLength = config.WordLength; }
                if( config.ParityMode) { mParity     = config.ParityMode; }
                if( config.Stop)       { mStopBits   = config.Stop;       }
            }

            auto transmit( const core::Bytes & payload) -> void
            {
                mLastWrite    = payload;
                mPendingWrite = payload;
            }

            //
            // What a real port would do at the end of its timeout: hand back
            // whatever arrived, which may be nothing. An empty reply is a
            // legitimate observation and not an error -- a DUT that said
            // nothing is a finding, and a criterion is the right place to fail
            // it, not the driver.
            //
            // Simulated, so the config is accepted and not consulted: a queued
            // reply is handed back whole, whatever terminator or timeout was
            // configured. On real hardware those two decide where a reply ends
            // and how long to wait for it, and they are carried on the config
            // (rather than invented here) precisely so that swapping this body
            // for real I/O needs no change at any call site. Note what does not
            // change either way: the terminator is not stripped from what comes
            // back -- see the builder's own comment.
            //
            [[nodiscard]]
            auto receive( const Racal1260Config &) -> core::Bytes
            {
                if( mReplies.empty())
                {
                    return core::Bytes{};
                }

                auto reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            auto setConnected( const bool connected) -> void
            {
                mConnected = connected;
            }

        private:
            InstrumentId                mId;
            std::optional<unsigned>     mBaudRate;
            std::optional<unsigned>     mWordLength;
            std::optional<Parity>       mParity;
            std::optional<StopBits>     mStopBits;
            core::Bytes                 mLastWrite;
            core::Bytes                 mPendingWrite;
            std::vector<core::Bytes>    mReplies;
            bool                        mConnected{ false };
    };

    //
    // ADL target for core::SetupEngine -- see core/apply.hpp's own comment on
    // the setupDriver customization point, and on why configuring is a verb of
    // its own rather than a flavour of Apply.
    //
    // Note what this driver deliberately does NOT define: applyDriver and
    // removeDriver. A serial port has no output to energise -- there is nothing
    // an Apply( Ser1.rs232()) could mean -- so Apply on this instrument is "no
    // matching function" at compile time, exactly the way Connect( DcP1.dc())
    // is on a supply with no isolation relay (see hal::SwitchableIsolation).
    // The absence is the design.
    //
    inline auto setupDriver( const Racal1260Config & config) -> void
    {
        config.Instrument.configurePort( config);
    }

    //
    // ADL targets for core::WriteEngine/core::ReadEngine -- the byte-oriented
    // verbs, see core/transfer.hpp.
    //
    inline auto writeDriver( const Racal1260Config & config, const core::Bytes & payload) -> void
    {
        config.Instrument.transmit( payload);
    }

    [[nodiscard]]
    inline auto readDriver( const Racal1260Config & config) -> core::Bytes
    {
        return config.Instrument.receive( config);
    }

    //
    // ADL target for the run journal -- see core/apply.hpp's own comment on the
    // describeConfig customization point.
    //
    // Framing is reported field by field rather than as the "9600 8N1" shorthand
    // an engineer would say out loud, because the shorthand cannot express an
    // unset field: a Setup that named only the baud rate is a different
    // instruction from one that named all four, and "9600 8N1" would invent the
    // three it was never told. Same reasoning as hal::describeSetting's, which
    // this uses for exactly that.
    //
    inline auto describeConfig( const Racal1260Config & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                config.BaudRate   ? "baud="       + std::to_string( *config.BaudRate)              : std::string{},
                config.WordLength ? "wordLength=" + std::to_string( *config.WordLength)            : std::string{},
                config.ParityMode ? "parity="     + std::string( to_string( *config.ParityMode))   : std::string{},
                config.Stop       ? "stopBits="   + std::string( to_string( *config.Stop))         : std::string{},
                config.Terminator.empty() ? std::string{} : "terminator=" + core::describeValue( config.Terminator),
                config.Timeout    ? "timeout="    + std::to_string( config.Timeout->count()) + "ms" : std::string{}
            })
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine's bundle overloads.
    //
    // This is the routed connectDriver the other drivers in this repo do not
    // have: hal::N6701A and hal::Ac6834B close their own fixed channels and
    // never consult the connector side at all, because their outputs are cabled
    // to a known pin. Here both halves matter -- the port's own matrix channels
    // and the channels the DUT interface's lines are wired to -- and the route
    // is their concatenation, exactly the composition core::MeasureEngine
    // performs for a routed reading.
    //
    // Every line of the bundle, closed as one path. An RS232 console is not
    // usable a wire at a time (see core::ConnectEngine's bundle overload on why
    // the interface rather than the pin is the unit), and closing the set
    // together is also what hal::InstrumentWiring::findAll already does for
    // hal::Ac6834B's three phases and neutral -- the same idea reaching the
    // connector side.
    //
    // The pairing of which instrument channel meets which DUT line is left to
    // the wiring tables and is not asserted here, deliberately. A crosspoint
    // matrix has no notion of transmit and receive; it closes the crosspoints it
    // is given, and which of them forms the outbound path is a fact about how
    // the bench was cabled -- a rig/wiring.inc fact, checked by
    // dut/tests/test_wiring_coverage.cpp, not something a driver can know.
    //
    //
    // The composed route: this port's own fixed matrix channels, plus the
    // channel every line of the destination interface is wired to.
    //
    // findAll rather than find on the instrument side, for the same reason
    // hal::N6701A's connectDriver uses it: a serial port is at least two wires
    // at the instrument end too, so it has more than one WIRE_INSTRUMENT row
    // (see rig/wiring.inc), and they close and open as a unit.
    //
    // Shared by connect and disconnect rather than written twice, because the
    // two must compose the identical path -- a disconnect that opened a
    // different set of channels from the one the connect closed would leave
    // crosspoints latched with nothing in the code saying so.
    //
    template<typename BundleT>
    [[nodiscard]]
    auto routeTo( const InstrumentWiring &  instrumentWiring,
                  const ConnectorWiring &   connectorWiring,
                  const Racal1260Config &   config) -> Path
    {
        auto path = instrumentWiring.findAll( config.Instrument.id());

        for( const auto location : bundleLocations<BundleT>)
        {
            const auto linePath = connectorWiring.find( location);

            path.insert( path.end(), linePath.begin(), linePath.end());
        }

        return path;
    }

    template<typename BundleT>
    auto connectDriver( SwitchFabric &            fabric,
                        const InstrumentWiring &  instrumentWiring,
                        const ConnectorWiring &   connectorWiring,
                        const Racal1260Config &   config,
                        const core::AdapterBundle<BundleT> &) -> void
    {
        fabric.connect( routeTo<BundleT>( instrumentWiring, connectorWiring, config));

        config.Instrument.setConnected( true);
    }

    template<typename BundleT>
    auto disconnectDriver( SwitchFabric &            fabric,
                           const InstrumentWiring &  instrumentWiring,
                           const ConnectorWiring &   connectorWiring,
                           const Racal1260Config &   config,
                           const core::AdapterBundle<BundleT> &) -> void
    {
        fabric.disconnect( routeTo<BundleT>( instrumentWiring, connectorWiring, config));

        config.Instrument.setConnected( false);
    }
} // namespace hal
