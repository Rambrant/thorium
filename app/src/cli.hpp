#pragma once

//
// Command line parsing and the --help text, both generated from annotations on
// the options struct itself (C++26 P3394, read back through P2996 reflection --
// this needs -freflection, which the top-level CMakeLists.txt passes globally).
//
// The point is that a flag is written down once. Before this, adding one meant
// editing the Options struct, then the parser's if/else chain, and then whatever
// documented it -- three places, with nothing checking they agreed. Here the
// struct member *is* the declaration: its type decides whether the flag takes a
// value and how that value is parsed, its Flag annotations give the spelling(s),
// and its Doc annotation is the --help line. There is no second list to fall out
// of step.
//
// Deliberately generic: nothing below knows anything about run_scripts, and the
// whole vocabulary is the five annotation types in the "Vocabulary" section. It
// lives in app/src/ rather than libs/core/include/ because main.cpp is its only
// consumer and a general-purpose CLI framework is not something libs/core should
// be committing to as installed public surface. If a second consumer ever
// appears, that is the moment to move it, not before.
//
// What this does NOT do, on purpose: invariants that span two flags (--record
// with --replay), or a value whose legality only some other component knows
// (--criteria=, validated by core::selectCriteriaVariant). Those stay written
// out by hand in main.cpp. The per-flag mechanics are what generate well; the
// judgement does not, and pretending otherwise would bury it.
//

