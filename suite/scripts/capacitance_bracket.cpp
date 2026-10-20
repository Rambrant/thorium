#include "../prelude.hpp"

//
// The Capacitance group's SETUP/TEARDOWN pair: remember how DcP7 was programmed
// before the group's tests run, and put it back afterwards.
//
// The sibling of suite/scripts/transient_bracket.cpp. What the pair does and why
// -- the teardown running whether or not the test did, the values put back being
// the ones found rather than a second copy of rigPowerOn's literals, nothing
// restored unless something took it away -- is hal::SourceSnapshot's, in
// hal/verbs/snapshot.hpp, and is the same for every source. What is left here is
// the one fact that is this suite's: which source this group disturbs.
//
// Only DcP7 is touched. The battery rail is the one this group's test takes
// down, and a hook that saved and restored every rail on the rig would be doing
// rigPowerOn's job from inside a group, on every pass, for no reason. Taking
// the rail down is not here either: that is the script's own precondition, and
// belongs next to the reading that needs it.
//

namespace
{
    //
    // The one thing the pair shares, and the reason both hooks are in one file:
    // file-local, so the only two functions that can name it are the two whose
    // invariant it is.
    //
    hal::SourceSnapshot<std::remove_cvref_t<decltype( DcP7)>> sDcP7{};
} // namespace

//
// The Capacitance group's SETUP. Reads, and does not judge: it returns true
// unconditionally, and that is a decision rather than an omission.
//
// A setup returning false skips its own group's tests (see
// framework/runner/src/main.cpp), so a hook here that insisted on finding a live
// battery rail would skip BulkCapacitance in exactly the three modes where no
// Apply ever reaches a driver -- --replay, --inject and --skeleton all detach
// the bench (core/session/bench.hpp). Those runs take their readings from a file
// and are meant to reach every test the live run reaches. Whether the rig
// actually came up is already asked, once, by RUN_SETUP, where a false stops
// everything rather than one group.
//
auto capacitanceSetup() -> bool
{
    sDcP7.capture( DcP7);

    return true;
}

//
// The Capacitance group's TEARDOWN: DcP7 back to what the setup found, if and
// only if something took it away.
//
auto capacitanceTeardown() -> bool
{
    sDcP7.restore( DcP7);

    return true;
}
