#include "../prelude.hpp"

//
// The Transient group's SETUP/TEARDOWN pair: remember how AcP1 was programmed
// before the group's tests run, and put it back afterwards.
//
// Both hooks live in this one file, and that is the mechanism rather than a
// filing convention. What they share is an ordinary translation-unit-local
// object -- see sAcP1 below -- so the pair has to be one translation unit for
// there to be anything to share. rigPowerOn/rigPowerOff are split across two
// files precisely because they share nothing: each is written against the rig's
// stated setpoints, not against what the other one found.
//
// Why a hook pair rather than the script putting the source back itself, which
// is what acDropoutScript used to do: the teardown runs whether or not the test
// did, and the values put back are the ones found rather than a second copy of
// rigPowerOn's 115 V / 400 Hz / 2 A. Both of those, and the rest of what makes
// a restore safe, are hal::SourceSnapshot's -- see hal/verbs/snapshot.hpp. What
// is left here is the one fact that is this suite's: which source this group
// disturbs.
//
// What is deliberately NOT here is the drop itself. Removing the AC input is
// this test's stimulus -- the event the capture is armed for -- so it belongs
// in the script that measures its effect, not in a bracket that runs whether or
// not anything was measured.
//

namespace
{
    //
    // The one thing the pair shares. File-local, so the only two functions that
    // can name it are the two whose invariant it is.
    //
    hal::SourceSnapshot<std::remove_cvref_t<decltype( AcP1)>> sAcP1{};
} // namespace

//
// The Transient group's SETUP. Reads, and does not judge: it returns true
// unconditionally, and that is a decision rather than an omission.
//
// A setup returning false skips its own group's tests (see framework/runner/src/main.cpp),
// so a hook here that insisted on finding a live AC input would skip AcDropout
// in exactly the three modes where no Apply ever reaches a driver -- --replay,
// --inject and --skeleton all detach the bench (core/session/bench.hpp). Those runs
// take their readings from a file and are meant to reach every test the live
// run reaches. Whether the rig actually came up is already asked, once, by
// RUN_SETUP, where a false stops everything rather than one group.
//
auto transientSetup() -> bool
{
    sAcP1.capture( AcP1);

    return true;
}

//
// The Transient group's TEARDOWN: AcP1 back to what the setup found, if and
// only if something took it away.
//
auto transientTeardown() -> bool
{
    sAcP1.restore( AcP1);

    return true;
}
