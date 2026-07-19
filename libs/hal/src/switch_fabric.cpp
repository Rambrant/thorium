#include "hal/switch_fabric.hpp"

namespace hal
{
    namespace
    {
        [[nodiscard]]
        auto deviceKindName( const SwitchDeviceKind kind) -> std::string_view
        {
            //
            // No default case -- adding a fourth SwitchDeviceKind without
            // updating this switch is a -Wswitch warning, promoted to a
            // build failure by -Werror, rather than a silent mislabel the
            // way the two-kind ternary this replaced would have produced
            // for a third kind.
            //
            switch( kind)
            {
                case SwitchDeviceKind::Matrix: return "Matrix";
                case SwitchDeviceKind::Mux:    return "Mux";
                case SwitchDeviceKind::RfMux:  return "RfMux";
            }

            return "Unknown"; // unreachable given the switch above; silences -Wreturn-type
        }
    } // namespace

    auto to_string( const SwitchElementId id) -> std::string
    {
        return std::string( deviceKindName( id.kind)) + " " +
               std::string( id.device) + " channel " + std::to_string( id.channel);
    }
} // namespace hal
