#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace webui
{
    //
    // The flags run_scripts accepts, as it describes them itself.
    //
    // This mirrors cli::OptionInfo field for field, and that duplication is the
    // one the process boundary costs. It is worth being exact about why it is
    // acceptable here and would not be acceptable if this were a list of the
    // flags themselves:
    //
    //   - the *flags* are declared once, in main.cpp's Options struct. Nothing
    //     in this program names one. Add --foo there and it appears here.
    //   - what is mirrored is the *vocabulary* -- that a flag has spellings,
    //     help, a kind, and four booleans. A vocabulary that changed would
    //     break this program loudly at the field it lost, on the first run,
    //     not quietly.
    //
    // The second is the test a shared header would also have had to pass, and
    // it passes it. A list of flag names would not.
    //
    enum class OptionKind
    {
        Switch,
        Text,
        Number,
        List,
        Unknown   // a kind this build of the UI has never heard of -- see below
    };

    struct OptionInfo
    {
        std::vector<std::string>  Spellings;
        std::string               Help;
        std::string               Placeholder;
        std::string               Noun;
        OptionKind                Kind{ OptionKind::Text };
        bool                      Clears{ false };
        bool                      Repeatable{ false };
        bool                      Positive{ false };
        bool                      Query{ false };

        // The spelling shown and used. First wins, which is the same rule
        // --help follows.
        [[nodiscard]]
        auto flag() const -> const std::string & { return Spellings.front(); }
    };

    //
    // Parses `run_scripts --describe-options`. Nothing on a malformed document:
    // a UI that built a form out of a half-read one would offer controls that
    // do nothing, which is worse than offering none.
    //
    [[nodiscard]]
    auto parseOptionModel( std::string_view json) -> std::optional<std::vector<OptionInfo>>;

    //
    // An unrecognised "kind" is OptionKind::Unknown rather than a parse
    // failure, and the form skips those.
    //
    // This is the one piece of forward compatibility the boundary needs. A
    // newer run_scripts installed beside an older UI is an ordinary situation
    // on a bench -- suites are installed per deployment and the console is
    // not -- and the failure mode has to be "that one flag is not offered"
    // rather than "the dialog is empty". The operator can still pass it: see
    // webui::RunCommand's extra arguments.
    //
    [[nodiscard]]
    auto optionKindFrom( std::string_view name) -> OptionKind;
} // namespace webui
