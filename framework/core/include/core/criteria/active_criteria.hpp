#pragma once

#include "core/criteria/criterion.hpp"
#include "core/criteria/predicates.hpp"
#include "core/quantities/quantity.hpp"

//
// Pulls in every CRITERIA/CRIT table, for every tolerance variant, and merges
// them into the one set of group structs a script names -- so that a single
// binary carries production, stress and aged side by side and a run picks
// between them with --criteria=, instead of each variant needing its own build.
//
// This is deliberately a separate header from criterion.hpp: that file is the
// general, dependency-free CRITERIA/CRIT mechanism (used on its own by e.g.
// test_criterion.cpp, with no notion of "variants" at all), while this one is a
// specific consumer of it -- resolving THORIUM_CRITERIA_VARIANT_TABLES (and,
// through it, THORIUM_CRITERIA_MASTER_FILE) requires the scripts target's build
// configuration, so folding this into criterion.hpp would force that
// requirement onto every unrelated user of the general macros.
// (CRIT_FROM_MASTER's *definition* lives in criterion.hpp despite assuming
// the same convention -- see the comment there for why that one's different:
// it's opt-in and inert until actually used.)
//
// That path is a compile definition set by CMake from a single, validated list
// (THORIUM_KNOWN_CRITERIA_VARIANTS plus THORIUM_CRITERIA_MASTER -- see the
// top-level CMakeLists.txt, cmake/CriteriaVariants.cmake and suite/README.md),
// not raw, untyped environment variables read via #ifdef. The second path this
// header needs, the master table's, is a #define inside that generated file
// rather than a definition of its own, so which variant is the master is
// decided in exactly one place.
//
// Every .inc file itself has no scaffolding of its own (no #pragma once, no
// #includes, no namespace, no "core::"/"using namespace" of its own) -- this
// header and the generated tables file supply all of that, including bringing
// core::quantities' predicates (EQ, MASK, ...), quantity types (Voltage, ...),
// and core::literals' literals (_V, ...) into unqualified scope, so a criteria
// file can write EQ( 5.0_V).epsilon( 0.05_V) instead of
// core::quantities::EQ( core::quantities::Voltage{ 5.0}).epsilon( core::quantities::Voltage{ 0.05}).
//
// Deliberately core::quantities, not a blanket "using namespace core;": core
// also holds Instrument, Verify, SwitchMatrix, and everything else in
// the framework, none of which a criteria file has any business touching.
// core::quantities holds only the predicate/quantity vocabulary (see
// quantity.hpp and predicates.hpp, which define it and reopen it respectively)
// -- anything new added there is automatically part of what a criteria file can
// use, with nothing else dragged in alongside it.
//
// Each .inc file is one flat file holding every script's CRITERIA/CRIT side by
// side.
//

//
// ---------------------------------------------------------------------------
// Pass 1: every variant, each in its own namespace
// ---------------------------------------------------------------------------
//
// The generated file (cmake/CriteriaVariants.cmake) is a namespace per known
// variant -- thorium::criteria::production, ::stress, ::aged -- each #including
// its own dut/criteria_<name>.inc, plus a `master` alias for whichever of them
// THORIUM_CRITERIA_MASTER names, the master table's own path, and the X-macro
// pass 2 iterates below.
//
// Nested inside thorium::criteria rather than sitting at global scope, so this
// does not declare FS_Supply_1 three times over; only the merged table below
// lands at global scope, which is what scripts actually name. The namespaces
// being *siblings* is also what makes CRIT_FROM_MASTER work from inside any of
// them: its deliberately unqualified `master::group::id` (see
// core/criteria/criterion.hpp) resolves by walking out to thorium::criteria and back in.
//
// suite/tests/test_criteria_variants_compile.cpp used to be the only place all
// variants existed side by side, purely so a typo in a variant nobody was
// building against would still be caught. That arrangement is now how the
// framework really works, rather than a test-only mirror of it -- see that
// file's own comment for what it still checks that this cannot.
//
#include THORIUM_CRITERIA_VARIANT_TABLES

