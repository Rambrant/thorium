#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{
    //
    // One installed suite: the run_scripts binary, the catalog it holds, and
    // the tolerance variants it was built with.
    //
    // Assembled from the manifest.json that cmake/GenerateManifest.cmake writes
    // beside every installed binary -- a file whose comment says it exists so
    // that a supervising process can offer "which suite, which tests, which
    // criteria variant" without parsing C++ or knowing anything about the build.
    // This is that process. Nothing here was invented for the UI; the manifest
    // predates it.
    //
    struct TestEntry
    {
        std::string  Group;
        std::string  Id;
        std::string  Description;
    };

    struct Suite
    {
        std::filesystem::path     Binary;          // the run_scripts beside the manifest
        std::filesystem::path     Manifest;

        std::vector<TestEntry>    Tests;
        std::vector<std::string>  CriteriaVariants;
        std::string               DefaultCriteria;

        //
        // Which variant the others inherit their unchanged rows from. Not
        // selectable, and carried for exactly that reason -- see
        // GenerateManifest.cmake: "these tolerances came from the stress table"
        // is half an answer if the table the unchanged rows came from is
        // invisible. The window shows it beside the picker and greys it.
        //
        std::string               MasterCriteria;

        //
        // The prefix this suite was discovered under, which label() needs and
        // nothing else does. Not in the manifest -- it is a property of where
        // somebody looked, not of what was installed.
        //
        std::filesystem::path     Root;

        //
        // What the picker shows.
        //
        // Not in the manifest, and deliberately not added to it: a display name
        // would have to come from THORIUM_DUT_NAME/THORIUM_RIG_NAME, which are
        // defaulted inside framework/core's CMakeLists and so are not in scope
        // where the manifest is generated (framework/core and framework/runner
        // are sibling add_subdirectory calls). Moving them up to reach it would
        // be relocating a decision framework/core documents at length, to put a
        // nicer word in a dropdown.
        //
        // So it is derived from where the manifest was found, relative to the
        // prefix that was scanned -- with a trailing "bin" dropped, because an
        // install puts every suite in <prefix>/bin and a picker whose every
        // entry reads "bin" answers the one question it exists to answer with
        // the same word every time.
        //
        [[nodiscard]]
        auto label() const -> std::string;
    };

    //
    // Every manifest.json under `prefix`, each turned into a Suite.
    //
    // A directory scan rather than a registry, which is what
    // GenerateManifest.cmake's "a discovering web server" implies and the only
    // thing that works for a bench where suites are installed independently and
    // nothing keeps a list. Malformed or unreadable manifests are skipped
    // rather than fatal: one bad install must not make the other suites
    // unreachable.
    //
    [[nodiscard]]
    auto discoverSuites( const std::filesystem::path & prefix) -> std::vector<Suite>;

    //
    // One manifest's text, parsed. Exposed separately from the scan above so it
    // can be tested without a filesystem.
    //
    // The catalog in the manifest is a snapshot taken at *install* time. It is
    // what the picker is populated from, and the window re-reads the live
    // catalog with --list-tests before a run -- see loadCatalog below. Both,
    // rather than one: the manifest is there when the binary is not yet
    // runnable (a half-mounted network prefix, a suite for another rig), and
    // the binary is the only thing that cannot be stale.
    //
    [[nodiscard]]
    auto parseManifest( std::string_view json,
                        const std::filesystem::path & manifestPath,
                        const std::filesystem::path & root = {}) -> std::optional<Suite>;

    //
    // `run_scripts --list-tests` output -- "group|id|description" per line.
    //
    // Split on the first two pipes only, never every pipe: a description
    // containing one would otherwise be truncated at the wrong point. The same
    // rule GenerateManifest.cmake applies to the same text, and the same reason.
    //
    [[nodiscard]]
    auto parseTestList( std::string_view text) -> std::vector<TestEntry>;
} // namespace ui
