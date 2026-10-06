#pragma once

#include <string_view>

//
// The DSOX1202G's own vocabulary: the enums every config, builder and the
// driver itself are written in, the one reason a measurement can come back
// empty, and the channel bound. Depends on nothing else in this driver, which
// is what lets every other header here start from it.
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    //
    // ---------------------------------------------------------------------
    // The instrument's own vocabulary, as enums
    // ---------------------------------------------------------------------
    //
    // Every enumerator below is a value the *1000 X-Series* command set
    // accepts, taken from Keysight's InfiniiVision 1000 X-Series Programmer's
    // Guide (version 01.01.0000, February 2017) rather than from this rig's
    // previous scope. That distinction is the whole reason this is a second
    // driver rather than a typedef: an Infiniium 8000 and an InfiniiVision
    // 1000 X are both "a Keysight scope with an edge trigger", and the two
    // command sets disagree about what an input, an acquisition mode and a
    // sweep even are.
    //
    // Where they disagree, this file follows the box on the bench and says so
    // on the enum, so that a reader who knows the old driver can see which
    // differences are real hardware and which are naming.
    //

    //
    // :CHANnel<n>:COUPling -- AC or DC, and nothing else.
    //
    // This is the biggest single difference from the Infiniium driver beside
    // it, and it is a difference in the hardware rather than in the modelling.
    // hal::keysight_dsox1202g::ChannelInput folds coupling and input impedance
    // into ONE setting, because that instrument offers DC at 1 MOhm, DC at
    // 50 Ohm and AC at 1 MOhm and the combined enum is what stops a script
    // asking for the fourth combination. This instrument has no such choice to
    // make: ":CHANnel<n>:IMPedance <impedance> ::= ONEMeg" -- the guide's own
    // syntax line -- has exactly one legal value, and 1 MOhm is what the front
    // BNCs are.
    //
    // So impedance is not a setting here at all, and coupling stands alone.
    // Modelling it as a two-value ChannelInput{ Dc1M, Ac1M } would have kept
    // the old shape at the cost of writing an impedance into every call site
    // that the instrument cannot be asked about.
    //
    // What that costs this rig is not nothing, and it is a bench fact rather
    // than a code one: RfMux1 is a 50 Ohm card feeding this scope's channel 1
    // (see rig/wiring.inc), and a 50 Ohm path into a 1 MOhm input is
    // unterminated. That needs a feedthrough terminator at the BNC, which is a
    // part to buy, not a line to write.
    //
    enum class Coupling
    {
        Dc,
        Ac
    };

    //
    // :CHANnel<n>:BWLimit -- the internal low-pass filter.
    //
    // Two named states rather than a bool, so the call site says which one it
    // means: bandwidth( Bandwidth::Limited) rather than bandwidthLimit( true).
    // The filter is a measurement trade -- a stable reading on a noisy signal
    // at the cost of attenuating genuinely fast edges -- and that is a
    // decision a reader of the script needs to see rather than decode.
    //
    // The corner is ~25 MHz here (":CHANnel<n>:BANDwidth <limit> ::= 25E6"),
    // against the Infiniium's own figure. Same setting, different filter: a
    // script ported from the other scope keeps compiling and starts measuring
    // something slightly different, which is worth knowing when a ported
    // rise-time reading moves.
    //
    enum class Bandwidth
    {
        Full,
        Limited
    };

    //
    // :CHANnel<n>:DISPlay -- whether the channel is drawn on the scope's own
    // screen.
    //
    // Not cosmetic, and on this instrument less cosmetic than on most: the
    // guide is explicit that a measurement is made on the *displayed*
    // waveform, and that a measurement it cannot make comes back as
    // +9.9E+37 -- "typically because the proper portion of the waveform is not
    // displayed". Turning a channel off is therefore a way to make every
    // measurement on it fail, which is a thing a script should be able to say
    // deliberately and never by accident.
    //
    enum class ChannelDisplay
    {
        On,
        Off
    };

    //
    // :ACQuire:TYPE -- how the samples that make up the record are taken.
    //
    // Named Type rather than Mode, after the command, and that is not
    // pedantry: this instrument also has an :ACQuire:MODE, and it means
    // something else entirely ({ RTIMe | SEGMented }, real-time against
    // segmented capture). The Infiniium driver's AcquisitionMode is this
    // instrument's TYPE, and calling both "mode" here would have produced a
    // driver where the word meant two things one line apart.
    //
    // Averaged is the enumerator the old driver does not have, and its
    // presence collapses a pair of settings into one. On the Infiniium,
    // averaging is a separate on/off flag beside the mode, so that driver
    // carries Averaging and AverageCount and an unaveraged() that turns the
    // flag off. Here, averaging IS a type: selecting Normal, HighResolution or
    // PeakDetect is what "not averaged" means, and there is nothing left for
    // an unaveraged() to do that type() does not already say.
    //
    // Segmented is deliberately absent rather than listed-and-unsupported, and
    // for a different reason than on the Infiniium: it is not merely a shape
    // this driver's verbs are wrong for, it is a licensed option (SGM) this
    // instrument may not even have. A rig that buys the licence adds the
    // enumerator, the segment count and the segment index together.
    //
    enum class AcquisitionType
    {
        Normal,
        Averaged,
        HighResolution,
        PeakDetect
    };

    //
    // :TRIGger:SWEep -- what the scope does when no trigger arrives.
    //
    // Two values, where the Infiniium has three. AUTO forces a sweep if
    // nothing triggers within a scope-determined time; NORMal waits
    // indefinitely and leaves the previous acquisition on screen. This
    // instrument has no third "single" sweep: single-shot is run control here
    // (:SINGle, see SingleConfig in single.hpp), not a sweep setting, and modelling it
    // as one would have made Arm( Osc1.single()) and a sweep setting two
    // spellings of one thing.
    //
    // NOT :TRIGger:MODE, which on this instrument selects the *kind* of
    // trigger (EDGE / GLITch / PATTern / TV / ...). Same trap the Infiniium
    // driver documents, and the legacy ATE script's ":TRIGGER:MODE:AUTO" is
    // wrong against this command set for the same reason it was wrong against
    // that one.
    //
    enum class TriggerSweep
    {
        Auto,
        Normal
    };

    //
    // :TRIGger[:EDGE]:SLOPe.
    //
    // Four enumerators, and two of them are the ones the Infiniium driver
    // could not offer. That driver's comment says it plainly -- "there is no
    // EITHER on this instrument ... either-edge triggering is an InfiniiVision
    // feature" -- and this is an InfiniiVision. The guide's syntax line is
    // "{ POSitive | NEGative | EITHer | ALTernate }".
    //
    // Either triggers on both edges, which is what a script wants when the
    // event of interest is a transition rather than a direction. Alternating
    // alternates between the two on successive sweeps, which is a
    // continuously-running display feature and is listed here because the
    // instrument has it -- a single-shot capture that asks for it gets
    // whichever edge the scope was on, so a script arming one shot wants one
    // of the first three.
    //
    enum class TriggerSlope
    {
        Rising,
        Falling,
        Either,
        Alternating
    };

    //
    // :TRIGger[:EDGE]:COUPling -- how the trigger comparator sees the source,
    // independently of how the channel itself is coupled for measurement.
    //
    // Three values here against the Infiniium's four, and the missing one has
    // not gone away -- it has moved. On this instrument the reject filters are
    // their own setting (:TRIGger[:EDGE]:REJect, see TriggerReject below)
    // rather than two more couplings, so high-frequency reject is expressible
    // *with* AC or DC coupling rather than instead of it. LFReject appears in
    // both commands, which is the instrument's own redundancy and not this
    // driver's: setting either one gets low-frequency reject.
    //
    enum class TriggerCoupling
    {
        Dc,
        Ac,
        LowFrequencyReject
    };

    //
    // :TRIGger[:EDGE]:REJect -- the trigger path's noise filters.
    //
    // Off is a real value rather than "unset", and the difference matters: a
    // config that never named a reject filter leaves whatever the instrument
    // was last told (see the config headers on what nullopt means), where
    // reject( TriggerReject::Off) is a script saying it wants neither filter.
    // A test that triggers reliably only because the last script left HF
    // reject on is exactly the inherited state this framework is built to
    // make visible.
    //
    enum class TriggerReject
    {
        Off,
        LowFrequency,
        HighFrequency
    };

    //
    // :TIMebase:REFerence -- where on the screen the trigger instant sits, and
    // therefore how much of the record is before the trigger and how much
    // after.
    //
    // Left means the trigger is at the left edge and the whole record is what
    // followed it, which is what a script capturing the aftermath of an event
    // it caused itself wants. Center splits the record either side of the
    // trigger, Right makes the record the history leading up to it.
    //
    enum class TimebaseReference
    {
        Left,
        Center,
        Right
    };

    //
    // ---------------------------------------------------------------------
    // Why a measurement can come back with no number in it
    // ---------------------------------------------------------------------
    //
    // One reason, because this instrument gives one reason. The guide is a
    // single sentence on the subject: "If a measurement cannot be made
    // (typically because the proper portion of the waveform is not displayed),
    // the value +9.9E+37 is returned for that measurement." There is no
    // :MEASure:SENDvalid on the 1000 X-Series, no result-state code beside the
    // number, and nothing else to ask -- +9.9E+37 is also this instrument's
    // representation of infinity, so even the sentinel is shared.
    //
    // That is a real loss against the scope this replaces, and it is worth
    // stating as a loss rather than quietly shipping a shorter enum.
    // hal::keysight_dsox1202g::MeasurementFault carries nineteen of the
    // Infiniium's own result states -- "waveform is clipped high", "required
    // edge not found", "top and base are equal" -- and its comment makes the
    // case for them: "Rise time unmeasurable" sends an engineer to the scope,
    // "Rise time unmeasurable: waveform is clipped high" sends them to the
    // vertical scale, which is where the fault actually is. This instrument
    // cannot tell them which, and a driver that guessed would be putting words
    // in its mouth.
    //
    // So: one string, and it says only what is true. A script with a specific
    // meaning for a missing answer can still write one (see
    // core::Port::whenUnmeasurable) -- what it can no longer do is
    // discriminate on the reason, because there is only one. Any script ported
    // from the Infiniium that branched on the text has to decide which branch
    // it now takes, and suite/scripts/ac_dropout_script.cpp is this suite's
    // worked example of making that decision explicitly.
    //
    // A string_view constant rather than a one-enumerator enum: an enum with a
    // single value is a switch with nothing to switch on, and the day this
    // instrument grows a second reason it will be because Keysight added a
    // command, at which point the enum comes back with the manual's own
    // wording behind it.
    //
    inline constexpr std::string_view kUnmeasurable = "measurement could not be made";

    class DSOX1202G;

    //
    // Bounds a channel number to this instrument's real hardware -- a
    // DSOX1202G has two physical input channels, so Channel<3> (or <0>) simply
    // has no valid instantiation.
    //
    // Two, and this is where the model number earns its reading: the 1000
    // X-Series is DSOX1202A/G with two channels, and its 1200 X-Series
    // successor DSOX1204A/G with four. A script written for the four-channel
    // scope this rig used to have does not silently retarget onto this one --
    // channel<3>() stops compiling, which is the entire point of the bound
    // being a concept rather than a runtime range check.
    //
    template<unsigned N>
    concept ValidChannel = ( N >= 1 && N <= 2);

    //
    // Declared here so that DSOX1202G::channel<N>() can name it; defined in
    // channel_view.hpp, after the instrument, because every one of its
    // members needs the instrument to be a complete type.
    //
    template<unsigned N>
        requires ValidChannel<N>
    class Channel;
} // namespace hal::keysight_dsox1202g
