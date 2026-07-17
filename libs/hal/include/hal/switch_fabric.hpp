#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hal
{
    //
    // Which kind of switching hardware a SwitchElementId's channel lives on.
    // Purely descriptive -- both kinds are commanded identically (open/close
    // a channel) -- kept so a route's path reads clearly and so error
    // messages can say "Matrix2 channel 14" rather than an opaque pair.
    //
    enum class SwitchDeviceKind
    {
        Matrix, // RACAL 1260 VXI matrix card
        Mux     // Agilent E1472A multiplexer
    };

    //
    // One switchable channel: a specific channel on a specific matrix card or
    // mux. This is deliberately just plumbing -- unlike hal::Instrument, it
    // has no quantity type and nothing to read -- so the type system can never
    // let a script try to Measure a mux channel.
    //
    struct SwitchElementId
    {
        SwitchDeviceKind kind;
        std::string_view device;  // e.g. "Matrix2", "Mux1"
        std::uint16_t    channel;

        friend constexpr auto operator==( SwitchElementId, SwitchElementId) -> bool = default;
        friend constexpr auto operator<=>( SwitchElementId, SwitchElementId) = default;
    };

    [[nodiscard]]
    auto to_string( SwitchElementId id) -> std::string;

    //
    // The switching fabric sitting between the instruments and the VPC array:
    // every RACAL 1260 matrix card and every Agilent E1472A mux in the
    // signal chain, addressed uniformly by SwitchElementId. On real hardware
    // close()/open() would be GPIB/VXI writes to the relevant card; here they
    // just track state so routing logic can be exercised and asserted on.
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
            // core/apply.hpp, core/measure.hpp): a source instrument's own
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
