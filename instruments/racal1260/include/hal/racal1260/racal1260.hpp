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
#include "hal/racal1260/builder.hpp"

//
// the instrument class
//
// Part of hal/racal1260.hpp -- include that, not this.
//

namespace hal::racal1260
{
    //
    // One RS232 port on a Racal 1260-series switching/instrumentation chassis,
    // switched onto the DUT through the same fabric everything else on this
    // rig goes through -- three changeover relays on this bench, though which
    // cards a rig uses is nothing this driver knows or should.
    //
    // PLACEHOLDER NAME. This class is modelled on how a switched serial
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
    // (see hal::keysight_edu34450a::Multimeter and hal::keysight_dsox1202g::Oscilloscope on how it went): a serial driver's
    // command set, its framing limits and its idea of a timeout are as
    // model-specific as any SCPI dialect, and a generic name promises an
    // interchangeability no real driver has.
    //
    // Routed, not cabled -- the opposite of hal::keysight_edu36311a::DcSource, and the reason this
    // driver has a connectDriver taking a destination at all. A console is a
    // signal-level interface: the fabric carries it perfectly well, there is no
    // load current to keep off the relays, and one port being switchable
    // between several DUT interfaces is exactly what switching is for. So
    // Connect takes the interface (see connectDriver below), and Setup/Write/
    // Read take none -- the route is already held open by then.
    //
    class Racal1260 : public InstrumentTag
    {
        public:
            //
            // Serial or GPIB, and which one is a bench fact this driver
            // genuinely does not know -- see this directory's README, which
            // says the same about the model name. Two different arrangements
            // both produce "an RS232 port switched onto the DUT through the
            // fabric", and they are reached differently:
            //
            //   - a port on the PC, its three conductors cabled into the
            //     switching: Serial, and the thing addressed and the thing
            //     switched are then one and the same port
            //   - a serial-interface module in the switching chassis,
            //     commanded over that chassis's own bus: Gpib, the way
            //     DcP1..DcP4 are commanded through their shared mainframe
            //
            // Both are listed because this constraint is meant to say what is
            // known about the hardware, and here that is "either" -- the other
            // four drivers each name exactly one panel because each one's is
            // known. Narrowing this to whichever the bench turns out to have is
            // a one-line change, and worth making then.
            //
            // What does not differ between the two: the framing this driver
            // configures (BaudRate/Parity/StopBits above) is what the port
            // speaks *out* at the DUT, never how the PC reaches the port. On
            // the first arrangement those are physically the same port, which
            // is exactly when the distinction is easiest to lose -- the baud
            // rate a test sets is a DUT-facing decision, and hal::Serial's
            // device path is not.
            //
            // This model's back panel, written once and read twice: the
            // constructor below is constrained by it, and
            // hal::bindAddresses() checks an address supplied at startup
            // against the same list (see hal::BackPanel). A second
            // spelling of these connectors could disagree with this one,
            // and only the bench would ever find out.
            //
            using Buses = BackPanel<Serial, Gpib>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            Racal1260( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            // Where the PC reaches this port -- see hal/driver/address.hpp.
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            //
            // Point this driver at a different instrument -- see
            // hal::keysight_edu34450a::Multimeter::useAddress() for the
            // argument, and note the one line of it that is missing here.
            //
            // There is no session to drop. This driver opens none: it answers
            // from its own simulation hooks, which is why it has no
            // identity() and why hal::ContactableInstrument does not match it
            // (see hal/verbs/preflight.hpp on why that is branched on rather
            // than asserted). So rebinding it is a store and nothing else --
            // and it still has to exist, because the resolver refuses to
            // silently skip a row it cannot rebind.
            //
            auto useAddress( const Address & address) -> void
            {
                mAddress = address;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // The one builder. Named for the electrical standard rather than
            // for the port number, because this class is one port -- a chassis
            // with several is several instances, exactly the way DcP1..DcP4 are
            // four hal::keysight_edu36311a::DcSource instances (see rig/instrument.inc).
            //
            [[nodiscard]]
            auto rs232() -> Rs232Builder
            {
                return Rs232Builder{ *this };
            }

            //
            // Drop the port to a known idle state -- see hal::safeRig() in
            // hal/verbs/safing.hpp for who calls this and why it takes no arguments
            // and reads no state.
            //
            // A serial port is not passive the way a DMM is, which is why this
            // has a body where hal::keysight_edu34450a::Multimeter's is empty: its transmit line is an
            // output, driven to a defined level, and on a real port it stays
            // driven after the last byte has gone. Safing therefore discards
            // anything still queued rather than letting a half-written command
            // finish arriving at a DUT nobody is supervising any more -- the
            // same reasoning as hal::keysight_edu36311a::DcSource zeroing its setpoint rather than
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

            auto configurePort( const Rs232Config & config) -> void
            {
                //
                // value_or is wrong here and the loop of ifs is the point: an
                // unset field means "leave what is already configured" (see
                // Rs232Config), so a Setup that named only the baud rate
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
            auto receive( const Rs232Config &) -> core::Bytes
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
            Address                     mAddress;
            std::optional<unsigned>     mBaudRate;
            std::optional<unsigned>     mWordLength;
            std::optional<Parity>       mParity;
            std::optional<StopBits>     mStopBits;
            core::Bytes                 mLastWrite;
            core::Bytes                 mPendingWrite;
            std::vector<core::Bytes>    mReplies;
            bool                        mConnected{ false };
    };
} // namespace hal::racal1260
