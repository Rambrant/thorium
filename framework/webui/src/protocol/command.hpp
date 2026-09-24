#pragma once

#include <string>
#include <vector>

#include "protocol/options.hpp"
#include "protocol/suite.hpp"

namespace webui
{
    //
    // What the operator has set, in the terms the form collects it.
    //
    // One entry per control the operator has *touched*. A control left alone
    // produces nothing, and that is the design rather than an optimisation --
    // see framework/webui/README.md's "Generating a form from --describe-options"
    // and cli::optionsModel's comment: main.cpp depends in
    // two places on telling "the caller said nothing" from "the caller named
    // the default", and a form that emitted every default would erase the
    // distinction.
    //
    struct OptionSetting
    {
        std::string  Flag;     // the canonical spelling, from OptionInfo::flag()
        std::string  Value;    // empty for a switch

        //
        // For a switch: whether the flag is passed at all. Not "the value is
        // true" -- a Clears flag like --no-logs is passed to turn something
        // off, so what the checkbox holds and what the command line needs are
        // inverses of each other. The form does that inversion once, here, so
        // nothing downstream has to know which flags are clearing ones.
        //
        bool         Present{ true };
    };

    struct RunRequest
    {
        //
        // Empty means every test, exactly as an absent --select does. Not the
        // full list: "run everything" and "run these nine, which happen to be
        // all of them" are the same run today and would stop being the same run
        // the moment a test is added to the catalog, which is precisely when an
        // operator would not notice.
        //
        std::vector<std::string>    Selection;

        std::vector<OptionSetting>  Settings;

        //
        // Anything the operator typed by hand. The escape hatch for a flag this
        // build of the UI did not recognise (OptionKind::Unknown) and for one
        // added to run_scripts after it shipped -- appended last, so it beats
        // nothing and is beaten by nothing, and passed through untouched.
        //
        std::vector<std::string>    Extra;
    };

    //
    // The argv for a run, including the binary at [0].
    //
    // --quiet, --no-color and --events=- are added here and are not the
    // operator's to change:
    //
    //   --events=-   is the whole reason this program can see anything
    //   --quiet      is required alongside it -- run_scripts refuses the pair
    //                otherwise, because JSON and coloured prose on one stdout
    //                is neither (see parseOptions in main.cpp)
    //   --no-color   because nothing renders ANSI here
    //
    // Deliberately absent: --no-logs. A run started from this window leaves the
    // same RTF and SARIF a terminal run does, because a run's record must not
    // depend on how it was launched. An operator who wants none can still tick
    // the box; that is their decision, made explicitly.
    //
    [[nodiscard]]
    auto buildRunCommand( const Suite & suite, const RunRequest & request) -> std::vector<std::string>;

    //
    // The argv for dropping the rig -- the same binary, with --safe.
    //
    // The same binary matters and is why this is one line rather than a
    // separate tool: it was built against this rig's exact instrument list, so
    // it cannot disagree with the run it is cleaning up after (see main.cpp's
    // note on why --safe lives in the runner at all).
    //
    [[nodiscard]]
    auto buildSafeCommand( const Suite & suite) -> std::vector<std::string>;

    //
    // The argv for the two questions the UI asks a binary about itself.
    //
    [[nodiscard]] auto buildListTestsCommand( const Suite & suite) -> std::vector<std::string>;
    [[nodiscard]] auto buildDescribeCommand( const Suite & suite) -> std::vector<std::string>;
} // namespace webui
