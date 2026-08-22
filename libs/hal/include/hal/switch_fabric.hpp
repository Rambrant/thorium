#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hal/switch_device.hpp"

namespace hal
{
    //
    // One switchable channel: a specific channel on a specific matrix card or
    // mux. This is deliberately just plumbing -- unlike hal::Instrument, it
    // has no quantity type and nothing to read -- so the type system can never
    // let a script try to Measure a mux channel.
    //
    // Two fields, not three. What kind of hardware the channel lives on is a
    // property of the device and is stated once where the device is declared
    // (see hal::kindOf and rig/devices.inc), so no two hops can disagree about
    // one card -- which they could, and silently, when each hop carried its own
    // kind alongside a device name in a bare string. hal/switch_device.hpp's
    // own comment spells out both failure modes that removed.
    //
    struct SwitchElementId
    {
        SwitchDeviceId device;
        std::uint16_t  channel;

        friend constexpr auto operator==( SwitchElementId, SwitchElementId) -> bool = default;
        friend constexpr auto operator<=>( SwitchElementId, SwitchElementId) = default;
    };

    [[nodiscard]]
    auto to_string( SwitchElementId id) -> std::string;

    //
    // A route through the fabric between two fixed points is rarely just
    // one relay -- the real wiring behind hal::InstrumentWiring/
    // hal::ConnectorWiring's entries can be a chain of several (a mux
    // narrowing thousands of DUT points down to a handful of lines, then
    // another mux narrowing further, then finally a matrix crosspoint --
    // see those two classes' own comments). SwitchFabric::connect()/
    // disconnect() already take a plain std::vector<SwitchElementId> and
    // don't care how many elements are in it or which wiring fact
    // contributed which one -- every element in the chain just needs
    // closing (or opening) together. Path names that vector at the type
    // level so a chain and a single hop are the same thing to write and to
    // compose (see hal::InstrumentWiring::find()/findAll() and
    // core::MeasureEngine's operator(), which concatenates an instrument's
    // Path with a connector's Path into the one combined route).
    //
    using Path = std::vector<SwitchElementId>;

    //
    // The switching fabric sitting between the instruments and the VPC array:
    // every card the rig's devices.inc declares (see hal/switch_device.hpp),
    // addressed uniformly by SwitchElementId. On real hardware close()/open()
    // would be GPIB/VXI writes to the relevant card -- hal::addressOf() is
    // where that card's address comes from -- while here they just track state
    // so routing logic can be exercised and asserted on.
    //
    // Each element's state is a use count, not a plain bool: a physical
    // relay is either open or closed, but two independent callers can both
    // legitimately need it closed at once -- e.g. Connect() holding a
    // supply on a DUT point while Measure() briefly listens in on that
    // same point with a second instrument. Whoever asked for it closed
    // last is not necessarily who's done with it first, so open() only
    // actually opens the relay once every close() on it has been matched
    // by an open() -- see connect()/disconnect() below, which is what
    // callers actually use.
    //
    class SwitchFabric
    {
        public:
            auto close( SwitchElementId id) -> void
            {
                ++mUseCount[ id];
            }

            auto open( SwitchElementId id) -> void
            {
                if( const auto found = mUseCount.find( id); found != mUseCount.end())
                {
                    if( --found->second <= 0)
                    {
                        mUseCount.erase( found);
                    }
                }
            }

            auto openAll() -> void
            {
                mUseCount.clear();
            }

            [[nodiscard]]
            auto isClosed( SwitchElementId id) const -> bool
            {
                return mUseCount.count( id) > 0;
            }

            //
            // connect()/disconnect(): close (or open) exactly the given
            // path's elements, one use each -- additive, leaving every
            // other currently-closed element (and every other use of these
            // same elements) untouched. This is what core::ConnectEngine/
            // DisconnectEngine and core::MeasureEngine call (see
            // core/route.hpp, core/measure.hpp): a source instrument's own
            // relay path shouldn't tear down some other already-live route
            // (another instrument's supply, say) just because it's being
            // connected, disconnected, or briefly shared with a
            // measurement -- and thanks to the use-count above, two
            // callers sharing one element (e.g. a supply's connector
            // channel also being read by a DMM) leave it closed until both
            // have released it, not just the first one to disconnect.
            //
            auto connect( const std::vector<SwitchElementId> & path) -> void
            {
                for( const auto id : path)
                {
                    close( id);
                }
            }

            auto disconnect( const std::vector<SwitchElementId> & path) -> void
            {
                for( const auto id : path)
                {
                    open( id);
                }
            }

        private:
            std::map<SwitchElementId, int>  mUseCount;
    };
} // namespace hal
