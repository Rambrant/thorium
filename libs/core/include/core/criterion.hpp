#pragma once

#include <concepts>
#include <cstddef>
#include <meta>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace core
{
    //
    // A predicate is any callable over one or more value types that yields
    // bool. Expressing this as a concept (rather than an IPredicate base class)
    // is what lets a criterion hold *any* predicate -- a struct, a lambda, a
    // factory result -- over any type or collection of types, with zero type
    // erasure and full constexpr evaluation. The variadic Ts... is what admits
    // "collection of types": a predicate may check one reading or several.
    //
    template<typename P, typename... Ts>
    concept PredicateFor =
        requires( const P & predicate, const Ts &... values)
        {
            { predicate( values...) } -> std::convertible_to<bool>;
        };

    //
    // A single named, traceable criterion: metadata (group / id / description,
    // matching a requirement or test-spec entry) plus the predicate that
    // encodes its tolerance. Templated on the predicate type so the exact type
    // is preserved -- the compiler sees straight through it.
    //
    template<typename Predicate>
    struct Criterion
    {
        std::string_view group;
        std::string_view id;
        std::string_view description;

        Predicate predicate;
    };

    template<typename Predicate>
    Criterion( std::string_view,
               std::string_view,
               std::string_view,
               Predicate)
        -> Criterion<Predicate>;

    //
    // Factory used by the CRIT macro. Kept as a free function so the macro
    // expansion stays readable and so criteria can also be built by hand.
    //
    template<typename Predicate>
    constexpr auto makeCriterion( std::string_view group,
                                  std::string_view id,
                                  std::string_view description,
                                  Predicate        predicate) -> Criterion<Predicate>
    {
        return Criterion<Predicate>{ group, id, description, predicate };
    }
} // namespace core

