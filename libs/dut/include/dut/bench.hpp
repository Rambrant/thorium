#pragma once

#include "core/session.hpp"
#include "dut/adapter.hpp"
#include "hal/route_table.hpp"
#include "hal/switch_fabric.hpp"

namespace dut
{
    //
    // Everything Measure needs besides the instrument port and the point
    // name: the rig's switching fabric and its fixed Route table, this DUT's
    // Adapter, and the session values are fetched through (live hardware by
    // default, or a ScriptedSession/RecordingSession -- see core/session.hpp
    // -- for tests and debugging, swapped at runtime with no recompile).
    //
    // A thin reference bundle, not an owner: the fabric/routes/adapter/session
    // all outlive it and are shared across every script in a run.
    //
    class Bench
    {
        public:
            Bench( hal::SwitchFabric & fabric, const hal::RouteTable & routes, const Adapter & adapter, core::ISession & session) :
                mFabric( fabric),
                mRoutes( routes),
                mAdapter( adapter),
                mSession( session)
            {}

            [[nodiscard]]
            auto fabric() -> hal::SwitchFabric &
            {
                return mFabric;
            }

            [[nodiscard]]
            auto routes() const -> const hal::RouteTable &
            {
                return mRoutes;
            }

            [[nodiscard]]
            auto adapter() const -> const Adapter &
            {
                return mAdapter;
            }

            [[nodiscard]]
            auto session() -> core::ISession &
            {
                return mSession;
            }

        private:
            hal::SwitchFabric &      mFabric;
            const hal::RouteTable &  mRoutes;
            const Adapter &          mAdapter;
            core::ISession &         mSession;
    };
} // namespace dut
