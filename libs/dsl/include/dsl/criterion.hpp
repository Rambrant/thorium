#pragma once

#include <string_view>

namespace dsl
{
    template< typename Predicate>
    struct Criterion
    {
        std::string_view group;
        std::string_view id;
        std::string_view description;

        Predicate predicate;
    };

    template< typename Predicate>
    Criterion( std::string_view,
               std::string_view,
               std::string_view,
               Predicate)
        -> Criterion<Predicate>;
} // namespace dsl

//
// GROUP/END_GROUP/CRIT: declarative macros for defining named, traceable
// criteria tables. Each CRIT expands to a `dsl::Criterion` constant that
// can be passed straight to dsl::Verify. Kept at global scope (rather than
// inside dsl::) so scripts can invoke them unqualified, e.g.:
//
//   GROUP(FS_Fuse_6, "Check of Fuses @ Register CB30")
//       CRIT(FS_Fuse_01, dsl::BAND(0x0Fu, 0x05u), "Value @ Register CB30")
//   END_GROUP
//
//   dsl::Verify(FS_Fuse_6::FS_Fuse_01, register_value);
//
#define GROUP( name, desc)                                       \
    namespace name                                              \
    {                                                            \
        inline constexpr std::string_view GroupName = #name;    \
        inline constexpr std::string_view GroupDescription = desc;

#define END_GROUP }

#define CRIT( id, pred, desc)                  \
    inline constexpr ::dsl::Criterion id      \
    {                                         \
        GroupName,                            \
        #id,                                  \
        desc,                                 \
        pred                                  \
    };
