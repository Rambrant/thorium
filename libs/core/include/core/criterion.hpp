#pragma once

#include <concepts>
#include <string_view>

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
// GROUP / CRIT / END_GROUP: declarative, Excel-readable criteria tables. Each
// CRIT expands to a named `static constexpr` member of the group struct, so a
// misspelled criterion identifier is a compile error (undeclared member) --
// the compile-time typo check we want, for free. Because the group is a struct
// (not a namespace), it is a first-class value: it can be passed around and,
// once reflection lands (see below), iterated and looked up by string.
//
//   GROUP( FS_Fuse_6, "Check of Fuses @ Register CB30")
//       CRIT( FS_Fuse_01, core::MASK( 0x0Fu, 0x05u), "Low nibble must be 0x5")
//       CRIT( FS_Fuse_02, core::EQ( 0xF5u),          "Fuse register == 0xF5")
//   END_GROUP
//
//   dsl::Verify( FS_Fuse_6::FS_Fuse_01, registerValue);
//
// Note: `Name` (the group's static member) is declared before the CRIT
// members, so each CRIT can reference it to stamp the group into its criterion.
//
#define GROUP( groupName, desc)                                       \
    struct groupName                                                  \
    {                                                                 \
        static constexpr std::string_view Name        = #groupName;   \
        static constexpr std::string_view Description = desc;

#define CRIT( id, pred, desc)                                          \
        static constexpr auto id = ::core::makeCriterion( Name, #id, desc, pred);

#define END_GROUP };

//
// ---------------------------------------------------------------------------
// FUTURE (C++26 / GCC 16 reflection, -freflection): automatic iteration and
// string-name lookup over a group's criteria, with a misspelled string name a
// compile error via static_assert. This needs static reflection to enumerate
// the group struct's static members -- it cannot be expressed in C++23, and is
// left commented out here as a starting point to verify on a GCC 16 toolchain.
//
// The intent:
//   - `all<FS_Fuse_6>()`            -> a tuple of every CRIT in the group
//   - `get<FS_Fuse_6, "FS_Fuse_01">()` -> that criterion, or a hard compile error
//
// A compile-time fixed string usable as a non-type template parameter, needed
// so the requested name can be a template argument (which is what makes a bad
// name a compile error):
//
// template<std::size_t N>
// struct FixedString
// {
//     char value[N]{};
//
//     consteval FixedString( const char ( &str)[N])
//     {
//         for( std::size_t i = 0; i < N; ++i) value[i] = str[i];
//     }
//
//     [[nodiscard]]
//     constexpr auto view() const -> std::string_view
//     {
//         return std::string_view( value, N - 1);
//     }
// };
//
// namespace core::meta
// {
//     // Reflect over Group's static data members, keeping those whose type is a
//     // Criterion<...> specialization, and fold them into a tuple.
//     template<typename Group>
//     consteval auto all()
//     {
//         // Pseudocode against P2996 reflection:
//         //   constexpr auto members = std::meta::nonstatic_data_members_of(^Group);
//         //   ... filter to Criterion<> members, splice into std::make_tuple(...)
//         // NOTE: group criteria are *static* members, so use the appropriate
//         // members-of query for statics on the target GCC 16 reflection API.
//     }
//
//     template<typename Group, FixedString Wanted>
//     consteval auto get()
//     {
//         // Walk all<Group>(), compare each .id against Wanted.view();
//         // return the match, else:
//         //   static_assert(false, "no criterion with that id in this group");
//     }
// }
// ---------------------------------------------------------------------------
//