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
// whole vocabulary is the annotation types in the "Vocabulary" section. It
// lives in framework/runner/src/ rather than in an include/ directory of its own,
// let alone framework/core/include/, because main.cpp is its only consumer and a
// general-purpose CLI framework is not something this framework should be
// committing to as installed public surface. Being under framework/ now does not
// change that: what framework/runner installs is the run_scripts binary, not a header
// anyone compiles against. If a second consumer ever appears, that is the moment
// to move it, not before.
//
// The UI (framework/console/, see its README) is pointedly *not* that second consumer, and the
// option model below is what keeps it from becoming one. It needs to know what
// the flags are, which is exactly what would have justified promoting this to a
// public header -- so instead the model is emitted as JSON by
// --describe-options and the UI reads that, in another process, built by
// another compiler. The UI cannot include this file and does not want to. That
// is the whole argument for the out-of-process boundary in one sentence: the
// declaration stays here, and what crosses is data.
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

#include "core/journal/json.hpp"

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
    // A list flag that accumulates one value per occurrence, instead of
    // taking one comma-separated list.
    //
    // Which of the two a list flag wants is decided by whether a comma can
    // appear *inside* a value. --select takes test ids, which cannot contain
    // one, so "--select=A,B" is the natural spelling and repeating the flag
    // would be a worse one. --address takes a bus address, and a GPIB address
    // is written "gpib:0,5" -- comma-splitting that would hand the parser two
    // fragments of one address and no way to tell that had happened.
    //
    // So this is not a convenience: it is the annotation that lets a value
    // contain the character the other spelling reserves.
    //
    struct Repeatable {};

    //
    // A number flag that rejects zero. The noun is what the flag is counting,
    // and exists only so the diagnostic can read "...whole number of passes"
    // rather than the generic wording -- the operator reading it is at a bench.
    //
    struct Positive { Text Noun; };

    //
    // A flag that makes the program describe itself and exit, rather than doing
    // anything the caller asked for -- --help, --list-tests, --describe-options.
    //
    // It exists for the option model below, and therefore for the UI. A
    // generated form must not offer these: they are how the form was built in
    // the first place, and a "print this list and exit" checkbox on a run
    // dialog is nonsense. Without the annotation the UI would carry a hardcoded
    // list of three names to skip -- a second declaration of what the flags
    // are, in another process, which is the exact failure this whole file
    // exists to prevent.
    //
    // Note what it is NOT: a general "hide this". --safe also runs no test, and
    // is deliberately not a Query, because a UI very much wants a button for
    // it. The distinction is whether the flag answers a question *about the
    // binary* or does something *to the rig*.
    //
    // Query flags still appear in --help. A person reading it is the one
    // audience these are for.
    //
    struct Query {};

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

            // A Query flag exits after printing. Anything that takes a value
            // would be a mode with an argument, which none of them is.
            if ( detail::has( member, ^^Query) && !isBool)
                return false;

            // A Query on something with no flag at all describes nothing.
            if ( detail::has( member, ^^Query) && !isFlag)
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
    // The same annotations, as data a program can read
    // -----------------------------------------------------------------------

    //
    // usageText() above renders the flags for a person. This renders them for a
    // program -- the UI, which builds its run dialog out of them and so needs
    // the facts behind the --help line rather than the line.
    //
    // Same annotations, same members, one declaration. A form with a checkbox
    // per flag, hand-written in another process, would be the three-places
    // problem this file's opening comment describes, reopened across a process
    // boundary where no compiler can see both halves at once. Here a flag added
    // to Options appears in --help, in the parser, and in the UI's form, and
    // the third of those needs no edit anywhere.
    //
    // What it deliberately cannot carry is a default. A member's default
    // initialiser is not reachable through reflection (see vocabularyIsSane's
    // comment, which runs into the same wall), and the honest consequence is
    // better than a workaround would have been: the UI starts every control
    // unset and emits only the flags an operator actually touched. That
    // preserves the distinction main.cpp depends on in at least two places --
    // "the caller said nothing" is not "the caller named the default" (see
    // Options::CriteriaVariant and Options::Repeat). A UI that helpfully filled
    // in defaults and always passed them would erase it.
    //
    // Clears is the one case where a default is knowable, and it is knowable
    // from the annotation rather than the initialiser: --no-logs exists to turn
    // something off, so the thing is on. That is enough for the UI to start
    // those boxes checked.
    //

    enum class Kind
    {
        Switch,   // bool -- a checkbox
        Text,     // a string of some flavour -- a field
        Number,   // std::optional<std::uint64_t> -- a spinner
        List      // std::vector<std::string_view> -- comma-separated, or repeated
    };

    [[nodiscard]]
    constexpr auto to_string( const Kind kind) -> std::string_view
    {
        switch ( kind)
        {
            case Kind::Switch: return "switch";
            case Kind::Text:   return "text";
            case Kind::Number: return "number";
            case Kind::List:   return "list";
        }

        return "text";
    }

    //
    // One flag, reduced to plain runtime data. std::string rather than
    // std::string_view even though every one of these points into static
    // storage: this is the type that crosses into a consumer that may well
    // build it from parsed JSON rather than from reflection, and a model whose
    // provenance changes its lifetime rules is a trap.
    //
    struct OptionInfo
    {
        std::vector<std::string>  Spellings;    // first is the canonical one
        std::string               Help;
        std::string               Placeholder;  // NAME in --criteria=NAME; empty for a switch
        std::string               Noun;         // Positive's noun, for a number's diagnostic
        Kind                      ValueKind{ Kind::Text };
        bool                      Clears{ false };
        bool                      Repeatable{ false };
        bool                      Positive{ false };
        bool                      Query{ false };
    };

    namespace detail
    {
        consteval auto kindOf( std::meta::info member) -> Kind
        {
            const auto type = std::meta::type_of( member);

            if ( type == ^^bool)
                return Kind::Switch;

            if ( type == ^^std::optional<std::uint64_t>)
                return Kind::Number;

            if ( type == ^^std::vector<std::string_view>)
                return Kind::List;

            //
            // Everything else is a string of some flavour, which is what the
            // parser does with it too. Not an exhaustive match on purpose: the
            // parser's own static_assert( false, "cli: no parsing rule for this
            // member's type") is where an unhandled type has to fail, because
            // that is the one that makes the flag unusable. A model that also
            // refused would just be a second, earlier error about the same
            // typo.
            //
            return Kind::Text;
        }
    }

    //
    // Members without a Flag are skipped, exactly as usageText() skips them: an
    // option that exists only internally is not part of the interface either
    // way.
    //
    template<typename Options>
    [[nodiscard]]
    auto optionsModel() -> std::vector<OptionInfo>
    {
        static_assert( vocabularyIsSane<Options>(),
                       "cli: an annotation is on a member it cannot apply to, or a flag has no Doc");

        static constexpr auto members = std::define_static_array( detail::membersOf( ^^Options));

        std::vector<OptionInfo> model;

        //
        // An expansion statement for the reason parse() uses one: the body has
        // to splice each member to ask its type. The cost parse() pays -- the
        // match logic emitted once per member -- is not paid here, because this
        // body does not branch on the type at all; kindOf() answers during
        // constant evaluation and what is left is a push_back.
        //
        template for ( constexpr auto member : members)
        {
            static constexpr auto spellings = std::define_static_array( detail::spellingsOf( member));

            if constexpr ( !spellings.empty())
            {
                OptionInfo info;

                for ( const auto & spelling : spellings)
                    info.Spellings.emplace_back( spelling.view());

                info.Help        = std::string( detail::docOf( member).view());
                info.Placeholder = std::string( detail::metaOf( member).view());
                info.Noun        = std::string( detail::nounOf( member).view());
                info.ValueKind   = detail::kindOf( member);
                info.Clears      = detail::has( member, ^^Clears);
                info.Repeatable  = detail::has( member, ^^Repeatable);
                info.Positive    = detail::has( member, ^^Positive);
                info.Query       = detail::has( member, ^^Query);

                model.push_back( std::move( info));
            }
        }

        return model;
    }

    //
    // The model as JSON, which is the form it actually travels in.
    //
    // Here rather than in main.cpp because it is generic -- it knows the
    // vocabulary and nothing about run_scripts, the same test every other
    // function in this file passes. The one outward dependency is
    // core::jsonQuoted, and it is the right one: a second escaper written here
    // would be a second answer to a question core/journal/json.hpp already
    // answers for both JSON sinks.
    //
    // Pretty-printed, one flag per object over several lines. It is read by a
    // program, but it is also the thing a person diffs when the UI offers a
    // control they did not expect, and a single-line document cannot be diffed.
    //
    inline auto writeOptionsJson( const std::vector<OptionInfo> & model, std::ostream & out) -> void
    {
        out << "{\n  \"options\": [";

        bool firstOption = true;

        for ( const auto & option : model)
        {
            out << ( firstOption ? "\n" : ",\n") << "    {\n";

            firstOption = false;

            out << "      \"flags\": [";

            bool firstSpelling = true;

            for ( const auto & spelling : option.Spellings)
            {
                out << ( firstSpelling ? "" : ", ") << core::jsonQuoted( spelling);

                firstSpelling = false;
            }

            out << "],\n";
            out << "      \"kind\": "  << core::jsonQuoted( to_string( option.ValueKind)) << ",\n";
            out << "      \"help\": "  << core::jsonQuoted( option.Help) << ",\n";

            //
            // The four booleans are written whether or not they are set, unlike
            // the two strings below them. A consumer branches on every one of
            // them -- a cleared switch starts checked, a repeatable list is
            // emitted as N flags rather than one comma-separated value -- and
            // "absent" and "false" being the same thing is fine for a field
            // that is always consulted. A placeholder or a noun that is not
            // there is genuinely not there.
            //
            out << "      \"clears\": "     << ( option.Clears     ? "true" : "false") << ",\n";
            out << "      \"repeatable\": " << ( option.Repeatable ? "true" : "false") << ",\n";
            out << "      \"positive\": "   << ( option.Positive   ? "true" : "false") << ",\n";
            out << "      \"query\": "      << ( option.Query      ? "true" : "false");

            if ( !option.Placeholder.empty())
                out << ",\n      \"placeholder\": " << core::jsonQuoted( option.Placeholder);

            if ( !option.Noun.empty())
                out << ",\n      \"noun\": " << core::jsonQuoted( option.Noun);

            out << "\n    }";
        }

        out << "\n  ]\n}\n";
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
                                {
                                    //
                                    // Append or replace -- see cli::Repeatable
                                    // on why the choice belongs to the flag
                                    // rather than being one rule for all lists.
                                    //
                                    if constexpr ( detail::has( member, ^^Repeatable))
                                        options.[: member :].push_back( value);

                                    else
                                        options.[: member :] = detail::splitCommaList( value);
                                }

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
