#include "../prelude.hpp"

#include <limits>
#include <string_view>

//
// Drop the primary AC input and watch what the 5 V output rail does about it.
//
// The DUT is meant to ride a loss of its 115 V / 400 Hz input on the backup
// supplies, so the rail should sag briefly and recover rather than collapse.
// "Briefly" and "not far" are the criteria; the negative transient is what
// this script measures.
//
// The point of this script, beyond the check itself, is the shape of a
// single-shot capture -- the one measurement in this suite whose stimulus the
// script itself causes, and therefore the one that cannot be written as a
// plain Measure:
//
//     set the scope up, arm it, cause the event, wait, then read
//
// Every one of those is a separate verb because every one is a separate thing
// that can go wrong, and a log showing them in order is what tells you whether
// a missing transient means the DUT held up, the scope never triggered, or the
// rail was never dropped in the first place.
//
auto acDropoutScript() -> void
{
    //
    // ------------------------------------------------------------------
    // Set the scope up
    // ------------------------------------------------------------------
    //
    // Four Setups because the instrument has four subsystems, and each one
    // answers a different question: when to capture, how long a window,
    // how to sample it, and what the probe on channel 3 is looking at. A
    // single call naming all of it would read as one decision, and it is
    // four.
    //

    //
    // Auto sweep, on purpose: this capture has to produce a waveform whether
    // or not the transient it is hunting for actually exists. With a
    // triggered sweep, a DUT that rode the dropout perfectly would leave the
    // scope waiting for an edge that never came, and "no transient" would be
    // indistinguishable from "the capture failed" -- which is precisely the
    // distinction the Verify below is for.
    //
    // Falling edge, and only falling: this instrument's edge trigger has no
    // either-edge setting (see hal::TriggerSlope), and a rail dropping out is
    // a falling event anyway.
    //
    Setup( Osc1.trigger()
               .edgeSource<3>()
               .slope( hal::TriggerSlope::Falling)
               .level( 4.8_V)
               .sweep( hal::TriggerSweep::Auto)
               .coupling( hal::TriggerCoupling::Dc));

    //
    // 10 ms/div with the trigger at the left edge, so the whole 100 ms
    // record is what happened *after* the input was lost. Reference::Center
    // would spend half the window on history that is known to be steady.
    //
    Setup( Osc1.timebase()
               .timePerDivision( 10_ms)
               .reference( hal::TimebaseReference::Left));

    //
    // High-resolution acquisition and, below, a bandwidth-limited input.
    // Both trade fast-edge fidelity for a stable, reproducible reading on a
    // noisy signal, and both are the right trade here: what is being measured
    // is how far a rail sagged over milliseconds, not how sharply it started
    // to.
    //
    // Not averaged, and it could not be: averaging works across successive
    // triggers and there is exactly one (see the acquisition builder's own
    // comment on the two mechanisms).
    //
    Setup( Osc1.acquisition()
               .mode( hal::AcquisitionMode::HighResolution)
               .automaticPoints()
               .unaveraged());

    //
    // Channel 3 carries a divide-by-10 probe on the 5 V rail. DC coupling at
    // 1 MOhm, because the rail's DC level is the thing the transient is
    // measured against -- AC coupling would throw away the baseline and leave
    // only the wiggle.
    //
    // The vertical offset is what makes 100 mV/div usable on a 5 V rail:
    // without it the trace sits five divisions off screen, and an off-screen
    // trace is one of the documented reasons this instrument refuses to
    // measure at all. Every measurement below is taken relative to the trace,
    // so the offset does not bias any of them -- which is why it needs no
    // compensating arithmetic afterwards.
    //
    Setup( Osc1.channel<3>()
               .input( hal::ChannelInput::Dc1M)
               .probeAdapter( hal::ProbeAdapter::Div10)
               .voltsPerDivision( 100_mV)
               .verticalOffset( 5_V)
               .bandwidth( hal::Bandwidth::Limited)
               .display( hal::ChannelDisplay::On));

    //
    // ------------------------------------------------------------------
    // The baseline, before anything is disturbed
    // ------------------------------------------------------------------
    //
    // vbase(), not vmin() and not vaverage(): base is the settled level the
    // waveform spends its time at, so a spike later in the record does not
    // move it. That is exactly what a transient should be measured against.
    //
    const auto baseline = Measure( Osc1.channel<3>().vbase(), at( dut::Output5V));

    Verify( FS_Transient_1::FS_Transient_5V0_Baseline, baseline);

    //
    // ------------------------------------------------------------------
    // Arm, drop the input, wait
    // ------------------------------------------------------------------
    //
    // Arm returns only once the scope is armed and ready to trigger -- which
    // is why the Remove below can be the very next line. Told-to-arm and
    // armed are different moments, and a single-shot capture that causes its
    // event in the gap between them silently records nothing. See
    // core/acquire.hpp, and Keysight's own "oscilloscope is armed and ready,
    // enable DUT here".
    //
    Arm( Osc1.single().timeout( 2_s));

    //
    // The event. Remove before Disconnect, so the relay opens on a dead path
    // rather than under load -- see core/source.hpp, which states the rule and
    // says why it is recorded rather than refused. Swapping these two lines is
    // not a build error and not a failing script; what catches it is
    // AcceptanceMachineLog.NoShippedScriptOrHookMovesARelayUnderLoad, which
    // reads this run's own machine log.
    //
    Remove(     AcP1.ac());
    Disconnect( AcP1.ac());

    const auto captured = Await( Osc1.single());

    //
    // Checked before anything is read out of the acquisition, and this
    // ordering is the point rather than tidiness: a measurement taken after a
    // capture that never completed is a measurement of whatever was in the
    // buffer beforehand. It would be a number, it would very likely be in
    // tolerance, and it would mean nothing.
    //
    Verify( FS_Transient_1::FS_Transient_Captured, captured);

    //
    // ------------------------------------------------------------------
    // How far the rail actually sagged
    // ------------------------------------------------------------------
    //
    // The reading itself is inside the guard, not merely the arithmetic on
    // it. With no capture, the acquisition buffer still holds whatever was in
    // it beforehand, so a vmin() taken here would return a real number about
    // a real waveform -- just not this one. Measuring it and then declining
    // to use it would still put that number in the log, where it would read
    // as the transient this test went looking for.
    //
    // Two readings at one pin, which is why the scope's ports carry a
    // qualifier: they key as "Output5V.Vbase" and "Output5V.Vmin" rather than
    // both as "Output5V" (see core::MeasureEngine).
    //
    // Something is recorded either way -- never nothing -- because a report in
    // which a check simply does not appear reads as a run that did not need it.
    // Same argument consoleScript makes about a reply too short to hold a
    // status byte. What differs is which of the two the else branch below
    // records: a verdict, or a Fail saying the check could not be made.
    //
    if( captured)
    {
        //
        // whenUnmeasurable says what this script means by a missing answer,
        // on the line that asks the question. A scope that cannot find a
        // minimum here has found no excursion at all, and no excursion is a
        // dip of zero volts -- a pass, and it should be recorded as one
        // rather than as a NaN failing a check the DUT actually met.
        //
        // Note what this deliberately does NOT do: substitute for every
        // reason the instrument might give. A clipped trace also has no
        // trustworthy minimum, and that one is a bench fault rather than a
        // healthy DUT -- so it is left to come back as NaN and fail, with the
        // instrument's own words in the log beside it.
        //
        const auto lowest = Measure(
            Osc1.channel<3>().vmin().whenUnmeasurable(
                []( const std::string_view reason) -> core::quantities::Voltage
                {
                    return reason.contains( "min not found")
                               ? 0_V
                               : core::quantities::Voltage{ std::numeric_limits<double>::quiet_NaN() };
                }),
            at( dut::Output5V));

        //
        // The transient is the sag from the settled rail, so it is stated as
        // a positive depth rather than as the absolute voltage at the bottom
        // of it. Both are derivable from the other, and the criterion is far
        // easier to read and to trace to a specification as "must not dip
        // more than 200 mV" than as "must stay above 4.8 V", which silently
        // restates the nominal rail voltage in a second place.
        //
        const auto dip = baseline - lowest;

        Verify( FS_Transient_1::FS_Transient_5V0_Dip, dip);
    }
    else
    {
        //
        // Deliberately the prose form of Fail rather than
        // FS_Transient_5V0_Dip. The criterion means "the rail dipped no more
        // than this much", and nothing here measured how far the rail dipped
        // -- naming it, even as unchecked, would attach this run's silence to
        // a requirement it has nothing to say about. What did happen is that
        // the check could not be made, and that is what this says.
        //
        Fail( "5Vdc rail dip on AC dropout -- not measured, the capture did not complete");
    }

    //
    // ------------------------------------------------------------------
    // Putting the bench back is not this script's job
    // ------------------------------------------------------------------
    //
    // The AC source stays off and disconnected when this returns. Its
    // group's TEARDOWN puts it back -- see
    // suite/scripts/transient_bracket.cpp, and suite/test_catalog.inc where
    // the Transient group declares the pair.
    //
    // It used to be restored right here, with a trailing Connect/Apply naming
    // 115 V / 400 Hz / 2 A a second time. Two things were wrong with that, and
    // neither was the restoring. The values were a second copy of what
    // rigPowerOn had already established, kept in step by nothing; and a
    // restore written at the end of a script only runs on the path where the
    // script reaches its end, where the group's teardown runs on every path
    // (libs/runner/src/main.cpp constructs its guard before the group's setup).
    //
}