#include <charconv>
#include <cstdint>
#include <meta>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cli
{
    // -----------------------------------------------------------------------
    // Vocabulary
    // -----------------------------------------------------------------------

    //
    // An annotation's value must be of *structural* type, and std::string_view
    // is not one -- its members are private. Text is the smallest thing that
    // is: a fixed char buffer with a length.
    //
    // Fixed capacity rather than a length-templated FixedString<N>, and this is
    // the load-bearing decision in this file. A templated payload would make
    // Flag<7> and Flag<9> different types, and both annotations_of_with_type()
    // and extract<> need one concrete type to be handed as a template argument
    // -- so a per-length type cannot be looked up or read back in any ordinary
    // consteval loop. The capacity costs compile-time storage only; none of
    // this reaches the binary.
    //
    inline constexpr std::size_t TextCapacity = 128;

    struct Text
    {
        char        Data[ TextCapacity]{};
        std::size_t Len{};

        Text() = default;

        template<std::size_t N>
        consteval Text( const char ( & text)[ N])
        {
            static_assert( N <= TextCapacity, "cli::TextCapacity is too small for this literal");

            Len = N - 1;

            for ( std::size_t i = 0; i < Len; ++i)
                Data[ i] = text[ i];
        }

        constexpr auto view() const -> std::string_view { return { Data, Len }; }
    };

    //
    // Flag may appear more than once on one member -- annotations accumulate
    // across a declaration -- and then every spelling is accepted, the first
    // being the one --help shows. That is the whole cost of --no-color and
    // --no-colour both working: a second annotation on the same line.
    //
    struct Flag { Text Name; };

    // The --help line. Required on anything carrying a Flag: an undocumented
    // flag would silently be missing from --help, which is checked below.
    struct Doc { Text Help; };

    // The value's name in --help: NAME in --criteria=NAME. Value flags only.
    struct Meta { Text Placeholder; };

    // A bool flag that sets false rather than true, for the --no-... spellings.
    struct Clears {};

    //
    // A number flag that rejects zero. The noun is what the flag is counting,
    // and exists only so the diagnostic can read "...whole number of passes"
    // rather than the generic wording -- the operator reading it is at a bench.
    //
    struct Positive { Text Noun; };

    // -----------------------------------------------------------------------
    // Reading the annotations back
    // -----------------------------------------------------------------------

    namespace detail
    {
        //
        // unchecked() rather than access_context::current(): current() resolves
        // to *this* namespace's access rights, so a private member would simply
        // not appear here and the generated parser would be quietly missing a
        // flag. A generator wants the whole struct or a hard error.
        //
        consteval auto membersOf( std::meta::info type) -> std::vector<std::meta::info>
        {
            return std::meta::nonstatic_data_members_of( type, std::meta::access_context::unchecked());
        }

        consteval auto has( std::meta::info member, std::meta::info annotation) -> bool
        {
            return !std::meta::annotations_of_with_type( member, annotation).empty();
        }

        //
        // Every accepted spelling of one member, in declaration order.
        //
        // Returned as values rather than reflections, and everything below does
        // the same: std::meta::info is consteval-only and must not be touched at
        // runtime at all -- not even for its size -- so anything the running
        // parser needs has to be reduced to plain data first.
        //
        consteval auto spellingsOf( std::meta::info member) -> std::vector<Text>
        {
            std::vector<Text> spellings;

            for ( const auto annotation : std::meta::annotations_of_with_type( member, ^^Flag))
                spellings.push_back( std::meta::extract<Flag>( annotation).Name);

            return spellings;
        }

        consteval auto docOf( std::meta::info member) -> Text
        {
            const auto found = std::meta::annotations_of_with_type( member, ^^Doc);

            return found.empty() ? Text{} : std::meta::extract<Doc>( found[ 0]).Help;
        }

        consteval auto metaOf( std::meta::info member) -> Text
        {
            const auto found = std::meta::annotations_of_with_type( member, ^^Meta);

            return found.empty() ? Text{} : std::meta::extract<Meta>( found[ 0]).Placeholder;
        }

        consteval auto nounOf( std::meta::info member) -> Text
        {
            const auto found = std::meta::annotations_of_with_type( member, ^^Positive);

            return found.empty() ? Text{} : std::meta::extract<Positive>( found[ 0]).Noun;
        }

        //
        // How one flag reads in --help: "--quiet" for a switch, "--criteria=NAME"
        // for anything that takes a value. Derived from the member's type, which
        // is why no annotation ever has to restate it.
        //
        consteval auto displayOf( std::meta::info member) -> std::string
        {
            std::string display;

            for ( const auto & spelling : spellingsOf( member))
            {
                if ( !display.empty())
                    display += ", ";

                display += std::string( spelling.view());
            }

            if ( std::meta::type_of( member) != ^^bool)
            {
                const auto placeholder = metaOf( member);

                display += '=';
                display += placeholder.Len ? std::string( placeholder.view()) : "VALUE";
            }

            return display;
        }
    }

    // -----------------------------------------------------------------------
    // What little policing is possible
    // -----------------------------------------------------------------------

    //
    // C++ has no equivalent of Java's @Target, so nothing in the language stops
    // Clears{} landing on a std::string or Meta{} on a bool. This is the
    // substitute, and it is only half a substitute: the type half is checkable,
    // but a Clears on a bool that already defaults to false -- which silently
    // inverts the flag's sense -- is not, because a member's default initialiser
    // is not reachable through reflection. Worth knowing before trusting this.
    //
    // Note also that a failure points at the assertion rather than at the member
    // that caused it. Reflection has no way to attach a diagnostic to a source
    // location, so narrowing it down is still a reading job.
    //
    template<typename Options>
    consteval auto vocabularyIsSane() -> bool
    {
        for ( const auto member : detail::membersOf( ^^Options))
        {
            const bool isBool  = std::meta::type_of( member) == ^^bool;
            const bool isFlag  = !detail::spellingsOf( member).empty();

            if ( detail::has( member, ^^Clears) && !isBool)
                return false;

            if ( detail::has( member, ^^Positive) && isBool)
                return false;

            if ( detail::has( member, ^^Meta) && isBool)
                return false;

            // An undocumented flag would be absent from --help and so, to
            // anyone reading --help, would not exist.
            if ( isFlag && detail::docOf( member).Len == 0)
                return false;

            // A Doc on something that is not a flag documents nothing.
            if ( !isFlag && detail::docOf( member).Len != 0)
                return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // --help
    // -----------------------------------------------------------------------

    //
    // The whole body, assembled during constant evaluation and promoted to
    // static storage by define_static_string: --help prints one baked string
    // and walks no metadata at runtime. Members without a Flag are skipped, so
    // an option that exists only internally does not appear.
    //
    template<typename Options>
    consteval auto usageText() -> std::string_view
    {
        std::size_t widest = 0;

        for ( const auto member : detail::membersOf( ^^Options))
            if ( !detail::spellingsOf( member).empty())
                widest = std::max( widest, detail::displayOf( member).size());

        std::string usage;

        for ( const auto member : detail::membersOf( ^^Options))
        {
            if ( detail::spellingsOf( member).empty())
                continue;

            const auto display = detail::displayOf( member);

            usage += "  ";
            usage += display;
            usage += std::string( widest - display.size(), ' ');
            usage += "  ";
            usage += std::string( detail::docOf( member).view());
            usage += '\n';
        }

        return std::define_static_string( usage);
    }

    // -----------------------------------------------------------------------
    // Parsing
    // -----------------------------------------------------------------------

    namespace detail
    {
        inline auto splitCommaList( std::string_view csv) -> std::vector<std::string_view>
        {
            std::vector<std::string_view> parts;
            std::size_t                   start = 0;

            while ( start <= csv.size())
            {
                const auto comma = csv.find( ',', start);
                const auto end   = ( comma == std::string_view::npos) ? csv.size() : comma;

                if ( end > start)
                    parts.push_back( csv.substr( start, end - start));

                if ( comma == std::string_view::npos)
                    break;

                start = comma + 1;
            }

            return parts;
        }
    }

    //
    // Returns nullopt on a malformed or unrecognised argument, having already
    // reported it -- the same "unknown argument is a hard failure" stance the
    // hand-written parser took, kept for the same reason: a mistyped flag
    // silently ignored is a run that didn't do what was asked.
    //
    // An empty value (--log-dir=) is accepted and assigns an empty value, which
    // is what the hand-written chain did. Arguably it should be rejected; that
    // would be a behaviour change and so is not smuggled in here.
    //
    template<typename Options>
    auto parse( const int argc, char ** argv, std::ostream & errors) -> std::optional<Options>
    {
        static_assert( vocabularyIsSane<Options>(),
                       "cli: an annotation is on a member it cannot apply to, or a flag has no Doc");

        //
        // define_static_array, because the vector nonstatic_data_members_of
        // returns is allocated during constant evaluation and cannot escape it;
        // and static, because a constexpr local's address is not itself a
        // constant unless it has static storage duration.
        //
        static constexpr auto members = std::define_static_array( detail::membersOf( ^^Options));

        Options options;

        for ( int i = 1; i < argc; ++i)
        {
            const std::string_view arg     = argv[ i];
            bool                   matched = false;
            bool                   failed  = false;

            //
            // An expansion statement, not a loop: this is unrolled once per
            // member at compile time, which is what lets the body splice the
            // member (options.[: member :]) and switch on its type. The cost is
            // that the match logic is emitted per member rather than shared --
            // measurably bigger code than an if/else chain, and the reason not
            // to reach for this pattern where size matters.
            //
            template for ( constexpr auto member : members)
            {
                static constexpr auto spellings = std::define_static_array( detail::spellingsOf( member));
                static constexpr auto type      = std::meta::type_of( member);

                if ( !matched && !failed && !spellings.empty())
                {
                    for ( const auto & spelling : spellings)
                    {
                        const auto name = spelling.view();

                        if constexpr ( type == ^^bool)
                        {
                            if ( arg == name)
                            {
                                options.[: member :] = !detail::has( member, ^^Clears);
                                matched              = true;

                                break;
                            }
                        }
                        else
                        {
                            if ( arg.size() > name.size() && arg.starts_with( name) && arg[ name.size()] == '=')
                            {
                                const auto value = arg.substr( name.size() + 1);

                                matched = true;

                                if constexpr ( type == ^^std::vector<std::string_view>)
                                    options.[: member :] = detail::splitCommaList( value);

                                else if constexpr ( type == ^^std::optional<std::uint64_t>)
                                {
                                    std::uint64_t number = 0;

                                    const auto [ end, error] =
                                        std::from_chars( value.data(), value.data() + value.size(), number);

                                    //
                                    // Rejected rather than clamped: =0 (run
                                    // nothing), =-1 and =ten are all a caller
                                    // asking for something this cannot do, and
                                    // quietly reinterpreting the number would be
                                    // a run that didn't do what was asked.
                                    //
                                    const bool positive = !detail::has( member, ^^Positive) || number != 0;

                                    if ( error != std::errc{} || end != value.data() + value.size() || !positive)
                                    {
                                        static constexpr auto noun = detail::nounOf( member);

                                        errors << name << "= needs a positive whole number";

                                        if constexpr ( noun.Len != 0)
                                            errors << " of " << noun.view();

                                        errors << ", got: " << value << '\n';

                                        failed = true;
                                    }
                                    else
                                        options.[: member :] = number;
                                }

                                else if constexpr ( type == ^^std::optional<std::string_view>)
                                    options.[: member :] = value;

                                else if constexpr ( type == ^^std::optional<std::string>)
                                    options.[: member :] = std::string( value);

                                else if constexpr ( type == ^^std::string)
                                    options.[: member :] = std::string( value);

                                else
                                    static_assert( false, "cli: no parsing rule for this member's type");

                                break;
                            }
                        }
                    }
                }
            }

            if ( failed)
                return std::nullopt;

            if ( !matched)
            {
                errors << "Unknown argument: " << arg << '\n';

                return std::nullopt;
            }
        }

        return options;
    }
}
