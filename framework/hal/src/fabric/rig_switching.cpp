#include "hal/fabric/rig_switching.hpp"

#include <concepts>
#include <cstddef>
#include <meta>
#include <stdexcept>
#include <string>
#include <vector>

#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_device.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/topology/boxes.hpp"

namespace hal
{
    namespace
    {
        consteval auto membersOfInfo( std::meta::info scope) -> std::vector<std::meta::info>
        {
            return std::meta::members_of( scope, std::meta::access_context::current());
        }

        template<std::meta::info Scope>
        constexpr auto members = std::define_static_array( membersOfInfo( Scope));

        class RigSwitchDriver final : public SwitchDriver
        {
            public:
                auto close( const SwitchElementId id) -> void override
                {
                    drive( id, true);
                }

                auto open( const SwitchElementId id) -> void override
                {
                    //
                    // An RF bank has no open state -- see this file's header.
                    //
                    if( kindOf( id.device) == SwitchDeviceKind::RfMux)
                    {
                        return;
                    }

                    drive( id, false);
                }

            private:
                static auto drive( const SwitchElementId id, const bool closing) -> void
                {
                    const auto row  = static_cast<std::size_t>( id.device);
                    const auto card = detail::switchDevices[ row].Card;
                    const auto box  = detail::deviceBoxRows[ row].Box;

                    //
                    // A device with no card address is reached by its own
                    // address alone, and nothing here drives one of those yet.
                    //
                    if( !card)
                    {
                        return;
                    }

                    bool driven = false;

                    template for( constexpr auto member : members<^^::>)
                    {
                        if constexpr( std::meta::is_variable( member))
                        {
                            using InstrumentT = [: std::meta::type_of( member) :];

                            if constexpr( std::derived_from<InstrumentT, InstrumentTag>
                                       && CardSwitchingInstrument<InstrumentT>)
                            {
                                auto & instrument = [: member :];

                                if( !driven && boxOf( instrument.id()) == box)
                                {
                                    if( closing)
                                    {
                                        instrument.closeOnCard( *card, id.channel);
                                    }
                                    else
                                    {
                                        instrument.openOnCard( *card, id.channel);
                                    }

                                    driven = true;
                                }
                            }
                        }
                    }

                    //
                    // Not driven: the box has no switching instrument, and
                    // the fabric's count is the whole of the state -- see this
                    // file's header.
                    //
                }
        };
    } // namespace

    auto rigSwitchDriver() -> SwitchDriver &
    {
        static RigSwitchDriver driver;

        return driver;
    }
} // namespace hal
