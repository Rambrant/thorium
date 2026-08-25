#pragma once

#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core::meta
{
    namespace detail
    {
        //
        // Every enumerator of Enum, as opaque std::meta::info handles --
        // std::meta::info itself is fine to promote to static storage (see
        // enumerators<Enum> below), unlike a struct with a std::string_view
        // member (see hal::detail::ConnectorWiringKey's own comment in
        // hal/wiring.hpp for why that one isn't), so nothing here needs to
        // work around a non-structural-type limit -- the enumerator's name
        // and value are pulled out of each info directly, at the point
        // to_string()/fromString() below actually use it.
        //
        template<typename Enum>
        consteval auto enumeratorInfos() -> std::vector<std::meta::info>
        {
            return std::meta::enumerators_of( ^^Enum);
        }

        template<typename Enum>
        constexpr auto enumerators = std::define_static_array( enumeratorInfos<Enum>());

        //
        // ASCII-only, hand-rolled rather than std::toupper: the enumerator
        // names this runs over are C++ identifiers (letters/digits/
        // underscore only), and std::toupper isn't constexpr on this
        // standard library (it's a C library function, locale-dependent by
        // signature even though nothing here uses a locale) -- upperNames()
        // below needs this to run at compile time.
        //
        consteval auto toUpperAscii( std::string_view text) -> std::string
        {
            std::string result;

            for( char c : text)
            {
                result.push_back( ( c >= 'a' && c <= 'z') ? static_cast<char>( c - 'a' + 'A') : c);
            }

            return result;
        }

        //
        // One uppercased, permanent-storage C string per enumerator, in the
        // same order as enumerators<Enum> above -- std::define_static_string
        // is what makes "compute a name at compile time" produce something
        // safe to hand back as a std::string_view at runtime (see
        // to_string_upper() below), the same role std::define_static_array
        // plays for a whole container rather than one string.
        //
        template<typename Enum>
        consteval auto upperNames() -> std::vector<const char*>
        {
            std::vector<const char*> names;

            for( auto enumerator : enumeratorInfos<Enum>())
            {
                names.push_back( std::define_static_string( toUpperAscii( std::meta::identifier_of( enumerator))));
            }

            return names;
        }

        template<typename Enum>
        constexpr auto upperNameTable = std::define_static_array( upperNames<Enum>());

        //
        // The enumerators as *values* rather than as std::meta::info handles
        // -- what values<Enum> below hands out. An enum is a structural type,
        // so unlike the name tables above this needs no define_static_string
        // step to survive into runtime storage.
        //
        template<typename Enum>
        consteval auto enumeratorValues() -> std::vector<Enum>
        {
            std::vector<Enum> result;

            for( auto enumerator : enumeratorInfos<Enum>())
            {
                result.push_back( std::meta::extract<Enum>( enumerator));
            }

            return result;
        }
    } // namespace detail

    //
    // Every value Enum declares, in declaration order -- for the ordinary
    // "do this for each of them" loop, which otherwise gets written as a
    // braced list repeating every enumerator at each call site.
    //
    // Same argument as to_string() above, one step further: that one removed
    // the second spelling of each enumerator's *name*, this removes the
    // second spelling of the enumerator *list*. A braced list is worse than a
    // switch in one respect -- a switch at least has -Wswitch to notice a
    // forgotten case, where a list that has silently stopped covering the
    // enum looks exactly like one that never did.
    //
    template<typename Enum>
    constexpr auto values = std::define_static_array( detail::enumeratorValues<Enum>());

    //
    // Generic enum <-> name, reflecting over Enum's own enumerators rather
    // than a hand-written switch/if-chain per enum -- see hal::to_string(
    // InstrumentId) and core::to_string(QuantityKind) for the two this
    // replaced, both of which used to redeclare every enumerator's name a
    // second time (a switch case, or an if/return) purely to stringify it.
    // Adding an enumerator here is a compile error nowhere -- unlike the
    // switch form, there is no -Wswitch to catch a forgotten case, because
    // there is no case to forget; to_string()/fromString() below see
    // whatever Enum actually declares, automatically.
    //
    // Returns the enumerator's own spelling verbatim (Enum::Debug ->
    // "Debug"), which is exactly what hal::InstrumentId/core::QuantityKind/
    // hal::SwitchDeviceKind want (see each one's own to_string test). Where
    // the wanted string ISN'T the enumerator's own spelling -- core::LogLevel
    // wants "DEBUG", not "Debug" -- see to_string_upper() below rather than
    // hand-writing a new switch.
    //
    //
    // [[maybe_unused]] on the parameter, for the one enum that has no
    // enumerators to loop over. A rig with no switching hardware declares an
    // empty SWITCH_DEVICES table, so hal::SwitchDeviceId is a legal enum with
    // no values (see dev/rig/devices.inc) -- the template for below then
    // expands to nothing, the parameter is genuinely never read, and
    // -Wunused-but-set-parameter is right about the code and wrong about the
    // intent. Nothing is lost by silencing it: for any enum that has
    // enumerators the parameter is used, and the attribute permits rather than
    // asserts.
    //
    template<typename Enum>
    [[nodiscard]] constexpr auto to_string( [[maybe_unused]] Enum value) -> std::string_view
    {
        std::string_view result = "Unknown";

        template for( constexpr auto enumerator : detail::enumerators<Enum>)
        {
            if( value == [: enumerator :])
            {
                result = std::meta::identifier_of( enumerator);
            }
        }

        return result;
    }

    //
    // Inverse of to_string() above -- std::nullopt for any text that isn't
    // exactly one of Enum's own enumerator names, letting each concrete
    // wrapper (e.g. core::quantityKindFromString) decide how to fail (throw,
    // default, ...) rather than baking one choice in here.
    //
    template<typename Enum>
    [[nodiscard]] constexpr auto fromString( [[maybe_unused]] std::string_view text) -> std::optional<Enum>
    {
        std::optional<Enum> result;

        template for( constexpr auto enumerator : detail::enumerators<Enum>)
        {
            if( text == std::meta::identifier_of( enumerator))
            {
                result = [: enumerator :];
            }
        }

        return result;
    }

    //
    // Same idea as to_string() above, uppercased -- for the enum whose
    // to_string wants Enum::Debug -> "DEBUG" rather than "Debug" (see
    // core::to_string(LogLevel)). Still reflects Enum's own enumerators, so
    // an added enumerator needs no matching update here either; only the
    // transform (ASCII-uppercase, see detail::toUpperAscii's own comment)
    // is hand-written, not each name.
    //
    template<typename Enum>
    [[nodiscard]] constexpr auto to_string_upper( [[maybe_unused]] Enum value) -> std::string_view
    {
        std::string_view result = "UNKNOWN";
        std::size_t index = 0;

        template for( constexpr auto enumerator : detail::enumerators<Enum>)
        {
            if( value == [: enumerator :])
            {
                result = detail::upperNameTable<Enum>[ index];
            }

            ++index;
        }

        return result;
    }
} // namespace core::meta