//
// ---------------------------------------------------------------------------
// Pass 2: merge them into the table a script names
// ---------------------------------------------------------------------------
//
// The master table is re-read a second time with CRITERIA/CRIT redefined, so
// that each CRIT emits not one Criterion but a MultiCriterion holding every
// variant's same-named criterion (see core/criteria/criterion.hpp). It is the same
// redefine-the-macro-and-re-#include trick CRIT_FROM_MASTER and
// hal::safeRig() use, and it is doing something reflection cannot: building a
// *new struct type* whose members are named by another file.
//
// Reading the master's table for the shape is not an arbitrary choice of
// reference -- it is already the baseline every other variant borrows from via
// CRIT_FROM_MASTER, and it is the one table that cannot itself use that macro,
// so it is the one guaranteed to spell out every group and every id in full.
// Which variant that is comes from THORIUM_CRITERIA_MASTER; nothing below
// assumes it is production.
//
// What this costs in compile-time checking: nothing. What it adds:
//
//   - a script's FS_Supply_1::FS_Supply_5V0 is still a static constexpr member
//     of a real struct, so a typo is still "no such member" -- unchanged;
//   - a CRIT that the master declares and another variant does not is now a
//     compile error naming both the id and the variant missing it, where before
//     it needed suite/tests/test_criteria_variants_compile.cpp to catch it;
//   - every variant's predicates are now type-checked against the reading each
//     script actually takes. A Current criterion in dut/criteria_aged.inc fails
//     the build today rather than the day someone first targets aged hardware
//     (see the diagnostics in core/criteria/verify.hpp).
//
// What it does not catch, and what that test file is therefore still for: a
// group or CRIT that exists ONLY in a non-master variant. Nothing here reads
// those tables looking for surplus, so a stray id in dut/criteria_stress.inc is
// invisible to this pass -- it is simply never merged, and never runs.
//
#undef CRITERIA
#undef CRIT
#undef END_CRITERIA
#undef CRIT_FROM_MASTER

//
// One `using ThoriumVariantN = <namespace>::<group>;` per known variant, inside
// each group struct. These exist so that CRIT below can reach each variant's
// matching group without being handed the group's name -- a macro cannot define
// another macro, so CRITERIA has no way to leave "the group I am currently
// opening" somewhere for CRIT to find. Member aliases are the way round it:
// they are scoped to exactly the struct being built, so nested or repeated
// groups cannot confuse them the way a file-scope alias could.
//
#define THORIUM_GROUP_ALIAS( idx, ns, groupName)  using ThoriumVariant##idx = ns::groupName;

//
// The leading comma is deliberate and belongs to the expansion, not to the call
// site: this pastes one extra argument per variant onto the end of a
// makeMultiCriterion( Name, #id ...) call, so the argument list has to grow
// with a separator each time and cannot know whether it is the first.
//
#define THORIUM_CRIT_REF( idx, ns, critId)      , ThoriumVariant##idx::critId

#define CRITERIA( groupName, desc)                                     \
    struct groupName                                                   \
    {                                                                  \
        static constexpr std::string_view Name        = #groupName;    \
        static constexpr std::string_view Description = desc;          \
        THORIUM_FOR_EACH_CRITERIA_VARIANT( THORIUM_GROUP_ALIAS, groupName)

//
// pred and desc are accepted and dropped. Both still live in the per-variant
// tables read in pass 1 -- this pass is building the *index* over those, and
// taking either from here would mean the merged table quietly imposing the
// master variant's tolerance or prose on every other variant's criterion.
// That is the exact failure this whole mechanism exists to make impossible.
//
#define CRIT( id, pred, desc)                                                 \
        static constexpr auto id = ::core::makeMultiCriterion( Name, #id      \
            THORIUM_FOR_EACH_CRITERIA_VARIANT( THORIUM_CRIT_REF, id) );

//
// Defined identically to CRIT rather than left undefined. The master table
// cannot use it (the master is what CRIT_FROM_MASTER borrows *from*), so this
// never fires -- but a macro left undefined in a pass that re-reads a
// hand-written file would turn a mistake in that file into a confusing
// preprocessor error, and the merge does not care where a criterion's value
// came from in any case.
//
#define CRIT_FROM_MASTER( groupName, id) CRIT( id, , )

#define END_CRITERIA };

//
// The two using-directives are what a script including this header has always
// got, and they stay: pass 2 itself needs neither (it never expands a
// predicate), but a script writing an ad-hoc EQ( 12.0_V) check does -- see
// suite/scripts/fuse_register_script.cpp -- and quietly withdrawing vocabulary
// that scripts already build against is not part of this mechanism's job.
//
// Unlike pass 1's, these are not confined to a namespace: the merged CRITERIA
// structs (FS_Supply_1, FS_Fuse_6, ...) have to land unqualified, at whatever
// scope includes this header, so scripts can keep writing
// FS_Supply_1::FS_Supply_5V0 directly. They therefore remain in effect for the
// rest of whatever script.cpp includes this header, the same as if that script
// had written `using namespace core::literals;` itself (which
// fuse_register_script.cpp already does, explicitly, today).
//
using namespace core::quantities;
using namespace core::literals;

#include THORIUM_CRITERIA_MASTER_FILE
