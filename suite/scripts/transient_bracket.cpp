#include "../prelude.hpp"

#include <optional>

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
// is what acDropoutScript used to do:
//
//   - The teardown runs whether or not the test did. libs/runner/src/main.cpp
//     constructs the group's TeardownGuard *before* the group's setup, so a
//     restore written here happens on the paths where the script returned
//     early, failed, or never started -- and a suite whose later groups depend
//     on which of those happened is not a suite.
//
//   - The values put back are the ones actually found, not a second copy of
//     rigPowerOn's literals. The trailing Apply in the script restated
//     115 V / 400 Hz / 2 A a second time, in a second file, with nothing but
//     habit keeping the two in step.
//
// What is deliberately NOT here is the drop itself. Removing the AC input is
// this test's stimulus -- the event the capture is armed for -- so it belongs
// in the script that measures its effect, not in a bracket that runs whether or
// not anything was measured.
//

namespace
{
    //
    // What AcP1 was programmed to when this group's setup ran, and whether it
    // was delivering it. Every field is a setpoint readback (VOLTage?,
    // FREQuency?, CURRent?, VOLTage:RANGe?) rather than a measurement:
    // AcP1.measuredVoltage() answers what the source says it is producing,
    // which is the right question for a check and the wrong one for a restore.
    // Re-applying a measured 114.7 V would program the rig to a value nobody
    // ever asked for, and would do it again, a little further off, on every
    // pass.
    //
    // Optionals throughout, because "never programmed" is a state the
    // instrument distinguishes and the restore has to preserve: std::nullopt
    // means leave that setting alone, which is what it means everywhere else in
    // hal::Ac6834BConfig too. Range is normally exactly this case -- nothing in
    // this suite programs it.
    //
    struct AcSourceState
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
        // the AC input already down -- a detached bench, where no Apply ever
        // reaches a driver (see core/bench.hpp) -- must not have its teardown
        // energise one.
        //
        bool Energised{ false };

        hal::PerPhaseValues<core::quantities::Voltage>                PhaseVoltage{};
        std::optional<core::quantities::Frequency>                    Frequency{};
        std::optional<hal::PerPhaseValues<core::quantities::Current>> CurrentLimit{};
        std::optional<hal::PerPhaseValues<core::quantities::Voltage>> Range{};
    };

    //
    // The one thing the pair shares. File-local, so the only two functions that
    // can name it are the two whose invariant it is -- no test script in this
    // suite can reach it, and no other bracket can collide with it.
    //
    AcSourceState sAcP1{};

    //
    // One optional readback per phase, folded into one optional triple:
    // present only if all three phases are. The builder's per-phase setters
    // take all three or none, so a setting programmed on some phases and not
    // others is not something this hook could put back by halves -- and it is
    // not a state this instrument can be left in anyway (INSTrument:COUPle
    // broadcasts, see hal::Ac6834BBuilder).
    //
    template<typename QuantityT, typename ReadbackT>
    [[nodiscard]]
    auto perPhase( ReadbackT && readback) -> std::optional<hal::PerPhaseValues<QuantityT>>
    {
        hal::PerPhaseValues<QuantityT> values{};

        for( const auto phase : hal::phases)
        {
            const auto value = readback( phase);

            if( ! value.has_value())
            {
                return std::nullopt;
            }

            values[ hal::indexOf( phase)] = *value;
        }

        return values;
    }
} // namespace

