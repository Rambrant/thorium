#pragma once

#include <cstddef>
#include <meta>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/quantities/quantity.hpp"

namespace core
{
    //
    // A runtime tag for "which Quantity<Unit> is this", mirroring the closed set
    // of aliases in core::quantities (Voltage, Current, ...). This exists so code
    // that must work across every unit at runtime -- the session hierarchy, in
    // particular -- can carry a value's kind and payload together without itself
    // being a template. Compile-time unit safety (Voltage vs Current) still lives
    // entirely in Quantity<Unit>/Port<Unit, Instrument>; QuantityKind only
    // re-describes that same closed set for the one seam (ISession::fetch) that
    // virtual dispatch can't be templated on.
    //
    // ---------------------------------------------------------------------------
    // This enum is the ONE list. Everything else about a unit is derived.
    // ---------------------------------------------------------------------------
    // Adding a unit to this framework used to mean editing nine parallel
    // per-unit lists -- a tag, an alias, a literal, an enumerator, a variant
    // alternative, two trait specialisations, a from-kind switch case, and a
    // symbol switch case -- with compile-time guards bolted on to catch the ones
    // you forgot. Guards make that safe; they do not make it simple, and eight
    // of the nine existed only to restate what the other one already said.
    //
    // Now there are three, all of them genuine vocabulary a human has to choose:
    //
    //   1. the unit tag, carrying its printed symbol   (core/quantities/quantity.hpp)
    //   2. the alias, e.g. `using Voltage = ...`       (core/quantities/quantity.hpp)
    //   3. the enumerator below, named to match (2)
    //
    // From those, QuantityVariant, QuantityFor, quantityKindOf, the symbol
    // lookup and quantityVariantFromKind are all generated. The
    // enumerator-order-must-match-variant-order invariant that used to be
    // asserted ten times in quantity_kind.cpp is now true by construction: the
    // variant is BUILT from the enumerators, in their order, so there is no
    // second ordering left to disagree.
    //
    // The one thing reflection cannot do here is create the enumerators
    // themselves -- naming a new entity needs token injection, which this
    // toolchain's reflection subset does not have. That is why this enum stays
    // hand-written rather than being generated from the aliases. It is also why
    // that is the right way round: an enumerator is a name that a POINT() entry
    // (see hal/topology/adapter.hpp) and a recording file (core/session/recording.hpp) both refer
    // to by spelling, so it has to be a stable, deliberate declaration -- not a
    // by-product of what types happen to exist.
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
        ReactivePower,
        //
        // Appended rather than filed next to a related unit, because a kind's
        // position is the alternative index of QuantityVariant: inserting one
        // mid-list renumbers every kind after it, and a recording written by
        // the previous build would still parse (the file stores the *name*,
        // see to_string) while anything that had cached an index would not.
        // Appending costs nothing and cannot do that.
        //
        Temperature,
        TemperatureDelta
    };

    namespace detail
    {
        //
        // Stands in for an enumerator that has no matching Quantity alias, so
        // that the variant below is still a well-formed type and the
        // static_assert further down is the *only* thing that fails.
        //
        // Without it the lookup's "not found" answer (^^void) propagates into
        // std::variant, and a missing alias produces six errors -- the clear one
        // buried under variant's own complaints about void alternatives and a
        // page of template arguments. Selecting this type is only ever possible
        // in a build that is already failing on that assert, so its behaviour is
        // irrelevant; it just has to satisfy everything a real alternative does
        // (constructible from a double, has value(), has symbol()) so nothing
        // downstream adds noise of its own.
        //
        struct MissingQuantityAlias
        {
            constexpr explicit MissingQuantityAlias( double = 0.0) {}

            [[nodiscard]] constexpr auto value() const -> double { return 0.0; }

            [[nodiscard]] static constexpr auto symbol() -> std::string_view { return "?"; }
        };

        //
        // The type in core::quantities whose name matches this enumerator --
        // QuantityKind::Voltage -> core::quantities::Voltage.
        //
        // Matching by identifier is what ties the two hand-written lists
        // together without a third list mapping between them. dealias() is
        // needed because the members being matched are alias declarations
        // (`using Voltage = Quantity<V_Type>;`), and what the variant wants is
        // the type they name.
        //
        // Returns MissingQuantityAlias when there is no match, which
        // everyKindHasAQuantity() below turns into a readable compile error --
        // see both of their own comments.
        //
        consteval auto quantityTypeFor( const std::meta::info enumerator) -> std::meta::info
        {
            for( auto member : std::meta::members_of( ^^quantities, std::meta::access_context::current()))
            {
                if( std::meta::has_identifier( member) &&
                    std::meta::identifier_of( member) == std::meta::identifier_of( enumerator))
                {
                    return std::meta::dealias( member);
                }
            }

            return ^^MissingQuantityAlias;
        }

        consteval auto quantityTypes() -> std::vector<std::meta::info>
        {
            std::vector<std::meta::info> types;

            for( auto enumerator : std::meta::enumerators_of( ^^QuantityKind))
            {
                types.push_back( quantityTypeFor( enumerator));
            }

            return types;
        }

        //
        // Promoted to static storage so it can be spliced -- the same reason
        // core::meta and hal::safeRig reach for define_static_array: a
        // std::vector<std::meta::info> computed inline is not a constexpr entity
        // a splice can name, and a variable template is.
        //
        constexpr auto quantityTypeList = std::define_static_array( quantityTypes());

        //
        // Checked separately from the lookup rather than thrown from inside it,
        // so a missing alias produces this sentence instead of the "call to
        // consteval function is not a constant expression" a throw would give.
        // This is the diagnostic that replaces the ten static_asserts: the
        // failure it catches is "you added an enumerator and nothing else",
        // which was previously caught by the variant/enum order asserts.
        //
        consteval auto everyKindHasAQuantity() -> bool
        {
            for( auto type : quantityTypes())
            {
                if( type == ^^MissingQuantityAlias)
                {
                    return false;
                }
            }

            return true;
        }

        static_assert( everyKindHasAQuantity(),
            "every QuantityKind enumerator needs a same-named Quantity alias in core::quantities"
            " -- add both, plus the unit tag carrying its symbol; see core/quantities/quantity.hpp");

        template<std::size_t... I>
        consteval auto quantityVariantType( std::index_sequence<I...>)
            -> std::variant<typename [: quantityTypeList[ I] :]...>;
    } // namespace detail

    //
    // One alternative per QuantityKind, in the same order -- generated from the
    // enumerators themselves (see this header's own comment), so index() casts
    // straight back to a QuantityKind and cannot drift.
    //
    using QuantityVariant =
        decltype( detail::quantityVariantType( std::make_index_sequence<detail::quantityTypeList.size()>{}));

    //
    // The concrete Quantity<Unit> for a kind known at compile time -- needed
    // wherever a QuantityKind is baked into a template parameter (an
    // AdapterPointTag, see core/topology/adapter.hpp) but the concrete type is what's
    // required to name a Port<Q, Instrument>.
    //
    // Was ten QuantityTypeOf specialisations; the variant already holds exactly
    // this mapping, in exactly this order, so asking it is both shorter and
    // impossible to get out of step.
    //
    template<QuantityKind Kind>
    using QuantityFor = std::variant_alternative_t<static_cast<std::size_t>( Kind), QuantityVariant>;

    //
    // The inverse: a concrete Quantity<Unit>'s kind, at compile time.
    //
    // Was ten QuantityKindOf specialisations. Now the variant decides, by
    // answering which alternative it would store this type in -- which is the
    // definition of the mapping rather than a restatement of it. A type that is
    // not one of the alternatives fails to compile here (std::variant has no
    // alternative to select), which is the same "unknown unit is a compile
    // error" guarantee the undefined-by-default primary template used to give.
    //
    template<quantities::QuantityType Q>
    [[nodiscard]]
    constexpr auto quantityKindOf() -> QuantityKind
    {
        return static_cast<QuantityKind>( QuantityVariant{ Q{} }.index());
    }

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
    // The raw double inside a QuantityVariant, regardless of which unit is
    // live. Pairs with quantityVariantFromKind -- together they're what let
    // core/session/recording.hpp serialize a value as (kind, double) and rebuild it
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
    // for turns into a runtime error -- see core/session/session.hpp for callers.
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
