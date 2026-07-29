#include "core/quantity_kind.hpp"

#include "core/meta.hpp"

namespace core
{
    namespace
    {
        //
        // QuantityVariant's alternatives are declared in the same order as
        // the QuantityKind enumerators, so that a variant's index() can be
        // cast straight back to a QuantityKind (used in asQuantity's error
        // message, and available to any other caller that only has an index).
        // This keeps that assumption from silently rotting if either list is
        // reordered without the other.
        //
        static_assert( std::variant_size_v<QuantityVariant> == 10,
            "QuantityVariant's alternative count must match QuantityKind's enumerator count");

        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Voltage),       QuantityVariant>, quantities::Voltage>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Current),       QuantityVariant>, quantities::Current>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Power),         QuantityVariant>, quantities::Power>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::ApparentPower), QuantityVariant>, quantities::ApparentPower>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Resistance),    QuantityVariant>, quantities::Resistance>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Time),         QuantityVariant>, quantities::Time>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Decibel),      QuantityVariant>, quantities::Decibel>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::Frequency),    QuantityVariant>, quantities::Frequency>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::PowerFactor),  QuantityVariant>, quantities::PowerFactor>);
        static_assert( std::is_same_v< std::variant_alternative_t<static_cast<std::size_t>( QuantityKind::ReactivePower),QuantityVariant>, quantities::ReactivePower>);
    } // namespace

    auto rawValue( const QuantityVariant & value) -> double
    {
        return std::visit( []( const auto & q) { return q.value(); }, value);
    }

    auto quantityVariantFromKind( const QuantityKind kind, const double value) -> QuantityVariant
    {
        switch( kind)
        {
            case QuantityKind::Voltage:       return quantities::Voltage{ value };
            case QuantityKind::Current:       return quantities::Current{ value };
            case QuantityKind::Power:         return quantities::Power{ value };
            case QuantityKind::ApparentPower: return quantities::ApparentPower{ value };
            case QuantityKind::Resistance:    return quantities::Resistance{ value };
            case QuantityKind::Time:          return quantities::Time{ value };
            case QuantityKind::Decibel:       return quantities::Decibel{ value };
            case QuantityKind::Frequency:     return quantities::Frequency{ value };
            case QuantityKind::PowerFactor:   return quantities::PowerFactor{ value };
            case QuantityKind::ReactivePower: return quantities::ReactivePower{ value };
        }

        throw std::runtime_error( "quantityVariantFromKind: unknown QuantityKind");
    }

    //
    // Reflects over QuantityKind's own enumerators (core::meta::to_string(),
    // core/meta.hpp) rather than restating each one's name a second time --
    // see that header's own comment on why this is a clean fit (the
    // enumerator's spelling IS the string every caller here wants) where
    // core::to_string(LogLevel) is deliberately not.
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
