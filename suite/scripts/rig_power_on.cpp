#include "../prelude.hpp"

//
// The catalog's SETUP: this rig's ordered power-up, run once before the first
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
    // dead in both directions. See core/source.hpp, which states the rule and
    // says why it is recorded rather than refused -- a Connect written the other
    // way round here would still close the relay, and would put "hot switching"
    // in the log of every run using this rig. That log is asserted on, so it
    // would also turn the build red: see
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad.
    //
    // DcP1/DcP2 below get no Connect at all: they are hal::N6701ADirect, wired
    // straight through with no isolation relay, so there is nothing to
    // sequence and Connect( DcP1.dc()) would not compile. Only DcP3 and AcP1
    // have one.
    //
    Connect( AcP1.ac());
    Apply(   AcP1.ac().phaseVoltage( 115_V).frequency( 400_Hz).currentLimit( 2_A));

    //
    // Read back from the source itself -- no at(...), no point. This asks the
    // instrument what it is delivering over its own interface, which is a
    // different question from what a DMM would see at a DUT pin: a supply's
    // rail does not travel the signal matrix (see hal::N6701A's own comment on
    // why its output is hard-wired), so there is no route to name. Note that
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
    // construction -- with per-phase setpoints available (see hal::Ac6834B),
    // checking phase A alone would pass a rig that had lost B or C entirely.
    // The Apply above is balanced, so all three are held to the same
    // criterion; an unbalanced power-up would want one criterion per phase.
    //
    for( const auto phase : hal::phases)
    {
        allPassed &= Verify( "Primary AC input at nominal, phase " + std::string( hal::to_string( phase)),
                             EQ( 115_V).epsilon( 2_V),
                             Measure( AcP1.measuredVoltage( phase)));
    }

    //
    // Backup supply -- dut::BackupSupply, "28Vdc backup supply". The pairing
    // named in this and the two comments below is no longer only a comment:
    // it is recorded as data in rig/wiring.inc's SOURCE_WIRING table and
    // cross-checked against dut/adapter.inc's SOURCE_POINTs when dut_tests
    // builds, so this file and the rig cannot drift apart silently.
    //
    Apply( DcP1.dc().voltage( 28_V).currentLimit( 7_A));

    allPassed &= Verify( "Backup supply at nominal",
                         EQ( 28_V).epsilon( 0.1_V),
                         Measure( DcP1.measuredVoltage()));

    //
    // Secondary backup supply -- dut::BackupSupply_2.
    //
    Apply( DcP2.dc().voltage( 28_V).currentLimit( 7_A));

    allPassed &= Verify( "Secondary backup supply at nominal",
                         EQ( 28_V).epsilon( 0.1_V),
                         Measure( DcP2.measuredVoltage()));

    //
    // Battery supply -- dut::BatterySupply.
    //
    Connect( DcP3.dc());
    Apply(   DcP3.dc().voltage( 24_V).currentLimit( 4_A));

    allPassed &= Verify( "Battery supply at nominal",
                         EQ( 24_V).epsilon( 0.1_V),
                         Measure( DcP3.measuredVoltage()));

    
    return allPassed;
}
