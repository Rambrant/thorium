#pragma once

#include <cstdint>
#include <set>
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
    class SwitchFabric
    {
        public:
            auto close( SwitchElementId id) -> void
            {
                mClosed.insert( id);
            }

            auto open( SwitchElementId id) -> void
            {
                mClosed.erase( id);
            }

            auto openAll() -> void
            {
                mClosed.clear();
            }

            [[nodiscard]]
            auto isClosed( SwitchElementId id) const -> bool
            {
                return mClosed.count( id) > 0;
            }

            //
            // Opens every element, then closes exactly the given path, in
            // order. This is the one operation dut::Measure actually calls --
            // "make this route, and only this route, live" -- rather than
            // open()/close() one at a time.
            //
            auto route( const std::vector<SwitchElementId> & path) -> void
            {
                openAll();

                for( const auto id : path)
                {
                    close( id);
                }
            }

        private:
            std::set<SwitchElementId> mClosed;
    };
} // namespace hal
