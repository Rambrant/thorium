#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/describe.hpp"
#include "core/journal.hpp"
#include "core/session.hpp"
#include "core/waveform.hpp"

namespace core
{
    //
    // Fetch: takes the captured record off an instrument -- the whole trace,
    // rather than one number measured from it.
    //
    // The fourth verb that observes something, after Measure, Read and Await,
    // and it goes through the same core::SessionBank for the reason that class
    // states at length: one run, one ordered recording, or a replay arms some
    // seams and not others and produces a green result that means nothing.
    //
    // Named for the operation and not for an oscilloscope, like every other
    // verb here. A transient recorder, a digitizer and a logger with a memory
    // behind it all hand back the same shape -- readings in a unit, at a known
    // rate -- and hal::DSO8064A is simply the first driver to answer to
    // fetchDriver.
    //
    // Where it sits in a script is fixed by the pair it follows:
    //
    //     Arm(   Osc1.single().timeout( 2_s));
    //     Remove( AcP1.ac());                              // the event
    //     const auto captured = Await( Osc1.single());
    //
    //     Verify( FS_Transient_1::FS_Transient_Captured, captured);
    //
    //     const auto trace = Fetch( Osc1.channel<3>().waveform());
    //
    // Fetching before checking the Await is the same mistake as measuring
    // before checking it, and has the same consequence: what comes back is the
    // *previous* acquisition, which will look like a perfectly good trace.
    // Nothing enforces the order, for the reason nothing enforces
    // Arm-before-event -- it is a property of the instrument, not of this code,
    // and there is nothing here to ask about it. Connect-before-Apply is no
    // longer the third member of that list: a supply can be asked whether its
    // output is on, so breaking that one is recorded even though it is still
    // permitted (see core/interlock.hpp).
    //
    // [[nodiscard]], like Await and for a related reason: a Fetch whose result
    // is dropped has moved a megabyte off an instrument to no purpose, and is
    // far more likely to be a half-finished edit than an intention.
    //
    class FetchEngine
    {
        public:
            explicit FetchEngine( SessionBank & sessions) : mSessions( sessions) {}

            template<typename BuilderT>
            [[nodiscard]]
            auto operator()( const BuilderT & builder) -> Waveform
            {
                const auto described = describeConfig( builder.config());

                const auto key = described.Instrument + "." + qualifierOf( builder.config());

                auto liveFetch = [&]() -> Waveform
                {
                    return fetchDriver( builder.config());
                };

                auto trace = mSessions.active().fetchTrace( key, described.Instrument, liveFetch);

                //
                // A summary, never the samples -- see core::describeValue for a
                // Waveform on why there is no length at which spelling out four
                // thousand numbers is what a reader wanted.
                //
                journal().post( JournalRecord{
                    .Method     = Verb::Fetch,
                    .Subject    = key,
                    .Detail     = "captured",
                    .Instrument = described.Instrument,
                    .Value      = describeValue( trace)
                });

                return trace;
            }

            //
            // Feeds a canned trace, bypassing hal entirely -- the trace-side
            // twin of Measure.inject, Read.inject and Await.inject.
            //
            auto inject( const std::string_view name, Waveform value) -> void
            {
                mSessions.injectTrace( name, std::move( value));
            }

            auto inject( const std::string_view name, std::initializer_list<Waveform> values) -> void
            {
                mSessions.injectTrace( name, traceSourceOf( std::vector<Waveform>( values)));
            }

            auto inject( const std::string_view name, TraceSource source) -> void
            {
                mSessions.injectTrace( name, std::move( source));
            }

            //
            // Deliberately absent: useLive, load, startRecording, stopRecording
            // and dump -- see core::ReadEngine's own comment on why the bank is
            // reached through Measure for all five and offered only once.
            //
        private:
            //
            // The "what" half of the session key, so a trace files under
            // "<instrument>.<what>" the way core::ReadEngine's "Ser1.Data" and
            // Measure's point-free readbacks already do.
            //
            // "Trace" unless the driver says otherwise, and a driver says
            // otherwise by defining traceQualifier() beside its config type.
            // hal::DSO8064A does, and has to: a four-channel scope holds four
            // records at once, and one slot for all of them would let a test
            // inject a channel-1 trace and have a channel-3 Fetch take it --
            // which is exactly the collision core::Port::qualifiedBy exists to
            // prevent among that same scope's fifteen measurements.
            //
            // Detected rather than required, because a single-record instrument
            // has nothing to qualify and should not have to write a function
            // saying so.
            //
            template<typename ConfigT>
            [[nodiscard]]
            static auto qualifierOf( const ConfigT & config) -> std::string
            {
                if constexpr( requires { traceQualifier( config); })
                {
                    return traceQualifier( config);
                }
                else
                {
                    return "Trace";
                }
            }

            SessionBank & mSessions;
    };
} // namespace core