//
// The Transient group's SETUP. Reads, and does not judge: it returns true
// unconditionally, and that is a decision rather than an omission.
//
// A setup returning false skips its own group's tests (see libs/runner/src/main.cpp),
// so a hook here that insisted on finding a live AC input would skip AcDropout
// in exactly the three modes where no Apply ever reaches a driver -- --replay,
// --inject and --skeleton all detach the bench (core/bench.hpp). Those runs
// take their readings from a file and are meant to reach every test the live
// run reaches. Whether the rig actually came up is already asked, once, by
// RUN_SETUP, where a false stops everything rather than one group.
//
auto transientSetup() -> bool
{
    //
    // Per pass, not per run: --repeat=3 enters this hook three times, and a
    // state that accumulated across passes would have the second teardown
    // restoring what the first one had already put back.
    //
    sAcP1 = {};

    for( const auto phase : hal::phases)
    {
        sAcP1.PhaseVoltage[ hal::indexOf( phase)] = AcP1.phaseVoltage( phase);
    }

    sAcP1.Frequency    = AcP1.frequency();
    sAcP1.CurrentLimit = perPhase<core::quantities::Current>(
                             []( const hal::Phase phase) { return AcP1.currentLimit( phase); });
    sAcP1.Range        = perPhase<core::quantities::Voltage>(
                             []( const hal::Phase phase) { return AcP1.range( phase); });

    sAcP1.Energised = AcP1.isEnabled();
    sAcP1.Recorded  = true;

    return true;
}

//
// The Transient group's TEARDOWN: AcP1 back to what the setup found, if and
// only if something took it away.
//
// The three-part guard below is what keeps the fabric's use counts balanced.
// hal::SwitchFabric counts closes against opens rather than holding a bool (see
// its own comment), so a Connect issued here on a path nobody opened would
// leave that count one too high -- invisibly on the first pass, and cumulatively
// under --repeat. Asking the source whether it is still energised answers
// "did the script get as far as dropping it" without this hook having to reach
// into hal::fabric, which is not a suite file's business.
//
auto transientTeardown() -> bool
{
    if( ! sAcP1.Recorded || ! sAcP1.Energised || AcP1.isEnabled())
    {
        return true;
    }

    //
    // Connect before Apply -- the relay closes on a dead path, then the output
    // comes up. Same rule, and the same reason, as the power-up sequence in
    // suite/scripts/rig_power_on.cpp; see core/source.hpp, and
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad, which
    // reads this run's own machine log to catch the inverse.
    //
    Connect( AcP1.ac());

    //
    // Chained through named locals rather than reassigned into one, because an
    // Ac6834BConfig holds a reference to its instrument: the builder is
    // copy-constructible, which a ternary needs, and not copy-assignable, which
    // `builder = builder.frequency( ...)` would need.
    //
    // Each stage is skipped where the setup found nothing programmed, so a
    // setting this suite never touches is left alone rather than being given a
    // value invented here. Range is normally that case.
    //
    // The range readback is already resolved to what the instrument selected
    // (150 V or 300 V, never the raw value someone passed), and hal::rangeFor
    // maps each of those to itself -- so it round-trips through the setter
    // rather than being re-resolved into the other range.
    //
    const auto voltages = AcP1.ac().phaseVoltage(
                              hal::phaseA( sAcP1.PhaseVoltage[ hal::indexOf( hal::Phase::A)]),
                              hal::phaseB( sAcP1.PhaseVoltage[ hal::indexOf( hal::Phase::B)]),
                              hal::phaseC( sAcP1.PhaseVoltage[ hal::indexOf( hal::Phase::C)]));

    const auto withFrequency = sAcP1.Frequency
                                   ? voltages.frequency( *sAcP1.Frequency)
                                   : voltages;

    const auto withCurrentLimit = sAcP1.CurrentLimit
                                      ? withFrequency.currentLimit(
                                            hal::phaseA( ( *sAcP1.CurrentLimit)[ hal::indexOf( hal::Phase::A)]),
                                            hal::phaseB( ( *sAcP1.CurrentLimit)[ hal::indexOf( hal::Phase::B)]),
                                            hal::phaseC( ( *sAcP1.CurrentLimit)[ hal::indexOf( hal::Phase::C)]))
                                      : withFrequency;

    const auto restored = sAcP1.Range
                              ? withCurrentLimit.range(
                                    hal::phaseA( ( *sAcP1.Range)[ hal::indexOf( hal::Phase::A)]),
                                    hal::phaseB( ( *sAcP1.Range)[ hal::indexOf( hal::Phase::B)]),
                                    hal::phaseC( ( *sAcP1.Range)[ hal::indexOf( hal::Phase::C)]))
                              : withCurrentLimit;

    Apply( restored);

    return true;
}
