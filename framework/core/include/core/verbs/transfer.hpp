#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "core/session/bench.hpp"
#include "core/quantities/bytes.hpp"
#include "core/driver/describe.hpp"
#include "core/journal/journal.hpp"
#include "core/session/session.hpp"

namespace core
{
    //
    // Write/Read: the byte-oriented verbs -- a stimulus sent to the DUT, and an
    // answer taken back off it.
    //
    // Named for what they do to a port rather than for the bus underneath, so
    // that the next instrument with a payload instead of a reading (a CAN
    // interface, a bus analyser, a bootloader link) reuses them rather than
    // arriving with a third and fourth verb meaning the same two things. That
    // is also why this file is core/verbs/transfer.hpp and not core/serial.hpp:
    // nothing here knows what RS232 is. hal::Racal1260 is simply the first
    // driver to answer to writeDriver/readDriver.
    //
    // Two verbs rather than one Transact, even though the shape a console
    // protocol is written in is nearly always command-then-acknowledgement.
    // Three reasons, in the order they bite:
    //
    //   - A write with no reply is real (a reset command, a broadcast), and a
    //     read with no write is real (an unsolicited status frame, a boot
    //     banner). One combined verb makes both awkward and makes at least one
    //     of them a lie in the log.
    //   - A reply that never came has to be *visible*. With two verbs the
    //     journal shows a Write and then nothing, which is exactly the
    //     diagnosis; a Transact that threw would show neither, and a Transact
    //     that returned empty would show one event claiming a round trip
    //     happened.
    //   - The two have genuinely different session behaviour, below: a Write
    //     goes straight to the instrument, and a Read must go through the
    //     session seam so it can be injected and replayed.
    //
    // Neither touches the switching fabric. The route is held open by Connect
    // for as long as the dialogue lasts -- see core::ConnectEngine's bundle
    // overload in core/verbs/route.hpp -- which is the one real difference from how
    // Measure works. A Measure connects, reads and disconnects within the call,
    // because a reading is instantaneous and independent. A console dialogue is
    // neither: dropping the path between a command and its acknowledgement
    // would break the exchange, and re-closing relays around every byte would
    // wear them out for nothing.
    //
    //     Connect(    Ser1.rs232(), at( dut::Console));
    //     Setup(      Ser1.rs232().baudRate( 9600).wordLength( 8).parity( Parity::None));
    //     Write(      Ser1.rs232(), "RD 30\r");
    //     const auto reply = Read( Ser1.rs232());
    //     Disconnect( Ser1.rs232(), at( dut::Console));
    //
    class WriteEngine
    {
        public:
            //
            // The payload is a core::Bytes, which a string literal converts to
            // implicitly -- so a text protocol reads the way its specification
            // is written, and a binary one is spelled Bytes::fromHex(...) at the
            // call site where the difference is worth seeing.
            //
            template<typename BuilderT>
            auto operator()( const BuilderT & builder, const Bytes & payload) const -> void
            {
                //
                // Conditional on a bench being attached, like every other
                // stimulus verb -- see core/session/bench.hpp. A run replaying a
                // console dialogue takes the replies from the file, and must
                // not put the commands out on a real port to get them.
                //
                const auto sent = bench().isAttached();

                if( sent)
                {
                    writeDriver( builder.config(), payload);
                }

                //
                // Logged after the driver call, for the reason every engine in
                // core/verbs/source.hpp gives: if the instrument refused the write,
                // nothing went out and nothing should claim it did.
                //
                const auto described = describeConfig( builder.config());

                journal().post( JournalRecord{
                    .Method     = Verb::Write,
                    .Subject    = described.Instrument,

                    //
                    // Spelled out here rather than through
                    // detail::postSourceEvent, which is what marks the other
                    // stimulus verbs: this event has a Detail of its own
                    // ("sent") that the marker replaces rather than fills in.
                    //
                    .Detail     = sent ? "sent" : kDetachedDetail,
                    .Instrument = described.Instrument,
                    .Value      = describeValue( payload)
                });
            }
    };

    //
    // Read goes through the session seam, exactly as Measure does, and holds a
    // reference to the same core::SessionBank so that one run produces one
    // ordered recording -- see that class's own comment for why sharing is a
    // correctness requirement and not a convenience.
    //
    class ReadEngine
    {
        public:
            explicit ReadEngine( SessionBank & sessions) : mSessions( sessions) {}

            template<typename BuilderT>
            [[nodiscard]]
            auto operator()( const BuilderT & builder) -> Bytes
            {
                const auto described = describeConfig( builder.config());

                //
                // Sessions key by name, and a port read has no DUT point to key
                // by -- Connect holds the route, and this call does not name it.
                // So it keys the way core::MeasureEngine's point-free readback
                // overload already does: "<instrument>.<what>", here
                // "Ser1.Data".
                //
                // Which is what a script unit test injects against:
                //
                //     Read.inject( "Ser1.Data", { "ACK\r", "0xF5\r" });
                //
                // Keyed on the instrument rather than on the interface it is
                // connected to, because that is the fact this call actually
                // has. A rig where one serial instrument is moved between two
                // DUT interfaces during a run would want the finer key, and the
                // way to get it is a qualifier on the builder -- the mechanism
                // core::Port::qualifiedBy already provides for a three-phase
                // source's per-phase readbacks. No driver needs it yet, so none
                // carries one.
                //
                const auto key = described.Instrument + ".Data";

                auto liveRead = [&]() -> Bytes
                {
                    return readDriver( builder.config());
                };

                auto payload = mSessions.active().fetchData( key, described.Instrument, liveRead);

                journal().post( JournalRecord{
                    .Method     = Verb::Read,
                    .Subject    = key,
                    .Detail     = "received",
                    .Instrument = described.Instrument,
                    .Value      = describeValue( payload)
                });

                return payload;
            }

            //
            // Feeds a canned reply, bypassing hal entirely -- the byte-side
            // twin of Measure.inject, and what makes a script that talks to the
            // DUT testable with no bench attached.
            //
            auto inject( const std::string_view name, Bytes value) -> void
            {
                mSessions.injectData( name, std::move( value));
            }

            //
            // A *sequence* of replies, one per read, in order -- a whole
            // dialogue, authored in the test:
            //
            //     Read.inject( "Ser1.Data", { "ACK\r", "0xF5\r" });
            //
            // Running past the end throws rather than repeating the last reply
            // (see core::ScriptedSession::fetchData): a script reading more
            // times than the test authored answers for has diverged from what
            // was expected, which is the thing worth failing on.
            //
            auto inject( const std::string_view name, std::initializer_list<Bytes> values) -> void
            {
                mSessions.injectData( name, dataSourceOf( std::vector<Bytes>( values)));
            }

            template<std::ranges::input_range R>
                requires std::constructible_from<Bytes, std::ranges::range_reference_t<R>>
            auto inject( const std::string_view name, R values) -> void
            {
                mSessions.injectData( name, dataSourceOf( std::move( values)));
            }

            auto inject( const std::string_view name, DataSource source) -> void
            {
                mSessions.injectData( name, std::move( source));
            }

            //
            // Deliberately absent: useLive, load, startRecording, stopRecording
            // and dump. All five act on the shared bank, so offering them here
            // as well would be two spellings of one operation -- and a reader
            // seeing Read.useLive() next to Measure.useLive() would reasonably
            // conclude the two seams can be armed independently, which is
            // exactly the half-scripted run core::SessionBank exists to prevent.
            // The bank is reached through Measure for all of them.
            //
        private:
            SessionBank & mSessions;
    };
} // namespace core
