#include "hal/address.hpp"

#include <meta>
#include <type_traits>

namespace hal
{
    namespace
    {
        //
        // The bus kind's own name, taken from the alternative's type name --
        // "Gpib", "Lan", "Simulated" -- rather than from a string literal
        // repeating it. Same reasoning as core::meta::to_string() over an
        // enum's enumerators (see core/meta.hpp), and the same consequence: a
        // sixth bus kind added to hal::Address needs no name written here,
        // only the detailOf() overload below that says what its fields are.
        //
        template<typename AddressT>
        [[nodiscard]]
        auto kindName() -> std::string_view
        {
            return std::meta::identifier_of( ^^AddressT);
        }

        //
        // Everything after the kind name -- which fields a given bus kind has
        // is the one thing reflection can't render usefully here, since
        // "0::14" and "bench-dmm1:5025" are the conventional spellings for
        // their respective buses rather than anything derivable from a field
        // list.
        //
        [[nodiscard]]
        auto detailOf( const Gpib & address) -> std::string
        {
            const auto secondary = address.secondary.has_value() ? "::" + std::to_string( address.secondary.value()) : std::string{};

            return std::to_string( address.board) + "::" + std::to_string( address.primary) + secondary;
        }

        [[nodiscard]]
        auto detailOf( const Lan & address) -> std::string
        {
            return std::string( address.host) + ":" + std::to_string( address.port);
        }

        [[nodiscard]]
        auto detailOf( const Serial & address) -> std::string
        {
            return std::string( address.device);
        }

        [[nodiscard]]
        auto detailOf( const Usb & address) -> std::string
        {
            return std::string( address.serialNumber);
        }

        //
        // Nothing to say beyond the kind itself -- see hal::Simulated's own
        // comment in hal/address.hpp.
        //
        [[nodiscard]]
        auto detailOf( const Simulated &) -> std::string
        {
            return {};
        }
    } // namespace

    auto to_string( const Address & address) -> std::string
    {
        return std::visit( []( const auto & kind) -> std::string
        {
            const auto name   = std::string( kindName<std::remove_cvref_t<decltype( kind)>>());
            const auto detail = detailOf( kind);

            return detail.empty() ? name : name + " " + detail;
        }, address);
    }
} // namespace hal
