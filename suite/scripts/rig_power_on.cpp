#include "../prelude.hpp"

//
// The catalog's RUN_SETUP: this rig's ordered power-up, run once before the first
// selected script. Its counterpart is rigPowerOff() alongside -- see that
// file's comment for why an ordered sequence earns a hook of its own rather
// than being left to hal::safeRig(), and README.md's "Bracket a run with setup
// and teardown" for what the runner promises about both.
//
// Unlike the teardown, this one has a verdict worth returning. It brings each
// source up and reads back what the instrument says it is actually delivering,
// so `false` here means the rig never reached the state the scripts are written
// against -- and a false setup stops the run before the first test, which is
// the right outcome: a suite that measures DUT rails on a bench that never
// powered up produces failures that say nothing about the DUT.
//
// The order is the teardown's, reversed. rigPowerOff() takes the DC rails down
// before the primary AC, so this brings the primary up first and the alternates
// after -- preserving the property the teardown is ordered for, in both
// directions: the DUT is never running on a backup rail while the primary is
// absent. Read the two files as one sequence and its inverse.
//
auto rigPowerOn() -> bool
{
    bool allPassed = true;

    //
    // Relay closed first, source energised second -- the mirror of the
    // teardown's Remove-then-Disconnect, read the other way round: a relay
    // closing onto an already-live supply arcs on contact bounce much as one
    // opening under load arcs on break, so the contacts move while the path is
    // dead in both directions. See core/verbs/source.hpp, which states the rule and
    // says why it is recorded rather than refused -- a Connect written the other
    // way round here would still close the relay, and would put "hot switching"
    // in the log of every run using this rig. That log is asserted on, so it
    // would also turn the build red: see
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad.
    //
    // Every source this hook brings up has a relay, so every one of them is
    // Connect-then-Apply. That was not true while an N6701A fed the DC rails:
    // DcP1/DcP2 were hal::keysight_edu36311a::DirectOutput1, wired straight through with
    // nothing to sequence, and Connect( DcP1.dc()) would not compile. The
    // EDU36311A outputs that replaced them are 1 A and fit inside a 1260-18
    // relay's 2 A rating, so both are RelayOutput and both get sequenced (see
    // rig/instrument.inc). The one output that is still hard-wired, DcP5, is a
    // 5 A output no relay in this rack can carry -- and it drives nothing on
    // this DUT, so it does not appear here at all.
    //
    Connect( AcP1.ac());
    Apply(   AcP1.ac().phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 2_A));

    //
    // Read back from the source itself -- no at(...), no point. This asks the
    // instrument what it is delivering over its own interface, which is a
    // different question from what a DMM would see at a DUT pin: a supply's
    // rail does not travel the signal matrix (see
    // hal::keysight_edu36311a::EDU36311A's own comment on why a supply's
    // output is cabled rather than routed), so there is no route to name. Note that
    // passing at(...) here anyway *compiles* -- it selects the routed overload
    // and closes a mux path for a reading that never leaves the instrument. If
    // the question really is what arrives at the DUT's pin, that is a DMM at a
    // point -- Measure( Dmm1.voltage(), at( dut::BackupSupply)) -- and a
    // different check from this one.
    //
    // Measured inline rather than into a named local, deliberately: each value
    // is checked on the line that produces it, so there is no second variable
    // left in scope for the next rail's check to name by accident.
    //
    // All three phases, not one. This used to read a single unqualified
    // measuredVoltage(), which was sound only while the three were equal by
    // construction -- with per-phase setpoints available (see hal::keysight_ac6834b::Ac6834B),
    // checking phase A alone would pass a rig that had lost B or C entirely.
    // The Apply above is balanced, so all three are held to the same
    // criterion; an unbalanced power-up would want one criterion per phase.
    //
    for( const auto phase : hal::keysight_ac6834b::phases)
    {
        allPassed &= Verify( "Primary AC input at nominal, phase " + std::string( to_string( phase)),
                             EQ( 115_V).epsilon( 2_V),
                             Measure( AcP1.measuredVoltage( phase)));
    }

    //
    // Backup supply -- dut::BackupSupply, "28Vdc backup supply". The pairing
    // named in this and the comment below is no longer only a comment: it is
    // recorded as data in rig/wiring.inc's SOURCE_WIRING table and
    // cross-checked against dut/adapter.inc's SOURCE_POINTs when dut_tests
    // builds, so this file and the rig cannot drift apart silently.
    //
    // -- On the current limits, which are not what they were ----------------
    //
    // 1 A, where this rail was brought up at 7 A while an N6701A fed it. That
    // is not a tightening anybody chose: DcP6 is a 30 V / **1 A** output of an
    // EDU36311A, so 1 A is its badge and the most it can be asked for at all.
    // Asking for more is refused before anything reaches the wire
    // (hal::keysight_edu36311a::RatingExceeded), deliberately, so that a
    // simulated run and an attached one fail the same way.
    //
    // What that means on a bench, stated here because this is the file that
    // would be blamed for it: if the DUT draws more than 1 A on this rail, the
    // supply enters constant current, the rail sags below 28 V, and the
    // readback Verify below fails -- which stops the run before the first test
    // rather than letting a suite measure a DUT that is browning out. That is
    // the right failure and a legible one. It is also not something this file
    // can fix: 28 V at 7 A is 196 W and the whole chassis is 90 W. See
    // rig/instrument.inc's own TODO.
    //
    Connect( DcP6.dc());
    Apply(   DcP6.dc().voltage( 28_V).currentLimit( 1_A));

    allPassed &= Verify( "Backup supply at nominal",
                         EQ( 28_V).epsilon( 0.1_V),
                         Measure( DcP6.measuredVoltage()));

    //
    // Battery supply -- dut::BatterySupply. 1 A for the same reason, where
    // this rail used to be brought up at 4 A.
    //
    Connect( DcP7.dc());
    Apply(   DcP7.dc().voltage( 24_V).currentLimit( 1_A));

    allPassed &= Verify( "Battery supply at nominal",
                         EQ( 24_V).epsilon( 0.1_V),
                         Measure( DcP7.measuredVoltage()));

    //
    // dut::BackupSupply_2 is not brought up, and its absence is the one thing
    // in this sequence worth checking against the DUT rather than the rig: the
    // secondary backup rail has no supply on this bench (see dut/adapter.inc,
    // where it is a plain POINT for that reason). A DUT that needs all three
    // rails up to reach the state these scripts are written against will fail
    // its first test, not this hook.
    //
    return allPassed;
}
