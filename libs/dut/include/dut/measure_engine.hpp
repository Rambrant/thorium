#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/quantity_kind.hpp"
#include "core/recording.hpp"
#include "core/session.hpp"
#include "dut/adapter.hpp"
#include "hal/instrument.hpp"
#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"

namespace dut
{
    //
    // The mechanism behind a single callable Measure object:
    //
    //   Measure( Dmm1.voltage(), "5VOutput");
    //
    // A class with operator(), not a free function, because it also owns
    // its own session state (live hardware by default; injected or replayed
    // values on request; optionally recorded) -- see inject()/load()/
    // useLive()/startRecording()/stopRecording()/dump() below. That state
    // has to live somewhere; putting it here means nothing about it appears
    // at the call site or gets threaded through every script.
    //
    // There is exactly one instance of this per DUT/rig, holding that DUT's
    // Adapter and that rig's fabric/RouteTable -- see suite/measure.cpp for
    // the concrete instance every script actually calls.
    //
    class MeasureEngine
    {
        public:
            MeasureEngine( hal::SwitchFabric & fabric, const hal::RouteTable & routes, const Adapter & adapter) :
                mFabric( fabric),
                mRoutes( routes),
                mAdapter( adapter)
            {}

            //
            // Resolves pointName on this DUT's Adapter, checks its declared
            // quantity against QuantityT, then fetches through whichever
            // session is currently active (see below) -- live routing by
            // default. See the TODO in resolve()'s definition for why both
            // that check and the RouteTable lookup inside route() are runtime
            // checks today rather than compile-time ones.
            //
            template<core::quantities::QuantityType QuantityT, typename InstrumentT>
            [[nodiscard]]
            auto operator()( hal::Port<QuantityT, InstrumentT> port, std::string_view pointName) -> QuantityT
            {
                constexpr auto expectedKind = core::quantityKindOf<QuantityT>();

                const auto location     = resolve( pointName, expectedKind);
                const auto instrumentId = port.instrumentId();

                auto liveRead = [&]() -> core::QuantityVariant
                {
                    route( location, instrumentId, expectedKind);
                    return core::QuantityVariant{ port.rawMeasure() };
                };

                auto value = activeSession().fetch( pointName, hal::to_string( instrumentId), expectedKind, liveRead);

                return core::asQuantity<QuantityT>( value);
            }

            //
            // Feeds a canned value for a point, bypassing hal entirely --
            // for script unit tests. Switches this engine to use injected/
            // loaded values if it wasn't already (no separate "now use
            // this" call needed); see useLive() to go back.
            //
            auto inject( std::string_view pointName, core::QuantityVariant value) -> void;

            //
            // Loads a recording (see dump() below) and replays it in place
            // of injected values -- discards whatever was previously
            // injected or loaded.
            //
            auto load( const std::string & path) -> void;

            // Returns to real routing + real instrument reads.
            auto useLive() -> void;

            // Starts/stops logging every fetch (live, injected, or replayed).
            auto startRecording() -> void;
            auto stopRecording() -> void;

            // Writes out whatever has been recorded so far.
            auto dump( std::ostream & out) const -> void;

        private:
            [[nodiscard]] auto resolve( std::string_view pointName, core::QuantityKind expectedKind) const -> hal::VpcLocation;
            auto route( hal::VpcLocation location, hal::InstrumentId instrument, core::QuantityKind kind) -> void;
            [[nodiscard]] auto activeSession() -> core::ISession &;

            hal::SwitchFabric &      mFabric;
            const hal::RouteTable &  mRoutes;
            const Adapter &          mAdapter;

            core::LiveSession        mLive;
            core::ScriptedSession    mScripted;
            core::SwitchableSession  mSwitchable{ mLive };
            core::RecordingSession   mRecorder{ mSwitchable };
            bool                     mRecording{ false };
    };
} // namespace dut
