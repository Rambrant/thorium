#pragma once

#include <cstddef>
#include <span>
#include <string_view>

//
// Which tolerance variant a run applies, chosen at runtime.
//
// Every variant in THORIUM_KNOWN_CRITERIA_VARIANTS is compiled into the binary
// (see core/criteria/active_criteria.hpp for how, and suite/README.md for why), so the
// choice between them is no longer a build-time one. This is the seam that
// makes it: run_scripts' --criteria= writes here, and core::Verify reads here
// once per check to pick which of a MultiCriterion's tolerances to apply.
//
// Deliberately a separate header from core/criteria/active_criteria.hpp, and it pulls in
// none of the criteria machinery -- no predicates, no quantities, no tables.
// The two have genuinely different audiences: active_criteria.hpp is included
// by every script translation unit and defines what the tolerances *are*, while
// this is included by main.cpp and core/src/journal/journal.cpp, neither of which has
// any business seeing a criteria table, and both of which only need to know
// which variant's name to put on a log.
//
// A process-wide setting rather than a parameter, for the same reason
// core::journal() and the Measure/Apply verbs are: a catalog test script takes
// no parameters at all (see core/catalog/test_catalog.hpp on why), so there is no route
// by which a per-run choice could be threaded down to the Verify calls inside
// one -- and those are precisely the call sites that need it.
//
namespace core
{
    //
    // Every variant this binary was built with, in THORIUM_KNOWN_CRITERIA_
    // VARIANTS order -- the same order the tables were generated in, so an
    // index means the same thing here and in a MultiCriterion's tuple.
    //
    // Exposed rather than kept private because a caller rejecting an unknown
    // --criteria= has to be able to say what the legal answers were, and
    // because it is what a discovering server reads out of manifest.json (see
    // cmake/GenerateManifest.cmake) to offer them.
    //
    [[nodiscard]]
    auto criteriaVariantNames() -> std::span<const std::string_view>;

    //
    // The variant a run uses when --criteria= is not given, baked in from
    // THORIUM_CRITERIA_VARIANT at build time. A deployment configured for a
    // production line still runs production tolerances when an operator forgets
    // the flag, rather than whichever variant happens to be listed first.
    //
    [[nodiscard]]
    auto defaultCriteriaVariantName() -> std::string_view;

    //
    // Which variant is this build's *master*: the table the others borrow their
    // unchanged criteria from via CRIT_FROM_MASTER, baked in from
    // THORIUM_CRITERIA_MASTER at build time (see cmake/CriteriaVariants.cmake).
    //
    // Not selectable, and here for exactly that reason. The variants are not
    // independent tables -- a run reporting "stress" applied stress's own
    // tolerances for the rows stress changes and the master's for every row it
    // does not, so "which tolerances were these" is only half answered without
    // it. Both log streams carry it in their traceability header alongside the
    // applied variant (see core::RunInfo in core/journal/journal.hpp), and
    // manifest.json reports it for the same reason.
    //
    // A name and no index: nothing selects by it, and unlike the default it is
    // never resolved against the list here -- the build already rejected a
    // master that is not one of the known variants, at configure time.
    //
    [[nodiscard]]
    auto masterCriteriaVariantName() -> std::string_view;

    //
    // The active variant, as an index into criteriaVariantNames() and into
    // every MultiCriterion's tuple of tolerances. Read once per Verify.
    //
    [[nodiscard]]
    auto activeCriteriaVariant() -> std::size_t;

    [[nodiscard]]
    auto activeCriteriaVariantName() -> std::string_view;

    //
    // Selects by name. False -- with nothing changed -- if the name is not one
    // of criteriaVariantNames(), which is a caller error a runner is expected
    // to report and refuse to run on, the same stance it takes on an unknown
    // flag.
    //
    // Throws std::logic_error if the run has already started (see
    // lockCriteriaVariant below). That is not a caller error but a programming
    // one: the CLI is parsed long before the journal opens, so nothing legitimate
    // reaches this afterwards.
    //
    auto selectCriteriaVariant( std::string_view name) -> bool;

    //
    // Freezes the selection. Called by Journal::begin(), i.e. the moment a run's
    // traceability header -- which names the variant -- is committed to both
    // logs.
    //
    // Without this, a variant changed halfway through a run would produce a log
    // whose header says one thing and whose checks did another, with nothing in
    // the file admitting it. That is the quietly-wrong artifact this framework
    // keeps refusing to hand back (see --record=/--replay= being exclusive, and
    // the git --dirty content revision), and the same answer applies: make it
    // impossible rather than document it.
    //
    auto lockCriteriaVariant() -> void;

    //
    // Unlocks and restores the default. For tests only -- a process running one
    // real test run has no use for it, which is why it is not part of the story
    // the header comment above tells.
    //
    auto resetCriteriaVariantForTesting() -> void;
} // namespace core
