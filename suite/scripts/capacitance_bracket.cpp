#include "../prelude.hpp"

#include <optional>

//
// The Capacitance group's SETUP/TEARDOWN pair: remember how DcP3 was programmed
// before the group's tests run, and put it back afterwards.
//
// The sibling of suite/scripts/transient_bracket.cpp, and read the same way --
// see that file for the argument in full. The short version, because it is the
// reason this file exists rather than a trailing Connect/Apply in the script:
//
//   - The teardown runs whether or not the test did. framework/runner/src/main.cpp
//     constructs the group's TeardownGuard *before* the group's setup, so a
//     restore written here happens on the paths where the script returned
//     early, failed, or never started.
//
//   - The values put back are the ones actually found, not a second copy of
//     rigPowerOn's literals. 24 V and 4 A are written once, there, and this
//     hook never learns them.
//
// What is deliberately NOT here is taking the rail down. That is the script's
// own precondition -- the thing it has to do before it can measure at all --
// so it belongs in the script, next to the reading that needs it, and not in a
// bracket that runs whether or not anything was measured. Same split
// transient_bracket.cpp draws around the AC drop.
//
// Two supplies, one bracket, is *not* what this is: only DcP3 is touched. The
// battery rail is the one this group's test disturbs, and a hook that saved and
// restored every rail on the rig would be doing rigPowerOn's job from inside a
// group, on every pass, for no reason.
//

namespace
{
    //
    // What DcP3 was programmed to when this group's setup ran, and whether it
    // was delivering it. Setpoint readbacks (VOLTage?, CURRent?) rather than
    // measurements: DcP3.measuredVoltage() answers what the source says it is
    // producing, which is the right question for a check and the wrong one for
    // a restore. Re-applying a measured 23.98 V would program the rig to a
    // value nobody asked for, and would do it again, a little further off, on
    // every pass.
    //
    struct DcSourceState
    {
        //
        // Whether the setup got as far as filling the rest in. The guard that
        // will call the teardown is constructed before the setup runs, so
        // "teardown reached with nothing recorded" is a state that has to have
        // an answer, and the answer is "do nothing".
        //
        bool Recorded { false };

        //
        // Whether the source was actually delivering. A group whose setup found
        // the battery rail already down -- a detached bench, where no Apply ever
        // reaches a driver (see core/session/bench.hpp) -- must not have its
        // teardown energise one.
        //
        bool Energised{ false };

        core::quantities::Voltage                 Voltage{};

        //
        // Optional, because "never programmed" is a state the instrument
        // distinguishes and the restore has to preserve: std::nullopt means
        // leave that setting alone, which is what it means everywhere else in
        // hal::keysight_n6701a::DcConfig too. Voltage above is not optional,
        // because the driver's own readback is not -- an output voltage is
        // always some value, where a current limit may genuinely never have
        // been set.
        //
        std::optional<core::quantities::Current>  CurrentLimit{};
    };

    //
    // The one thing the pair shares, and the reason both hooks are in one file:
    // file-local, so the only two functions that can name it are the two whose
    // invariant it is. No test script in this suite can reach it, and no other
    // bracket can collide with it -- transient_bracket.cpp's sAcP1 is a
    // different object in a different translation unit with the same lifetime
    // rules.
    //
    DcSourceState sDcP3{};
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
    //
    // Per pass, not per run: --repeat=3 enters this hook three times, and a
    // state that accumulated across passes would have the second teardown
    // restoring what the first one had already put back.
    //
    sDcP3 = {};

    sDcP3.Voltage      = DcP3.outputVoltage();
    sDcP3.CurrentLimit = DcP3.currentLimit();
    sDcP3.Energised    = DcP3.isEnabled();
    sDcP3.Recorded     = true;

    return true;
}

//
// The Capacitance group's TEARDOWN: DcP3 back to what the setup found, if and
// only if something took it away.
//
// The three-part guard below is what keeps the fabric's use counts balanced.
// hal::SwitchFabric counts closes against opens rather than holding a bool (see
// its own comment), so a Connect issued here on a path nobody opened would leave
// that count one too high -- invisibly on the first pass, and cumulatively under
// --repeat. Asking the source whether it is still energised answers "did the
// script get as far as dropping it" without this hook having to reach into
// hal::fabric, which is not a suite file's business.
//
auto capacitanceTeardown() -> bool
{
    if( ! sDcP3.Recorded || ! sDcP3.Energised || DcP3.isEnabled())
    {
        return true;
    }

    //
    // Connect before Apply -- the relay closes on a dead path, then the output
    // comes up. Same rule, and the same reason, as the power-up sequence in
    // suite/scripts/rig_power_on.cpp; see core/verbs/source.hpp, and
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad, which
    // reads this run's own machine log to catch the inverse.
    //
    Connect( DcP3.dc());

    //
    // Chained through a named local rather than reassigned into one, because a
    // DcConfig holds a reference to its instrument: the builder is
    // copy-constructible, which a ternary needs, and not copy-assignable, which
    // `builder = builder.currentLimit( ...)` would need.
    //
    // The current limit is skipped where the setup found none programmed, so a
    // setting this suite never touches is left alone rather than being given a
    // value invented here. On this rig rigPowerOn always sets one, so the
    // nullopt branch is the detached-bench case -- which is also the case the
    // Energised guard above has already returned on. Written anyway: a hook that
    // is correct only because of what another file happens to do is a hook that
    // breaks when that file changes.
    //
    const auto voltage  = DcP3.dc().voltage( sDcP3.Voltage);
    const auto restored = sDcP3.CurrentLimit
                              ? voltage.currentLimit( *sDcP3.CurrentLimit)
                              : voltage;

    Apply( restored);

    return true;
}
