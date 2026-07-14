#pragma once

#include <stdexcept>
#include <string>
#include <variant>

#include "core/quantity.hpp"

namespace core
{
    //
    // A runtime tag for "which Quantity<Unit> is this", mirroring the closed
    // set of aliases in core::quantities (Voltage, Current, ...). This exists
    // so code that must work across every unit at runtime -- the session
    // hierarchy below, in particular -- can carry a value's kind and payload
    // together without itself being a template. Compile-time unit safety
    // (Voltage vs Current) still lives entirely in Quantity<Unit>/Port<Unit,
    // Instrument>; QuantityKind only re-describes that same closed set for the
    // one seam (ISession::fetch) that virtual dispatch can't be templated on.
    //
    enum class QuantityKind
    {
        Voltage,
        Current,
        Power,
        ApparentPower,
        Resistance,
        Time,
        Decibel,
        Frequency,
        PowerFactor,
        ReactivePower
    };

    [[nodiscard]]
    auto to_string( QuantityKind kind) -> std::string_view;

    //
    // Inverse of to_string(QuantityKind) -- parses the kind column back out of
    // a recording file. Throws on anything not produced by to_string, rather
    // than silently defaulting to some kind, since a bad kind column means the
    // rest of that row can't be trusted either.
    //
    [[nodiscard]]
    auto quantityKindFromString( std::string_view text) -> QuantityKind;

    //
    // One variant alternative per QuantityKind, in the same order, so
    // std::variant's index() lines up with the enum's underlying value.
    // A static_assert in quantity_kind.cpp keeps the two declarations honest
    // against each other if either one grows.
    //
    using QuantityVariant = std::variant<
        quantities::Voltage,
        quantities::Current,
        quantities::Power,
        quantities::ApparentPower,
        quantities::Resistance,
        quantities::Time,
        quantities::Decibel,
        quantities::Frequency,
        quantities::PowerFactor,
        quantities::ReactivePower>;

    //
    // Maps a concrete Quantity<Unit> type to its QuantityKind at compile time.
    // Specialized once per unit below; add a line here whenever a new unit is
    // added to core::quantities. Deliberately left undefined for any type not
    // specialized, so using an unknown Quantity<Unit> here is a compile error
    // rather than a silently wrong kind.
    //
    template<typename Q>
    struct QuantityKindOf;

    template<> struct QuantityKindOf<quantities::Voltage>       { static constexpr QuantityKind value = QuantityKind::Voltage; };
    template<> struct QuantityKindOf<quantities::Current>       { static constexpr QuantityKind value = QuantityKind::Current; };
    template<> struct QuantityKindOf<quantities::Power>         { static constexpr QuantityKind value = QuantityKind::Power; };
    template<> struct QuantityKindOf<quantities::ApparentPower> { static constexpr QuantityKind value = QuantityKind::ApparentPower; };
    template<> struct QuantityKindOf<quantities::Resistance>    { static constexpr QuantityKind value = QuantityKind::Resistance; };
    template<> struct QuantityKindOf<quantities::Time>          { static constexpr QuantityKind value = QuantityKind::Time; };
    template<> struct QuantityKindOf<quantities::Decibel>       { static constexpr QuantityKind value = QuantityKind::Decibel; };
    template<> struct QuantityKindOf<quantities::Frequency>     { static constexpr QuantityKind value = QuantityKind::Frequency; };
    template<> struct QuantityKindOf<quantities::PowerFactor>   { static constexpr QuantityKind value = QuantityKind::PowerFactor; };
    template<> struct QuantityKindOf<quantities::ReactivePower> { static constexpr QuantityKind value = QuantityKind::ReactivePower; };

    template<quantities::QuantityType Q>
    [[nodiscard]]
    constexpr auto quantityKindOf() -> QuantityKind
    {
        return QuantityKindOf<Q>::value;
    }

    //
    // The raw double inside a QuantityVariant, regardless of which unit is
    // live. Pairs with quantityVariantFromKind -- together they're what let
    // core/recording.hpp serialize a value as (kind, double) and rebuild it
    // later without caring which of the ten units it happens to be.
    //
    [[nodiscard]]
    auto rawValue( const QuantityVariant & value) -> double;

    //
    // Builds a QuantityVariant from a kind decided at runtime plus a raw
    // double -- the inverse of QuantityVariant's usual construction from a
    // concrete Quantity<Unit>. Used only where the kind genuinely isn't known
    // until runtime, such as rebuilding a sample read back from a recording
    // file; ordinary code should prefer constructing the concrete Quantity<Unit>
    // directly.
    //
    [[nodiscard]]
    auto quantityVariantFromKind( QuantityKind kind, double value) -> QuantityVariant;

    //
    // Unwraps a QuantityVariant into a concrete Quantity<Unit>, throwing if the
    // variant's live alternative doesn't match. This is the one place a kind
    // mismatch between what a session actually holds and what the caller asked
    // for turns into a runtime error -- see core/session.hpp for callers.
    //
    template<quantities::QuantityType Q>
    [[nodiscard]]
    auto asQuantity( const QuantityVariant & value) -> Q
    {
        if( const auto * q = std::get_if<Q>( &value))
        {
            return *q;
        }

        throw std::runtime_error(
            "QuantityVariant holds a different unit than requested (" +
            std::string( to_string( static_cast<QuantityKind>( value.index()))) + ")");
    }
} // namespace core
