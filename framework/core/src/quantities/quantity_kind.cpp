#include "core/quantities/quantity_kind.hpp"

#include <array>
#include <utility>

#include "core/meta.hpp"

namespace core
{
    namespace
    {
        //
        // Rebuilds alternative I of QuantityVariant from a raw double. One
        // function template instead of a ten-case switch: the alternative's own
        // type is what says which unit to construct, so there is nothing here to
        // keep in step with the enum.
        //
        template<std::size_t I>
        auto variantAlternativeFrom( const double value) -> QuantityVariant
        {
            return QuantityVariant{ std::in_place_index<I>, std::variant_alternative_t<I, QuantityVariant>{ value } };
        }

        //
        // A jump table over the variant's alternatives, indexed by
        // QuantityKind's underlying value. This replaces
        // quantityVariantFromKind's switch, and with it the last per-unit list
        // in this file -- see core/quantities/quantity_kind.hpp on why the enum is now the
        // only one.
        //
        // -Wswitch used to be what caught a forgotten case here. The equivalent
        // guarantee is now structural rather than diagnostic: the table has
        // exactly one entry per alternative because it is built from
        // make_index_sequence over the alternatives, so a new kind is wired up
        // by existing.
        //
        using FromDouble = QuantityVariant ( *)( double);

        template<std::size_t... I>
        constexpr auto makeFromDoubleTable( std::index_sequence<I...>) -> std::array<FromDouble, sizeof...( I)>
        {
            return { &variantAlternativeFrom<I>... };
        }

        constexpr auto kFromDouble = makeFromDoubleTable( std::make_index_sequence<std::variant_size_v<QuantityVariant>>{});
    } // namespace

    auto rawValue( const QuantityVariant & value) -> double
    {
        return std::visit( []( const auto & q) { return q.value(); }, value);
    }

    auto quantityVariantFromKind( const QuantityKind kind, const double value) -> QuantityVariant
    {
        const auto index = static_cast<std::size_t>( kind);

        if( index >= kFromDouble.size())
        {
            //
            // Only reachable from a cast of an out-of-range integer -- a
            // corrupt recording row, say, since quantityKindFromString already
            // rejects an unknown name. Kept as a throw rather than a contract
            // violation for the same reason readRecording throws on a malformed
            // row: it is bad input, not a bug in the caller.
            //
            throw std::runtime_error( "quantityVariantFromKind: unknown QuantityKind");
        }

        return kFromDouble[ index]( value);
    }

    //
    // Reflects over QuantityKind's own enumerators (core::meta::to_string(),
    // core/meta.hpp) rather than restating each one's name a second time --
    // see that header's own comment on why this is a clean fit (the
    // enumerator's spelling IS the string every caller here wants), unlike
    // an enum whose log text differs from how it is spelled in code.
    //
    auto to_string( const QuantityKind kind) -> std::string_view
    {
        return meta::to_string( kind);
    }

    auto quantityKindFromString( const std::string_view text) -> QuantityKind
    {
        if( const auto kind = meta::fromString<QuantityKind>( text))
        {
            return *kind;
        }

        throw std::runtime_error( "quantityKindFromString: unknown kind '" + std::string( text) + "'");
    }
} // namespace core