//
// CRITERIA / CRIT / END_CRITERIA: declarative, Excel-readable criteria
// tables. Each CRIT expands to a named `static constexpr` member of the
// group struct, so a misspelled criterion identifier is a compile error
// (undeclared member) -- the compile-time typo check we want, for free.
// Because the group is a struct (not a namespace), it is a first-class
// value: it can be passed around and, once reflection lands (see below),
// iterated and looked up by string.
//
// Deliberately not called GROUP/END_GROUP: that name is reserved for
// grouping test cases (a separate, upcoming mechanism), and the two would
// otherwise collide -- both lexically (same token, easy to misread one for
// the other) and literally (both would try to #define END_GROUP).
//
//   CRITERIA( FS_Fuse_6, "Check of Fuses @ Register CB30")
//       CRIT( FS_Fuse_01, MASK( 0x0Fu, 0x05u), "Low nibble must be 0x5")
//       CRIT( FS_Fuse_02, EQ( 0xF5u),          "Fuse register == 0xF5")
//   END_CRITERIA
//
//   dsl::Verify( FS_Fuse_6::FS_Fuse_01, registerValue);
//
// Note: `Name` (the group's static member) is declared before the CRIT
// members, so each CRIT can reference it to stamp the group into its criterion.
//
#define CRITERIA( groupName, desc)                                    \
    struct groupName                                                  \
    {                                                                 \
        static constexpr std::string_view Name        = #groupName;   \
        static constexpr std::string_view Description = desc;

#define CRIT( id, pred, desc)                                          \
        static constexpr auto id = ::core::makeCriterion( Name, #id, desc, pred);

#define END_CRITERIA };

//
// CRIT_FROM_PRODUCTION( group, id): a companion to CRIT for tolerance-variant
// files (see suite/README.md) where a criterion's value doesn't change
// from production -- group/id are still stated explicitly, so a typo'd id
// still fails to compile exactly like an ordinary CRIT (there's no
// inheritance here, nothing to silently fall back to), but the predicate AND
// description are borrowed from production's matching criterion, so the
// actual tolerance value and its prose live in exactly one place. A typo in
// the reference itself is also a hard compile error: production::group::id
// simply won't exist.
//
// This macro's definition is inert on its own -- it only requires a
// "production" namespace to exist at the point it's actually *used*, not
// merely defined, so declaring it here costs nothing to code that never
// invokes it (e.g. this file's own reflection support below, or
// test_criterion.cpp's throwaway groups). That's different from baking
// "production" awareness into CRITERIA/CRIT themselves, which would force
// every use of the general mechanism to satisfy it.
//
// Deliberately unqualified (production::group::id, not ::production::...):
// resolves correctly whether "production" is a sibling at global scope (a
// real build) or nested inside a wrapper namespace (as in
// test_criteria_variants_compile.cpp, which needs every variant to compile
// side by side rather than picking one).
//
#define CRIT_FROM_PRODUCTION( group, id)                                                   \
        static constexpr auto id = ::core::makeCriterion(                                  \
            Name, #id, production::group::id.description, production::group::id.predicate);

//
// ---------------------------------------------------------------------------
// C++26 / GCC 16 reflection (-freflection): automatic iteration and
// string-name lookup over a group's criteria, with a misspelled name a
// compile error rather than a runtime failure. Verified against Homebrew
// g++-16 16.1.0 -- two things below aren't obvious from the reflection
// proposal and are called out where they bite:
//
//   - a `static constexpr Criterion<...>` member's reflected type is
//     const-qualified, so matching it against `^^Criterion` needs
//     remove_cv() first (isCriterion() below), or nothing ever matches;
//   - splicing (`[: m :]`) a std::meta::info only works when `m` is a
//     genuinely constexpr entity at the splice site -- a std::meta::info
//     value merely *passed through* as an ordinary function parameter
//     (even into another consteval function, even one only ever called
//     with constant arguments) loses that status on this toolchain. That
//     ruled out taking the wanted name as a runtime std::string_view
//     parameter; FixedString below exists so get() can take it as a
//     template argument instead, which doesn't have the problem.
//
namespace core::meta
{
    //
    // A compile-time string usable as a non-type template parameter -- see
    // this block's own comment above for why get() needs the wanted name as
    // a template argument rather than a std::string_view parameter.
    //
    template<std::size_t N>
    struct FixedString
    {
        char value[N]{};

        consteval FixedString( const char ( &str)[N])
        {
            for ( std::size_t i = 0; i < N; ++i) value[i] = str[i];
        }

        [[nodiscard]]
        constexpr auto view() const -> std::string_view
        {
            return std::string_view( value, N - 1);
        }
    };

    namespace detail
    {
        //
        // True for a reflected type that is a specialization of
        // core::Criterion<...> -- what distinguishes a CRIT member from
        // Group's other static members (Name, Description). See this
        // block's own comment above for why remove_cv() is required.
        //
        consteval auto isCriterion( std::meta::info type) -> bool
        {
            return std::meta::has_template_arguments( type)
                && std::meta::template_of( type) == ^^Criterion;
        }

        //
        // Every CRIT member of Group, in declaration order.
        //
        template<typename Group>
        consteval auto criterionMembers() -> std::vector<std::meta::info>
        {
            std::vector<std::meta::info> result;

            for ( auto member : std::meta::members_of( ^^Group, std::meta::access_context::current()))
            {
                if ( std::meta::is_variable( member) &&
                     isCriterion( std::meta::remove_cv( std::meta::type_of( member))))
                    result.push_back( member);
            }

            return result;
        }

        //
        // define_static_array() promotes criterionMembers()'s result to
        // static storage and hands back a std::span over it -- needed
        // because splicing requires a genuinely constexpr entity (see this
        // block's own comment above), which a std::vector<...> computed
        // inline is not, but a variable template is. Templated on Group so
        // each group's array is computed independently.
        //
        template<typename Group>
        constexpr auto members = std::define_static_array( criterionMembers<Group>());

        //
        // static_assert( false, ...) is unconditionally ill-formed inside a
        // template body since C++23 (P2593), even in an if-constexpr branch
        // that's never instantiated for any Group -- a literal `false` in
        // get() below would fail to compile for every group, found-or-not.
        // Making it depend on Group defers the check to instantiation, where
        // if constexpr actually discards the branch once the id is found.
        //
        template<typename> constexpr bool dependentFalse = false;

        template<typename Group, FixedString Wanted>
        consteval auto indexOf() -> std::size_t
        {
            for ( std::size_t i = 0; i < members<Group>.size(); ++i)
                if ( std::meta::identifier_of( members<Group>[ i]) == Wanted.view())
                    return i;

            return static_cast<std::size_t>( -1);
        }
    } // namespace detail

    //
    // Every CRIT in Group, as a tuple -- criteria in the same group hold
    // different Predicate types (MASK(...) and EQ(...) are different
    // structs, see predicates.hpp), so a tuple is the only container that
    // can hold them all without type erasure.
    //
    //   constexpr auto all = core::meta::all<FS_Fuse_6>();
    //   static_assert( std::get<0>( all).id == "FS_Fuse_01");
    //
    template<typename Group>
    consteval auto all()
    {
        return []<std::size_t... I>( std::index_sequence<I...>)
        {
            return std::make_tuple( [: detail::members<Group>[ I] :] ...);
        }( std::make_index_sequence<detail::members<Group>.size()>{});
    }

    //
    // The one criterion in Group named Wanted, looked up at compile time. A
    // misspelled Wanted is a hard compile error (see dependentFalse's own
    // comment above) -- there is no runtime fallback, the same guarantee
    // CRIT's own member-typo check already gives code that spells the name
    // directly (FS_Fuse_6::FS_Fuse_01); get() is for the case where the name
    // is only available as a string at the call site -- e.g. built by
    // another macro -- rather than spelled as an identifier.
    //
    //   constexpr auto c = core::meta::get<FS_Fuse_6, "FS_Fuse_01">();
    //
    template<typename Group, FixedString Wanted>
    consteval auto get()
    {
        constexpr auto idx = detail::indexOf<Group, Wanted>();

        if constexpr ( idx == static_cast<std::size_t>( -1))
        {
            static_assert( detail::dependentFalse<Group>, "no criterion with that id in this group");
        }
        else
        {
            return [: detail::members<Group>[ idx] :];
        }
    }
} // namespace core::meta
// ---------------------------------------------------------------------------
//