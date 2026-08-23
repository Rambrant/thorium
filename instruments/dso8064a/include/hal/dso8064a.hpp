#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/describe.hpp"
#include "core/meta.hpp"
#include "core/port.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"
#include "core/waveform.hpp"

#include "hal/address.hpp"
#include "hal/describe.hpp"
#include "hal/instrument.hpp"

namespace hal
{
    //
    // ---------------------------------------------------------------------
    // The instrument's own vocabulary, as enums
    // ---------------------------------------------------------------------
    //
    // Every one of these replaces a string that the legacy ATE sent through
    // uninspected -- "InstrumentData Data=(:ACQUIRE:MODE HRES) at=osc3" and
    // its thirty siblings. A misspelled mnemonic there was discovered by the
    // scope rejecting it mid-run, if at all; here it is "no such enumerator".
    //
    // They are also where this driver refuses to invent capability. The
    // legacy script this was reconstructed from carries
    // ":TRIGGER:EDGE:SLOPE EITHER", and there is no EITHER on this
    // instrument -- the Infiniium 8000/54830 edge trigger takes POSitive or
    // NEGative and nothing else (either-edge triggering is an InfiniiVision
    // feature). Modelling it would have produced a driver that compiled a
    // setting the scope cannot perform. See TriggerSlope below.
    //

    //
    // :CHANnel<N>:INPut -- coupling and input impedance as ONE setting,
    // which is the fact worth carrying over from the datasheet.
    //
    // The legacy vocabulary spelled these as two independent knobs
    // ("Coupling=DC" beside an input selection), and they are not
    // independent: this instrument offers DC at 1 MOhm, DC at 50 Ohm, and AC
    // at 1 MOhm, and there is no AC-coupled 50 Ohm input to ask for. Two
    // separate settings would make that fourth combination expressible and
    // wrong.
    //
    // LfReject1/LfReject2 are the 1153A probe's low-frequency-reject inputs
    // (AC coupled, 1 MOhm). Named for what they do rather than as LFR1/LFR2
    // because the mnemonic says nothing to anybody who is not holding the
    // programmer's reference.
    //
    enum class ChannelInput
    {
        Dc1M,
        Dc50,
        Ac1M,
        LfReject1,
        LfReject2
    };

    //
    // :CHANnel<N>:BWLimit -- the internal low-pass filter, usable with either
    // coupling.
    //
    // Two named states rather than a bool, so the call site says which one it
    // means: bandwidth( Bandwidth::Limited) rather than bandwidthLimit( true).
    // Same reasoning hal::Parity gets, and it matters more here because the
    // filter is a measurement trade -- it buys a stable reading on a noisy
    // signal at the cost of attenuating genuinely fast edges, which is a
    // decision a reader of the script needs to see rather than decode.
    //
    enum class Bandwidth
    {
        Full,
        Limited
    };

    //
    // :CHANnel<N>:PROBe:EADapter -- which external divider adapter is fitted
    // to the end of the probe, so the scope scales what it reads.
    //
    // Valid on this family only for the 1153A/1154A/1159A probes; on anything
    // else the instrument answers with a settings-conflict error. That is a
    // fact about which probe is plugged in, which is a bench fact this driver
    // does not know -- so it is not modelled as a constraint here, and a rig
    // that sets it against the wrong probe finds out at run time. Contrast
    // ValidDso8064aChannel below, which IS a fixed property of the model and
    // therefore is a compile error.
    //
    enum class ProbeAdapter
    {
        None,
        Div10,
        Div20,
        Div100
    };

    //
    // :CHANnel<N>:DISPlay -- whether the channel is drawn on the scope's own
    // screen.
    //
    // Worth having even though nothing automated reads a screen, and the
    // legacy script shows why it is not cosmetic: it disabled the three
    // channels it was not using and explicitly enabled the one it was. On a
    // real Infiniium that is load-bearing -- an undisplayed channel is one of
    // the documented reasons a measurement comes back invalid ("Result
    // invalid. Waveform is not displayed", see MeasurementFault below) -- and
    // it is also what an operator standing at the bench sees while the suite
    // runs.
    //
    enum class ChannelDisplay
    {
        On,
        Off
    };

    //
    // :ACQuire:MODE.
    //
    // HighResolution is the one the legacy script reaches for, with a comment
    // saying why: it averages the samples taken within each time bucket,
    // which buys resolution and noise rejection on a quiet signal and blunts
    // genuinely fast transients. That trade is the same one Bandwidth::Limited
    // makes, one layer down, and a script wanting a reproducible reading of a
    // slow event usually wants both.
    //
    // Segmented is listed because the instrument has it, and deliberately
    // nothing here supports it further: it changes what a "capture" even is
    // (many short records rather than one), and the acquisition verbs below
    // are written for the single-record case. A rig that needs it should add
    // the segment count and index alongside, not discover halfway through
    // that Await answers for the wrong thing.
    //
    enum class AcquisitionMode
    {
        RealTime,
        PeakDetect,
        HighResolution,
        Segmented
    };

    //
    // :TRIGger:SWEep -- what the scope does when no trigger arrives.
    //
    // NOT :TRIGger:MODE, which on this instrument selects the *kind* of
    // trigger (EDGE/GLITch/ADVanced). The legacy script's
    // ":TRIGGER:MODE:AUTO" is the older HP 546xx spelling of this setting and
    // does not mean on an Infiniium what it meant there; conflating the two
    // would have produced a driver whose "mode" sometimes meant sweep and
    // sometimes meant trigger type.
    //
    // Auto forces a sweep if nothing triggers within a scope-determined time,
    // which is what the legacy script wanted: capture the waveform whether or
    // not the transient it is hunting for actually exists, so the test
    // produces a reading either way. Triggered and Single both leave the
    // previous acquisition on screen instead. Note the manual's warning that
    // Auto is a poor choice below about 50 Hz, where the scope will trigger
    // itself before the signal gets a chance to.
    //
    enum class TriggerSweep
    {
        Auto,
        Triggered,
        Single
    };

    //
    // :TRIGger:EDGE:SLOPe. Two enumerators, because the instrument has two --
    // see this section's own comment on the EITHER that does not exist here.
    //
    enum class TriggerSlope
    {
        Rising,
        Falling
    };

    //
    // :TRIGger:EDGE:COUPling -- how the trigger comparator sees the source,
    // independently of how the channel itself is coupled for measurement.
    //
    enum class TriggerCoupling
    {
        Dc,
        Ac,
        LowFrequencyReject,
        HighFrequencyReject
    };

    //
    // :TIMebase:REFerence -- where on the screen the trigger instant sits, and
    // therefore how much of the record is before the trigger and how much
    // after.
    //
    // Left means the trigger is at the left edge and the whole record is what
    // followed it, which is what you want when the thing being captured is a
    // consequence of the trigger -- a transient after a supply drops out, say.
    // Center gives half the record as pre-trigger history, which is what you
    // want when the question is what led up to the event.
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
    // An Infiniium answers an automatic measurement it could not make with
    // 9.99999E+37, and -- with :MEASure:SENDvalid on -- with a numeric result
    // state alongside saying which of some thirty specific things went wrong.
    // The enumerators below carry those states' own code numbers, so that a
    // future driver talking to real hardware maps SENDvalid's answer through
    // this enum rather than inventing a second numbering beside it.
    //
    // A subset of the instrument's table, not all of it. The ones left out
    // are the ones this rig cannot reach: eye-diagram and jitter-package
    // states, FFT peaks, colour-grade database conditions. Adding one is a
    // line here and a line in to_string below; leaving thirty unreachable
    // ones in would have made the list look like a specification of what this
    // driver handles rather than of what this bench can see.
    //
    // This is what the legacy ATE flattened into a single ISINVALID()
    // predicate, and the flattening is the loss worth naming. "Rise time
    // unmeasurable" sends an engineer to the scope. "Rise time unmeasurable:
    // waveform is clipped high" sends them to the vertical scale, which is
    // where the fault actually is. See core::UnmeasurableReading, which is how
    // this reaches a script and a log.
    //
    enum class MeasurementFault
    {
        Invalid                     = 4,
        RequiredEdgeNotFound        = 5,
        MaximumNotFound             = 6,
        MinimumNotFound             = 7,
        RequestedTimeNotFound       = 8,
        RequestedVoltageNotFound    = 9,
        TopAndBaseAreEqual          = 10,
        MeasurementZoneTooSmall     = 11,
        LowerThresholdOffWaveform   = 12,
        UpperThresholdOffWaveform   = 13,
        ThresholdsTooClose          = 14,
        CompletionCriteriaNotReached = 17,
        WaveformNotDisplayed        = 19,
        WaveformClippedHigh         = 20,
        WaveformClippedLow          = 21,
        WaveformClippedBothWays     = 22,
        NoDataOnScreen              = 24,
        SignalTooSmallToEvaluate    = 35,
        AwaitingAveraging           = 36
    };

    //
    // The instrument's own wording, near enough to be recognisable to anyone
    // holding the programmer's reference, and written as prose because this
    // string ends up in a sentence in the run log ("Config clock probe --
    // unmeasurable: waveform is clipped high").
    //
    // Hand-written rather than reflected through core::meta::to_string, and
    // this is exactly the case hal/describe.hpp's describeChoice carves out:
    // the text wanted here is not the enumerator's spelling, it is what the
    // instrument says. Same split core::to_string(LogLevel) already makes.
    //
    [[nodiscard]]
    constexpr auto to_string( const MeasurementFault fault) -> std::string_view
    {
        switch( fault)
        {
            case MeasurementFault::Invalid:                      return "result returned is invalid";
            case MeasurementFault::RequiredEdgeNotFound:         return "required edge not found";
            case MeasurementFault::MaximumNotFound:              return "max not found";
            case MeasurementFault::MinimumNotFound:              return "min not found";
            case MeasurementFault::RequestedTimeNotFound:        return "requested time not found";
            case MeasurementFault::RequestedVoltageNotFound:     return "requested voltage not found";
            case MeasurementFault::TopAndBaseAreEqual:           return "top and base are equal";
            case MeasurementFault::MeasurementZoneTooSmall:      return "measurement zone too small";
            case MeasurementFault::LowerThresholdOffWaveform:    return "lower threshold not on waveform";
            case MeasurementFault::UpperThresholdOffWaveform:    return "upper threshold not on waveform";
            case MeasurementFault::ThresholdsTooClose:           return "upper and lower thresholds are too close";
            case MeasurementFault::CompletionCriteriaNotReached: return "completion criteria not reached";
            case MeasurementFault::WaveformNotDisplayed:         return "waveform is not displayed";
            case MeasurementFault::WaveformClippedHigh:          return "waveform is clipped high";
            case MeasurementFault::WaveformClippedLow:           return "waveform is clipped low";
            case MeasurementFault::WaveformClippedBothWays:      return "waveform is clipped high and low";
            case MeasurementFault::NoDataOnScreen:               return "no data on screen";
            case MeasurementFault::SignalTooSmallToEvaluate:     return "signal may be too small to evaluate";
            case MeasurementFault::AwaitingAveraging:            return "awaiting completion of averaging";
        }

        return "result returned is invalid";
    }

    class DSO8064A;

    //
    // Bounds a channel number to this instrument's real hardware -- a real
    // DSO8064A has four physical input channels, so DSO8064AChannel<5> (or
    // <0>) simply has no valid instantiation. Kept as its own concept
    // (rather than inlined into DSO8064AChannel's own template) so the
    // bound lives in exactly one place and reads at the call site as "this
    // channel number, checked against this instrument" rather than a bare
    // arithmetic condition.
    //
    template<unsigned N>
    concept ValidDso8064aChannel = ( N >= 1 && N <= 4);

    //
    // ---------------------------------------------------------------------
    // What a Setup call on this instrument boils down to
    // ---------------------------------------------------------------------
    //
    // Four config types, one per SCPI subsystem the scope actually has --
    // :TRIGger, :TIMebase, :ACQuire and :CHANnel<N> -- rather than one
    // flattened "scope settings" bag.
    //
    // That split is not filing. It is what makes each Setup call in a script
    // say what part of the instrument it is touching, and it matches how the
    // settings interact: everything in the trigger config is about one
    // decision (when to capture), everything in the channel config is about
    // one input, and the two are independent. A single config would also have
    // made "which channel" a field on a structure that is otherwise
    // instrument-wide -- and then a Setup naming a timebase and a channel
    // offset in one chain would read as though the timebase were per-channel.
    //
    // Every field is std::optional and unset means "leave whatever is already
    // configured" -- the same convention core::MeasureSetup uses on the
    // sensing side and hal::Racal1260Config on the framing side, and for the
    // same reason: a script that sets only the trigger level must not
    // silently reset the slope to whatever this file would have defaulted it
    // to.
    //

    //
    // :TRIGger -- when to capture.
    //
    // Source, slope and level travel together on purpose, because on the
    // instrument they are not independent: :TRIGger:EDGE:SLOPe applies to
    // whichever source :TRIGger:EDGE:SOURce last selected, and
    // :TRIGger:LEVel is stored per channel. Three separate Setup calls would
    // read as three independent facts and behave as one ordered sequence.
    //
    struct DSO8064ATriggerConfig
    {
        DSO8064A &                              Instrument;

        //
        // Which channel the edge trigger watches. A plain unsigned here,
        // although the builder that sets it takes the channel number as a
        // template argument checked against ValidDso8064aChannel -- the
        // check happens where the number is written, and what survives into
        // the config is the value. Same shape as DSO8064AChannel<N> handing
        // N to setChannel().
        //
        std::optional<unsigned>                 EdgeSource;
        std::optional<TriggerSlope>             Slope;
        std::optional<core::quantities::Voltage> Level;
        std::optional<TriggerSweep>             Sweep;
        std::optional<TriggerCoupling>          Coupling;
        std::optional<core::quantities::Time>   Holdoff;
    };

    //
    // :TIMebase -- how much time the record covers, and where the trigger
    // sits within it.
    //
    struct DSO8064ATimebaseConfig
    {
        DSO8064A &                              Instrument;

        //
        // Seconds per division, the number written on the front panel and in
        // every test spec -- not :TIMebase:RANGe, which is the same fact
        // times ten. Both exist on the instrument; carrying both here would
        // have let a script set them to values that disagree.
        //
        std::optional<core::quantities::Time>   TimePerDivision;

        //
        // Time between the trigger event and the reference point below.
        // Positive delays the record after the trigger; negative shows what
        // preceded it.
        //
        std::optional<core::quantities::Time>   Position;
        std::optional<TimebaseReference>        Reference;
    };

    //
    // :ACQuire -- how the samples that make up the record are taken.
    //
    struct DSO8064AAcquisitionConfig
    {
        DSO8064A &                              Instrument;
        std::optional<AcquisitionMode>          Mode;

        //
        // Memory depth. Points and AutomaticPoints are two settings on the
        // instrument (:ACQuire:POINts and :ACQuire:POINts:AUTO) and stay two
        // here, because "let the scope choose" and "use exactly this many"
        // are different instructions and a script may well want to say the
        // first without knowing what the second would have been. The builder
        // is what keeps them consistent -- see points()/automaticPoints().
        //
        std::optional<unsigned>                 Points;
        std::optional<bool>                     AutomaticPoints;

        //
        // Averaging, as one decision in two fields: whether, and how many.
        // Set only through averagedOver()/unaveraged() on the builder, which
        // is what stops "averaging on, count unspecified" from being
        // expressible -- the instrument would then use whatever count it was
        // last told, which is exactly the kind of inherited state a
        // reproducible test must not depend on.
        //
        std::optional<bool>                     Averaging;
        std::optional<unsigned>                 AverageCount;

        std::optional<core::quantities::Frequency>  SampleRate;
        std::optional<bool>                         AutomaticSampleRate;
    };

    //
    // :CHANnel<N> -- one input, vertically.
    //
    struct DSO8064AChannelConfig
    {
        DSO8064A &                              Instrument;

        //
        // Which input this configures. Not optional, unlike everything
        // below it: a channel config with no channel is not an
        // underspecified instruction, it is not an instruction at all. It is
        // filled in by DSO8064AChannel<N>, so a script can only ever produce
        // one of these by naming a channel that ValidDso8064aChannel accepts.
        //
        unsigned                                Channel;

        std::optional<ChannelInput>             Input;

        //
        // Volts per division -- the front-panel number again, and again not
        // its :CHANnel<N>:RANGe sibling, for the reason
        // DSO8064ATimebaseConfig gives about the horizontal axis.
        //
        std::optional<core::quantities::Voltage>  VoltsPerDivision;

        //
        // The voltage represented at the centre of the screen. Offsetting a
        // small signal that sits on a large DC level is what lets the
        // vertical scale be turned up far enough to resolve it -- and every
        // measurement the scope then makes is relative to the trace, so the
        // offset does not bias the answer. The legacy script says exactly
        // this in a comment and then subtracts the offset back out by hand in
        // three places, which is the arithmetic this field exists to make
        // unnecessary.
        //
        std::optional<core::quantities::Voltage>  VerticalOffset;

        std::optional<Bandwidth>                BandwidthLimit;
        std::optional<ProbeAdapter>             Probe;
        std::optional<ChannelDisplay>           Display;
    };

    //
    // :SINGle, plus the two status registers that make it usable -- the
    // config behind Arm and Await (see core/acquire.hpp).
    //
    // Both verbs take the same config because they are two halves of one
    // operation, and the timeouts belong to the halves rather than to the
    // instrument: a script that arms, drops a rail and waits is stating how
    // long each of those is allowed to take, and those are different numbers
    // for different tests on the same scope.
    //
    struct DSO8064ASingleConfig
    {
        DSO8064A &                              Instrument;

        //
        // How long Await will poll :ADER? before giving up and reporting the
        // capture as not completed. Unset means the driver's own default (see
        // DSO8064A::kDefaultCaptureTimeout).
        //
        std::optional<core::quantities::Time>   Timeout;

        //
        // How long Arm will poll :AER? for the scope to report itself armed
        // and ready. A separate number from the one above because it bounds a
        // different thing -- the instrument getting ready, which takes as
        // long as it takes regardless of the DUT, against the event arriving,
        // which is entirely about the DUT.
        //
        std::optional<core::quantities::Time>   ArmTimeout;
    };

    //
    // :WAVeform -- which record to transfer off the instrument, for Fetch.
    //
    // Carries the channel and nothing else. Everything the transfer needs
    // besides that is either fixed by this driver (the format: the samples come
    // back scaled into volts, see fetchWaveform below) or already decided by
    // the acquisition that produced the record -- how many points there are and
    // how far apart they sit were settled when the scope triggered, and a
    // :WAVeform setting cannot change them after the fact.
    //
    // Separate from DSO8064AChannelConfig even though both are "a channel and
    // some settings", because they are instructions to different subsystems at
    // different times: one configures an input before a capture, the other
    // names a record to read out after one. Folding them together would make
    // Setup( Osc1.channel<3>()) and Fetch( Osc1.channel<3>()) the same config
    // reaching two verbs, and a Setup that had to ignore half its fields.
    //
    struct DSO8064AWaveformConfig
    {
        DSO8064A &  Instrument;

        //
        // Not optional, for DSO8064AChannelConfig::Channel's reason: a
        // transfer with no source is not an underspecified instruction, it is
        // not one at all. Filled in by DSO8064AChannel<N>, so a script can only
        // produce one by naming a channel ValidDso8064aChannel accepts.
        //
        unsigned    Channel;
    };

    //
    // ---------------------------------------------------------------------
    // The fluent chains a script builds before handing them to a verb
    // ---------------------------------------------------------------------
    //
    // All the same "return *this by value, updated" shape as
    // hal::N6701ABuilder, hal::Racal1260Builder and core::Port's setup
    // builders -- so "how do I set X" reads the same way whether X is
    // sourced, sensed, framed or triggered.
    //

    class DSO8064ATriggerBuilder
    {
        public:
            using Config = DSO8064ATriggerConfig;

            explicit DSO8064ATriggerBuilder( DSO8064A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt }
            {}

            //
            // Which channel the trigger watches, checked the same way
            // DSO8064A::channel<N>() is -- edgeSource<7>() has no valid
            // instantiation on a four-channel scope, rather than being a
            // settings-conflict error discovered on the bench.
            //
            template<unsigned N>
                requires ValidDso8064aChannel<N>
            [[nodiscard]]
            auto edgeSource() const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.EdgeSource = N;
                return copy;
            }

            [[nodiscard]]
            auto slope( const TriggerSlope value) const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.Slope = value;
                return copy;
            }

            [[nodiscard]]
            auto level( const core::quantities::Voltage value) const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.Level = value;
                return copy;
            }

            [[nodiscard]]
            auto sweep( const TriggerSweep value) const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.Sweep = value;
                return copy;
            }

            [[nodiscard]]
            auto coupling( const TriggerCoupling value) const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.Coupling = value;
                return copy;
            }

            [[nodiscard]]
            auto holdoff( const core::quantities::Time value) const -> DSO8064ATriggerBuilder
            {
                auto copy = *this;
                copy.mConfig.Holdoff = value;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    class DSO8064ATimebaseBuilder
    {
        public:
            using Config = DSO8064ATimebaseConfig;

            explicit DSO8064ATimebaseBuilder( DSO8064A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto timePerDivision( const core::quantities::Time value) const -> DSO8064ATimebaseBuilder
            {
                auto copy = *this;
                copy.mConfig.TimePerDivision = value;
                return copy;
            }

            [[nodiscard]]
            auto position( const core::quantities::Time value) const -> DSO8064ATimebaseBuilder
            {
                auto copy = *this;
                copy.mConfig.Position = value;
                return copy;
            }

            [[nodiscard]]
            auto reference( const TimebaseReference value) const -> DSO8064ATimebaseBuilder
            {
                auto copy = *this;
                copy.mConfig.Reference = value;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    class DSO8064AAcquisitionBuilder
    {
        public:
            using Config = DSO8064AAcquisitionConfig;

            explicit DSO8064AAcquisitionBuilder( DSO8064A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto mode( const AcquisitionMode value) const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.Mode = value;
                return copy;
            }

            //
            // An explicit memory depth, which necessarily turns the automatic
            // choice off -- asking for exactly a million points and leaving
            // the scope free to pick a different number is not a coherent
            // instruction, and setting :ACQuire:POINts on the real instrument
            // clears :POINts:AUTO anyway. Written here rather than left to the
            // driver so that the config a log renders says both.
            //
            [[nodiscard]]
            auto points( const unsigned count) const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.Points          = count;
                copy.mConfig.AutomaticPoints = false;
                return copy;
            }

            [[nodiscard]]
            auto automaticPoints() const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.AutomaticPoints = true;
                return copy;
            }

            //
            // Averaging on, over this many acquisitions. The count is not
            // optional here for the reason DSO8064AAcquisitionConfig gives:
            // "average, over however many you were last told" is not
            // reproducible.
            //
            // Note this is a different mechanism from AcquisitionMode::
            // HighResolution, which also averages -- that one averages the
            // samples within a single trigger's record, this one averages
            // across successive triggers. A single-shot capture can use the
            // first and cannot use the second, since there is only ever one
            // trigger.
            //
            [[nodiscard]]
            auto averagedOver( const unsigned count) const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.Averaging    = true;
                copy.mConfig.AverageCount = count;
                return copy;
            }

            [[nodiscard]]
            auto unaveraged() const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.Averaging = false;
                return copy;
            }

            [[nodiscard]]
            auto sampleRate( const core::quantities::Frequency value) const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.SampleRate          = value;
                copy.mConfig.AutomaticSampleRate = false;
                return copy;
            }

            [[nodiscard]]
            auto automaticSampleRate() const -> DSO8064AAcquisitionBuilder
            {
                auto copy = *this;
                copy.mConfig.AutomaticSampleRate = true;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    //
    // The per-channel chain.
    //
    // Note what it holds: the instrument and a channel *number*, never a
    // DSO8064AChannel<N>. That is the same dangling-reference lesson
    // core::Port learned (see DSO8064AChannel's own comment below) --
    // `Osc1.channel<3>().input( ... )` produces a temporary channel view
    // which is gone by the end of the full expression, well before Setup gets
    // its hands on the config.
    //
    class DSO8064AChannelBuilder
    {
        public:
            using Config = DSO8064AChannelConfig;

            DSO8064AChannelBuilder( DSO8064A & instrument, const unsigned channel) :
                mConfig{ instrument, channel, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto input( const ChannelInput value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.Input = value;
                return copy;
            }

            [[nodiscard]]
            auto voltsPerDivision( const core::quantities::Voltage value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.VoltsPerDivision = value;
                return copy;
            }

            [[nodiscard]]
            auto verticalOffset( const core::quantities::Voltage value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.VerticalOffset = value;
                return copy;
            }

            [[nodiscard]]
            auto bandwidth( const Bandwidth value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.BandwidthLimit = value;
                return copy;
            }

            [[nodiscard]]
            auto probeAdapter( const ProbeAdapter value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.Probe = value;
                return copy;
            }

            [[nodiscard]]
            auto display( const ChannelDisplay value) const -> DSO8064AChannelBuilder
            {
                auto copy = *this;
                copy.mConfig.Display = value;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    class DSO8064AWaveformBuilder
    {
        public:
            using Config = DSO8064AWaveformConfig;

            DSO8064AWaveformBuilder( DSO8064A & instrument, const unsigned channel) :
                mConfig{ instrument, channel }
            {}

            //
            // No setters at all, and that is the point rather than an omission
            // -- see DSO8064AWaveformConfig on why a transfer has nothing left
            // to decide. It is still a builder rather than a bare config so
            // that Fetch's argument reads like every other verb's, and so that
            // the day this instrument grows something worth naming here (a
            // sub-range of the record, a segment index) it grows a method
            // rather than a new type.
            //
            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    class DSO8064ASingleBuilder
    {
        public:
            using Config = DSO8064ASingleConfig;

            explicit DSO8064ASingleBuilder( DSO8064A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto timeout( const core::quantities::Time value) const -> DSO8064ASingleBuilder
            {
                auto copy = *this;
                copy.mConfig.Timeout = value;
                return copy;
            }

            [[nodiscard]]
            auto armTimeout( const core::quantities::Time value) const -> DSO8064ASingleBuilder
            {
                auto copy = *this;
                copy.mConfig.ArmTimeout = value;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    //
    // One of the DSO8064A's four physical input channels, narrowed to at
    // compile time via DSO8064A::channel<N>() -- resolves the "Channel
    // selection" item this file's older comment deferred, the same way
    // DcP1..DcP4 resolve N6701A's four module slots: a real hardware axis
    // modeled as its own compile-time fact rather than an implicit,
    // whichever-was-last-selected mode.
    //
    // It is the one place a channel number is written in a script, and it
    // serves both things a script does to a channel -- configure it and
    // measure through it:
    //
    //     Setup(   Osc1.channel<3>().input( ChannelInput::Dc1M).voltsPerDivision( 100_mV));
    //     Measure( Osc1.channel<3>().vmin(), at( dut::Vout));
    //
    // Note there is deliberately no config() here, only on the builder each
    // setting method returns. So `Setup( Osc1.channel<3>())` -- a Setup that
    // names a channel and no setting, which can only be a mistake -- is "no
    // matching function" rather than a call that does nothing.
    //
    // Deliberately a thin, transient view over DSO8064A&, never itself
    // retained by anything returned from it: every measurement method
    // (vpp(), riseTime(), ...) records N onto the real DSO8064A instance --
    // via setChannel(), the same way it already records which mode via
    // setMode() -- and then hands back a core::Port<Q, DSO8064A> bound
    // directly to that real, singular instrument, not to this view. That
    // is deliberate and load-bearing: an earlier version of this file had
    // Port referencing DSO8064AChannel<N> itself, which meant a Port
    // obtained via `osc1.channel<3>().vmax()` and then stored past the
    // full expression that created it held a reference to a temporary
    // that no longer existed -- silent dangling-reference UB, caught by a
    // test, not by the compiler. Binding Port straight to DSO8064A& removes
    // the dangling risk entirely, at the cost of channel now being
    // instrument-level mutable state -- but that is exactly the same
    // accepted sharp edge Mode already has (see DSO8064A's own comment): a
    // Port handle read after a later channel<M>() switch reads whichever
    // channel is current at rawMeasure() time, not the channel selected
    // when the handle was obtained. One documented sharp edge is a better
    // trade than a second, worse, undocumented one.
    //
    // The setting methods have no such edge, because they carry the channel
    // number into the config by value rather than leaving it on the
    // instrument -- see DSO8064AChannelBuilder.
    //
    template<unsigned N>
        requires ValidDso8064aChannel<N>
    class DSO8064AChannel
    {
        public:
            static constexpr unsigned Number = N;

            explicit DSO8064AChannel( DSO8064A & instrument) : mInstrument( instrument) {}

            // --- Configuring this channel: hands off to the builder ---

            [[nodiscard]] auto input( ChannelInput value) const -> DSO8064AChannelBuilder;
            [[nodiscard]] auto voltsPerDivision( core::quantities::Voltage value) const -> DSO8064AChannelBuilder;
            [[nodiscard]] auto verticalOffset( core::quantities::Voltage value) const -> DSO8064AChannelBuilder;
            [[nodiscard]] auto bandwidth( Bandwidth value) const -> DSO8064AChannelBuilder;
            [[nodiscard]] auto probeAdapter( ProbeAdapter value) const -> DSO8064AChannelBuilder;
            [[nodiscard]] auto display( ChannelDisplay value) const -> DSO8064AChannelBuilder;

            //
            // The amplitude family -- the :MEASure:V... subset that answers
            // questions about levels. Each records both N (setChannel) and its
            // own mode (setMode) onto the real instrument before returning a
            // Port bound to that same instrument -- see this class's own
            // comment for why Port never references DSO8064AChannel itself.
            //
            // vbase()/vtop() are not vmin()/vmax(), and the difference is the
            // one that matters for transient work. Min and max are the extreme
            // samples in the record, so a single spike moves them. Base and
            // top are the settled levels the waveform spends its time at, so
            // they are not moved by the spike at all -- which makes
            // vbase() - vmin() the size of a negative transient measured
            // against the rail it departed from.
            //
            // That is the calculation the legacy ATE script performed by hand,
            // out of a screen-median reading and a manually subtracted vertical
            // offset. Both instruments could always answer it directly.
            //
            [[nodiscard]] auto vpp()        -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vmax()       -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vmin()       -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vrms()       -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vaverage()   -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vbase()      -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vtop()       -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vamplitude() -> core::Port<core::quantities::Voltage, DSO8064A>;
            [[nodiscard]] auto vmiddle()    -> core::Port<core::quantities::Voltage, DSO8064A>;

            //
            // The timing family. frequency()/period()/positiveWidth()/
            // negativeWidth() need nothing beyond "which channel";
            // riseTime()/fallTime() also carry the usual 10%/90% edge-timing
            // thresholds as a MeasureSetup, defaulted here so a bare
            // `.riseTime()` is still a complete, valid reading, and
            // overridable via the same chained-builder spelling as
            // core::Port's range()/nplc() --
            // `.riseTime().lowThreshold( 0.2).highThreshold( 0.8)`.
            //
            // Still not modeled: duty cycle, overshoot and preshoot. All three
            // are dimensionless ratios, and this framework has no
            // dimensionless quantity to return them as -- core::quantities has
            // PowerFactor and nothing else without a unit, and borrowing it
            // for a duty cycle would make "0.45 of a power factor" the thing a
            // criterion compares against. That is a core question (a Ratio
            // quantity, or a criterion over a bare double), not a driver one,
            // and inventing an answer here would put it in the wrong file.
            //
            [[nodiscard]] auto frequency()      -> core::Port<core::quantities::Frequency, DSO8064A>;
            [[nodiscard]] auto period()         -> core::Port<core::quantities::Time, DSO8064A>;
            [[nodiscard]] auto riseTime()       -> core::Port<core::quantities::Time, DSO8064A>;
            [[nodiscard]] auto fallTime()       -> core::Port<core::quantities::Time, DSO8064A>;
            [[nodiscard]] auto positiveWidth()  -> core::Port<core::quantities::Time, DSO8064A>;
            [[nodiscard]] auto negativeWidth()  -> core::Port<core::quantities::Time, DSO8064A>;

            //
            // The whole captured record off this channel, for Fetch -- see
            // core/trace.hpp.
            //
            // Not a Port and not a Measure, which is the distinction worth
            // drawing here rather than in core: everything above answers one
            // number about this channel and reaches the DUT point named at the
            // Measure call, with the route closed and reopened around it. A
            // trace is already inside the instrument, arrived over whatever
            // route the capture was taken on, and is not a quantity a criterion
            // can be pointed at (see core::Waveform on what a script does with
            // one instead).
            //
            // Unlike the measurement methods above, this does NOT switch the
            // instrument's mode or selected channel -- the channel travels in
            // the config by value, so the sharp edge those fifteen carry (a
            // handle taken before a later channel switch reads the later
            // channel) does not exist here. Same as the setting builders.
            //
            [[nodiscard]] auto waveform() const -> DSO8064AWaveformBuilder;

        private:
            DSO8064A & mInstrument;
    };

    //
    // Agilent/Keysight Infiniium DSO8064A: this rig's actual scope -- four
    // channels, 600 MHz -- replacing the old generic hal::Oscilloscope
    // placeholder (see that class's own comment, still in hal/instrument.hpp,
    // for why it existed) now that the real model is known, the same
    // retirement hal::L4411A gave the old generic hal::Dmm.
    //
    // What it does, in the four groups a script uses:
    //
    //   Setup  -- :TRIGger, :TIMebase, :ACQuire instrument-wide, and
    //             :CHANnel<N> per input. Four builders, one per subsystem;
    //             see the config types above for why they are four and not
    //             one.
    //   Arm /  -- :SINGle, bracketing whatever event the script causes.
    //   Await     See core/acquire.hpp, which is where the verbs live and why
    //             they are generic rather than scope-specific.
    //   Measure-- the :MEASure amplitude and timing families, channel-scoped
    //             via channel<N>()/DSO8064AChannel above.
    //   Fetch  -- :WAVeform, the whole captured record off one channel rather
    //             than one number measured from it. See core/trace.hpp for the
    //             verb and core::Waveform for what comes back.
    //
    // Calling one of the Mode-tagged measurement methods switches the
    // instrument's current measurement mode *and* channel, the same way a
    // real Infiniium's front-panel Measure menu switches which readout (and
    // which channel) is active before a reading is taken, mirroring
    // hal::L4411A's own Mode-switch pattern. Same accepted sharp edge as
    // hal::L4411A's AC/DC mode: a port handle read after a later mode or
    // channel switch reads whichever mode/channel is current at
    // rawMeasure() time, not whichever was active when the handle was
    // obtained -- harmless for Measure(port, at(...))'s
    // read-immediately-and-discard usage.
    //
    // Waveform transfer was deferred until the recording format could carry
    // one, which is the right order and worth recording: an observation this
    // framework could not replay would have been the one hole in --replay. It
    // can now -- a trace row carries its unit and timebase and refers its
    // samples to a file beside the recording (see core/recording.hpp) -- so
    // the verb exists.
    //
    // Still deliberately deferred:
    //   - Segmented acquisition -- see AcquisitionMode::Segmented.
    //   - Duty cycle, overshoot, preshoot -- see DSO8064AChannel's own
    //     comment on why these wait on a core decision, not a driver one.
    //   - The trigger kinds beyond edge (:TRIGger:MODE GLITch / ADVanced,
    //     and the pattern, state, delay, TV and violation modes under it).
    //     Edge is what this bench triggers on; the others are a large surface
    //     each with its own parameter set, and speculatively modelling them
    //     would mean maintaining settings nothing on this rig can exercise.
    //
    class DSO8064A : public InstrumentTag
    {
        public:
            static constexpr unsigned channel_count = 4;

            enum class Mode
            {
                Vpp, Vmax, Vmin, Vrms, Vaverage, Vbase, Vtop, Vamplitude, Vmiddle,
                Frequency, Period, RiseTime, FallTime, PositiveWidth, NegativeWidth
            };

            //
            // How long Await polls for the capture to complete, and Arm for
            // the scope to report itself armed, when the script does not say.
            //
            // Both are stated here rather than left implicit because a
            // default timeout is a real decision about how a failing test
            // behaves: too short and a slow DUT reports a transient that was
            // there, too long and a suite hangs on a DUT that is simply dead.
            // The capture default is the more generous of the two for that
            // reason -- it is waiting on the device under test, where arming
            // is waiting only on the instrument.
            //
            static constexpr core::quantities::Time kDefaultCaptureTimeout{ 5.0 };
            static constexpr core::quantities::Time kDefaultArmTimeout{ 1.0 };

            //
            // GPIB, LAN or USB -- all three are on this model's back panel,
            // and which one a given rig cabled is a rig fact rather than a
            // driver one (see hal::ReachableOver in hal/address.hpp, and
            // rig/instrument.inc for this repo's choice).
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Gpib, Lan, Usb>
            DSO8064A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            // Where the PC reaches this scope -- see hal/address.hpp.
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // Narrows to one of the instrument's four physical channels --
            // channel<5>() or channel<0>() simply has no valid
            // instantiation (ValidDso8064aChannel), a hard compile error,
            // not a runtime range check. See DSO8064AChannel's own comment
            // for why this returns a transient view rather than
            // transferring ownership, and for why Port never ends up
            // referencing that view.
            //
            template<unsigned N>
                requires ValidDso8064aChannel<N>
            [[nodiscard]]
            auto channel() -> DSO8064AChannel<N>
            {
                return DSO8064AChannel<N>{ *this };
            }

            // --- The instrument-wide builders ---

            [[nodiscard]]
            auto trigger() -> DSO8064ATriggerBuilder
            {
                return DSO8064ATriggerBuilder{ *this };
            }

            [[nodiscard]]
            auto timebase() -> DSO8064ATimebaseBuilder
            {
                return DSO8064ATimebaseBuilder{ *this };
            }

            [[nodiscard]]
            auto acquisition() -> DSO8064AAcquisitionBuilder
            {
                return DSO8064AAcquisitionBuilder{ *this };
            }

            //
            // The single-shot capture, for Arm and Await -- named after the
            // instrument's own :SINGle rather than after what the verbs do
            // with it, so that a reader holding the programmer's reference
            // recognises it. Which of the two verbs is being invoked is what
            // says whether this is arming or waiting.
            //
            [[nodiscard]]
            auto single() -> DSO8064ASingleBuilder
            {
                return DSO8064ASingleBuilder{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto channelNumber() const -> unsigned
            {
                return mChannel;
            }

            //
            // Nothing to do -- a scope is passive, exactly as hal::L4411A
            // is; see that class's safe() for why this is written out as an
            // explicit empty body rather than simply left absent.
            //
            // Note this deliberately does not reset mMode/mChannel, nor any
            // of the settings a Setup left behind. They are instrument state
            // a script set (see this class's own comment on the
            // mode-switch-on-the-instrument pattern), not anything that can
            // energise the DUT -- and safing runs when a script has already
            // died, so there is nobody left to surprise with a mode change.
            // Resetting them would only discard the last thing the scope was
            // told to look at, which is the one piece of state worth still
            // being able to read afterwards.
            //
            // The armed flag is the one thing that IS cleared, and for a
            // reason the others do not share: it is not a setting but a
            // pending expectation. A scope left armed after a script died is
            // waiting for an event that is no longer coming, and the next
            // script's Await would be answered by it.
            //
            auto safe() -> void
            {
                mArmed = false;
            }

            // Switches the instrument's current measurement mode/channel --
            // called by DSO8064AChannel's builder methods (vpp()/vmax()/.../
            // riseTime()/...), never by a script directly.
            auto setMode( const Mode mode) -> void
            {
                mMode = mode;
            }

            auto setChannel( const unsigned channel) -> void
            {
                mChannel = channel;
            }

            // --- What the ADL customization points below actually call ---

            //
            // Four configure methods, one per subsystem, and each one a loop
            // of ifs rather than a run of value_or. That is the same choice
            // hal::Racal1260::configurePort makes and for the same reason: an
            // unset field means "leave what is already configured", so a
            // Setup naming only the trigger level must not reset the slope to
            // some default the builder never chose.
            //
            auto configureTrigger( const DSO8064ATriggerConfig & config) -> void
            {
                if( config.EdgeSource) { mTriggerSource   = config.EdgeSource; }
                if( config.Slope)      { mTriggerSlope    = config.Slope;      }
                if( config.Level)      { mTriggerLevel    = config.Level;      }
                if( config.Sweep)      { mTriggerSweep    = config.Sweep;      }
                if( config.Coupling)   { mTriggerCoupling = config.Coupling;   }
                if( config.Holdoff)    { mTriggerHoldoff  = config.Holdoff;    }
            }

            auto configureTimebase( const DSO8064ATimebaseConfig & config) -> void
            {
                if( config.TimePerDivision) { mTimePerDivision   = config.TimePerDivision; }
                if( config.Position)        { mTimebasePosition  = config.Position;        }
                if( config.Reference)       { mTimebaseReference = config.Reference;       }
            }

            auto configureAcquisition( const DSO8064AAcquisitionConfig & config) -> void
            {
                if( config.Mode)                { mAcquisitionMode    = config.Mode;                }
                if( config.Points)              { mPoints             = config.Points;              }
                if( config.AutomaticPoints)     { mAutomaticPoints    = config.AutomaticPoints;     }
                if( config.Averaging)           { mAveraging          = config.Averaging;           }
                if( config.AverageCount)        { mAverageCount       = config.AverageCount;        }
                if( config.SampleRate)          { mSampleRate         = config.SampleRate;          }
                if( config.AutomaticSampleRate) { mAutomaticSampleRate = config.AutomaticSampleRate; }
            }

            auto configureChannel( const DSO8064AChannelConfig & config) -> void
            {
                auto & data = atChannel( config.Channel);

                if( config.Input)            { data.Input            = config.Input;            }
                if( config.VoltsPerDivision) { data.VoltsPerDivision = config.VoltsPerDivision; }
                if( config.VerticalOffset)   { data.VerticalOffset   = config.VerticalOffset;   }
                if( config.BandwidthLimit)   { data.BandwidthLimit   = config.BandwidthLimit;   }
                if( config.Probe)            { data.Probe            = config.Probe;            }
                if( config.Display)          { data.Display          = config.Display;          }
            }

            //
            // Arm: on real hardware, :STOP and *OPC? to settle, :ADER? to
            // clear any stale acquisition-done event, :SINGle to arm, then
            // poll :AER? until the scope reports itself armed and ready or
            // the arm timeout runs out.
            //
            // Simulated here, so what survives is the state that ordering
            // produces: armed, with no completed acquisition behind it. The
            // clearing is not incidental -- an Await that could be answered
            // by the *previous* capture's done event is precisely the bug the
            // manual's ":ADER? // clear ADER event" line exists to prevent.
            //
            auto armSingle( const DSO8064ASingleConfig &) -> void
            {
                mArmed     = true;
                mCompleted = false;
            }

            //
            // Await: poll :ADER? until it reads 1 or the timeout runs out.
            //
            // Awaiting something that was never armed answers false rather
            // than throwing, and that is a deliberate reading of what the
            // mistake is. A script that measures a transient without having
            // armed a capture has not crashed -- it has measured whatever was
            // left in the acquisition buffer, which is a wrong answer, and
            // the check that this Await gates is exactly where a wrong answer
            // should be caught. Throwing would abandon the rest of the run
            // over a script bug the run itself is capable of reporting.
            //
            [[nodiscard]]
            auto awaitAcquisition( const DSO8064ASingleConfig &) -> bool
            {
                if( !mArmed)
                {
                    return false;
                }

                mArmed     = false;
                mCompleted = mCaptureCompletes;

                return mCompleted;
            }

            //
            // Fetch: on real hardware, :WAVeform:SOURce CHANnel<N>, then
            // :WAVeform:PREamble? for the scaling and :WAVeform:DATA? for the
            // block, with the raw levels turned into volts against seconds
            // here -- (raw - yReference) * yIncrement + yOrigin, and the x
            // pair carried straight into core::Waveform::Timing.
            //
            // The scaling belongs here and not one layer up, which is what
            // core::Waveform stores values in units for: a raw level is a fact
            // about this digitiser at this vertical setting, and this driver
            // is the only thing that knows the encoding. A recording holding
            // raw levels would be unreadable without the instrument that wrote
            // it.
            //
            // Simulated here, so what comes back is whatever
            // setSimulatedTrace() put there. Fetching a channel nothing was
            // put on answers an empty trace rather than throwing, for the
            // reason awaitAcquisition answers false rather than throwing: a
            // script that reads out a record it never captured has not crashed,
            // and the check it feeds is where that should surface.
            //
            [[nodiscard]]
            auto fetchWaveform( const DSO8064AWaveformConfig & config) -> core::Waveform
            {
                return atChannel( config.Channel).Trace;
            }

            // --- Test/simulation hooks -- real hardware has no such setters ---
            //
            // Channel is a plain runtime unsigned here (1-4): this is test
            // scaffolding setting up canned data, not the compile-time-
            // checked script-facing surface channel<N>() provides.

            auto setSimulatedVpp( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vpp = v;
            }

            auto setSimulatedTrace( const unsigned channel, core::Waveform trace) -> void
            {
                atChannel( channel).Trace = std::move( trace);
            }

            auto setSimulatedVmax( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmax = v;
            }

            auto setSimulatedVmin( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmin = v;
            }

            auto setSimulatedVrms( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vrms = v;
            }

            auto setSimulatedVaverage( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vaverage = v;
            }

            auto setSimulatedVbase( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vbase = v;
            }

            auto setSimulatedVtop( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vtop = v;
            }

            auto setSimulatedVamplitude( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vamplitude = v;
            }

            auto setSimulatedVmiddle( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmiddle = v;
            }

            auto setSimulatedFrequency( const unsigned channel, const core::quantities::Frequency f) -> void
            {
                atChannel( channel).Freq = f;
            }

            auto setSimulatedPeriod( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).Period = t;
            }

            auto setSimulatedRiseTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).RiseTime = t;
            }

            auto setSimulatedFallTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).FallTime = t;
            }

            auto setSimulatedPositiveWidth( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).PositiveWidth = t;
            }

            auto setSimulatedNegativeWidth( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).NegativeWidth = t;
            }

            //
            // Makes one measurement on one channel report itself unmeasurable
            // -- what a real scope does when the trace does not support the
            // question being asked of it.
            //
            // Per (channel, measurement) rather than per channel, because
            // that is how the instrument behaves: a clipped trace still has a
            // perfectly good period, and a flat trace with no edge on it has
            // a vmax and no rise time. A single "this channel is broken" flag
            // could not express either case, and a test written against it
            // would be testing something the hardware never does.
            //
            auto setSimulatedUnmeasurable( const unsigned channel, const Mode mode, const MeasurementFault fault) -> void
            {
                atChannel( channel).Faults.at( static_cast<std::size_t>( mode)) = fault;
            }

            auto clearSimulatedUnmeasurable( const unsigned channel, const Mode mode) -> void
            {
                atChannel( channel).Faults.at( static_cast<std::size_t>( mode)) = std::nullopt;
            }

            //
            // Whether the next armed capture completes. Defaults to true --
            // the happy path, so a test that does not care about capture
            // failure says nothing about it -- and set false to exercise the
            // timeout branch.
            //
            auto setSimulatedCaptureCompletes( const bool completes) -> void
            {
                mCaptureCompletes = completes;
            }

            [[nodiscard]]
            auto isArmed() const -> bool
            {
                return mArmed;
            }

            [[nodiscard]]
            auto lastAcquisitionCompleted() const -> bool
            {
                return mCompleted;
            }

            // --- Settings readback, for tests and for describeConfig ---

            [[nodiscard]] auto triggerSource() const     -> std::optional<unsigned>          { return mTriggerSource;   }
            [[nodiscard]] auto triggerSlope() const      -> std::optional<TriggerSlope>      { return mTriggerSlope;    }
            [[nodiscard]] auto triggerLevel() const      -> std::optional<core::quantities::Voltage> { return mTriggerLevel; }
            [[nodiscard]] auto triggerSweep() const      -> std::optional<TriggerSweep>      { return mTriggerSweep;    }
            [[nodiscard]] auto triggerCoupling() const   -> std::optional<TriggerCoupling>   { return mTriggerCoupling; }
            [[nodiscard]] auto triggerHoldoff() const    -> std::optional<core::quantities::Time> { return mTriggerHoldoff; }

            [[nodiscard]] auto timePerDivision() const   -> std::optional<core::quantities::Time> { return mTimePerDivision; }
            [[nodiscard]] auto timebasePosition() const  -> std::optional<core::quantities::Time> { return mTimebasePosition; }
            [[nodiscard]] auto timebaseReference() const -> std::optional<TimebaseReference> { return mTimebaseReference; }

            [[nodiscard]] auto acquisitionMode() const   -> std::optional<AcquisitionMode>   { return mAcquisitionMode; }
            [[nodiscard]] auto points() const            -> std::optional<unsigned>          { return mPoints;          }
            [[nodiscard]] auto automaticPoints() const   -> std::optional<bool>              { return mAutomaticPoints; }
            [[nodiscard]] auto averaging() const         -> std::optional<bool>              { return mAveraging;       }
            [[nodiscard]] auto averageCount() const      -> std::optional<unsigned>          { return mAverageCount;    }

            [[nodiscard]] auto channelInput( const unsigned channel) -> std::optional<ChannelInput>
            {
                return atChannel( channel).Input;
            }

            [[nodiscard]] auto voltsPerDivision( const unsigned channel) -> std::optional<core::quantities::Voltage>
            {
                return atChannel( channel).VoltsPerDivision;
            }

            [[nodiscard]] auto verticalOffset( const unsigned channel) -> std::optional<core::quantities::Voltage>
            {
                return atChannel( channel).VerticalOffset;
            }

            [[nodiscard]] auto channelBandwidth( const unsigned channel) -> std::optional<Bandwidth>
            {
                return atChannel( channel).BandwidthLimit;
            }

            [[nodiscard]] auto probeAdapter( const unsigned channel) -> std::optional<ProbeAdapter>
            {
                return atChannel( channel).Probe;
            }

            [[nodiscard]] auto channelDisplay( const unsigned channel) -> std::optional<ChannelDisplay>
            {
                return atChannel( channel).Display;
            }

            //
            // The one rawMeasure() Port<Q, DSO8064A> actually calls --
            // mChannel/mMode are already instrument state by the time this
            // runs (set by whichever DSO8064AChannel<N> builder method
            // produced the Port), so this needs no channel argument of its
            // own, exactly the same shape hal::L4411A's rawMeasure() has.
            //
            // Throws core::UnmeasurableReading when the instrument would have
            // answered 9.99999E+37 -- see MeasurementFault above for what that
            // means, and core::MeasureEngine for what catches it. Throwing
            // rather than returning a sentinel is what keeps every driver's
            // rawMeasure() returning the quantity it says it returns: a
            // sentinel would be a magic number that every caller had to know
            // to test for, which is exactly the ISINVALID() arrangement this
            // replaces.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                const auto & data = atChannel( mChannel);

                if( const auto & fault = data.Faults.at( static_cast<std::size_t>( mMode)))
                {
                    throw core::UnmeasurableReading( to_string( *fault));
                }

                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    switch( mMode)
                    {
                        case Mode::Vpp:        return data.Vpp;
                        case Mode::Vmax:       return data.Vmax;
                        case Mode::Vmin:       return data.Vmin;
                        case Mode::Vrms:       return data.Vrms;
                        case Mode::Vaverage:   return data.Vaverage;
                        case Mode::Vbase:      return data.Vbase;
                        case Mode::Vtop:       return data.Vtop;
                        case Mode::Vamplitude: return data.Vamplitude;
                        case Mode::Vmiddle:    return data.Vmiddle;
                        default:               return data.Vpp;
                    }
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return data.Freq;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Time>)
                {
                    switch( mMode)
                    {
                        case Mode::Period:        return data.Period;
                        case Mode::RiseTime:      return data.RiseTime;
                        case Mode::FallTime:      return data.FallTime;
                        case Mode::PositiveWidth: return data.PositiveWidth;
                        case Mode::NegativeWidth: return data.NegativeWidth;
                        default:                  return data.Period;
                    }
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "DSO8064A has no port for this quantity");
                }
            }

        private:
            //
            // How many measurements a channel can be made to report as
            // unmeasurable -- derived from Mode's own enumerators by
            // reflection rather than written down beside them (see
            // core/meta.hpp). Adding a measurement to Mode therefore cannot
            // leave this array a size too short, because there is nothing to
            // forget to update.
            //
            static constexpr std::size_t kModeCount = core::meta::values<Mode>.size();

            struct ChannelData
            {
                core::quantities::Voltage    Vpp{};
                core::quantities::Voltage    Vmax{};
                core::quantities::Voltage    Vmin{};
                core::quantities::Voltage    Vrms{};
                core::quantities::Voltage    Vaverage{};
                core::quantities::Voltage    Vbase{};
                core::quantities::Voltage    Vtop{};
                core::quantities::Voltage    Vamplitude{};
                core::quantities::Voltage    Vmiddle{};
                core::quantities::Frequency  Freq{};
                core::quantities::Time       Period{};
                core::quantities::Time       RiseTime{};
                core::quantities::Time       FallTime{};
                core::quantities::Time       PositiveWidth{};
                core::quantities::Time       NegativeWidth{};

                // What a Setup left on this input, and which of its
                // measurements the instrument would refuse to answer.
                std::optional<ChannelInput>               Input;
                std::optional<core::quantities::Voltage>  VoltsPerDivision;
                std::optional<core::quantities::Voltage>  VerticalOffset;
                std::optional<Bandwidth>                  BandwidthLimit;
                std::optional<ProbeAdapter>               Probe;
                std::optional<ChannelDisplay>             Display;

                std::array<std::optional<MeasurementFault>, kModeCount> Faults{};

                // What :WAVeform:DATA? would hand back for this input.
                core::Waveform Trace{};
            };

            [[nodiscard]]
            auto atChannel( const unsigned channel) -> ChannelData &
            {
                return mChannels.at( channel - 1);
            }

            InstrumentId               mId;
            Address                    mAddress;
            Mode                       mMode{ Mode::Vpp};
            unsigned                   mChannel{ 1};
            std::array<ChannelData, channel_count> mChannels;

            std::optional<unsigned>                     mTriggerSource;
            std::optional<TriggerSlope>                 mTriggerSlope;
            std::optional<core::quantities::Voltage>    mTriggerLevel;
            std::optional<TriggerSweep>                 mTriggerSweep;
            std::optional<TriggerCoupling>              mTriggerCoupling;
            std::optional<core::quantities::Time>       mTriggerHoldoff;

            std::optional<core::quantities::Time>       mTimePerDivision;
            std::optional<core::quantities::Time>       mTimebasePosition;
            std::optional<TimebaseReference>            mTimebaseReference;

            std::optional<AcquisitionMode>              mAcquisitionMode;
            std::optional<unsigned>                     mPoints;
            std::optional<bool>                         mAutomaticPoints;
            std::optional<bool>                         mAveraging;
            std::optional<unsigned>                     mAverageCount;
            std::optional<core::quantities::Frequency>  mSampleRate;
            std::optional<bool>                         mAutomaticSampleRate;

            bool                                        mArmed{ false };
            bool                                        mCompleted{ false };
            bool                                        mCaptureCompletes{ true };
    };

    //
    // ---------------------------------------------------------------------
    // DSO8064AChannel<N>, out of line
    // ---------------------------------------------------------------------
    //
    // Below DSO8064A rather than inside the class template, because every one
    // of these needs the instrument to be a complete type.
    //

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::input( const ChannelInput value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.input( value);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::waveform() const -> DSO8064AWaveformBuilder
    {
        return DSO8064AWaveformBuilder{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::voltsPerDivision( const core::quantities::Voltage value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.voltsPerDivision( value);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::verticalOffset( const core::quantities::Voltage value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.verticalOffset( value);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::bandwidth( const Bandwidth value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.bandwidth( value);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::probeAdapter( const ProbeAdapter value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.probeAdapter( value);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::display( const ChannelDisplay value) const -> DSO8064AChannelBuilder
    {
        return DSO8064AChannelBuilder{ mInstrument, N }.display( value);
    }

    //
    // The measurement family. Every one of these is the same three lines --
    // record the channel, record the mode, hand back a Port on the real
    // instrument, qualified by which measurement it is.
    //
    // That qualifier is what stops fifteen different answers about one DUT pin
    // from sharing one session slot: a routed reading keys as
    // "Output5V.Vbase" rather than as "Output5V" (see core::MeasureEngine, and
    // core::Port::qualifiedBy for the mechanism). A DMM measuring a rail needs
    // no such thing -- there is only one voltage at a pin -- which is why the
    // qualifier is opt-in and this is the driver that opts in.
    //
    // The list is generated from a macro rather than written out fifteen
    // times, which is a reversal of the choice this file used to make and
    // worth saying why. Written out, each body was four lines of which three
    // were identical, and the qualifier above would have been a fourth thing
    // to remember to spell correctly per measurement -- with a typo producing
    // not a compile error but a session key nothing injects against. The macro
    // makes the method name, the quantity and the mode one row, which is the
    // form the measurement surface is actually read in.
    //
#define THORIUM_DSO8064A_PORT( method, quantity, mode)                                                  \
    template<unsigned N>                                                                                \
        requires ValidDso8064aChannel<N>                                                                \
    auto DSO8064AChannel<N>::method() -> core::Port<core::quantities::quantity, DSO8064A>                \
    {                                                                                                   \
        mInstrument.setChannel( N);                                                                     \
        mInstrument.setMode( DSO8064A::Mode::mode);                                                     \
        return core::Port<core::quantities::quantity, DSO8064A>{ mInstrument }.qualifiedBy( #mode);       \
    }

    THORIUM_DSO8064A_PORT( vpp,           Voltage,   Vpp)
    THORIUM_DSO8064A_PORT( vmax,          Voltage,   Vmax)
    THORIUM_DSO8064A_PORT( vmin,          Voltage,   Vmin)
    THORIUM_DSO8064A_PORT( vrms,          Voltage,   Vrms)
    THORIUM_DSO8064A_PORT( vaverage,      Voltage,   Vaverage)
    THORIUM_DSO8064A_PORT( vbase,         Voltage,   Vbase)
    THORIUM_DSO8064A_PORT( vtop,          Voltage,   Vtop)
    THORIUM_DSO8064A_PORT( vamplitude,    Voltage,   Vamplitude)
    THORIUM_DSO8064A_PORT( vmiddle,       Voltage,   Vmiddle)
    THORIUM_DSO8064A_PORT( frequency,     Frequency, Frequency)
    THORIUM_DSO8064A_PORT( period,        Time,      Period)
    THORIUM_DSO8064A_PORT( positiveWidth, Time,      PositiveWidth)
    THORIUM_DSO8064A_PORT( negativeWidth, Time,      NegativeWidth)

#undef THORIUM_DSO8064A_PORT

    //
    // riseTime()/fallTime() are written out rather than going through the
    // macro above, because they are the two that carry a MeasureSetup: they
    // seed the usual 10%/90% thresholds up front, so a bare `.riseTime()` is
    // a complete reading -- same "still valid, if underspecified" shape as
    // N6701ABuilder's voltage()/currentLimit(), just pre-filled here rather
    // than left at nullopt, since 10%/90% is the de-facto default on real
    // scopes rather than genuinely optional.
    //
    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::riseTime() -> core::Port<core::quantities::Time, DSO8064A>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064A::Mode::RiseTime);
        return core::Port<core::quantities::Time, DSO8064A>{ mInstrument }
                   .qualifiedBy( "RiseTime").lowThreshold( 0.1).highThreshold( 0.9);
    }

    template<unsigned N>
        requires ValidDso8064aChannel<N>
    auto DSO8064AChannel<N>::fallTime() -> core::Port<core::quantities::Time, DSO8064A>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSO8064A::Mode::FallTime);
        return core::Port<core::quantities::Time, DSO8064A>{ mInstrument }
                   .qualifiedBy( "FallTime").lowThreshold( 0.1).highThreshold( 0.9);
    }

    //
    // ---------------------------------------------------------------------
    // ADL customization points
    // ---------------------------------------------------------------------
    //
    // Four setupDriver overloads, one per config type -- see core/source.hpp
    // on why configuring is a verb of its own rather than a flavour of Apply.
    //
    // Note what this driver deliberately does NOT define: applyDriver and
    // removeDriver. A scope has no output to energise -- there is nothing an
    // Apply( Osc1.trigger()) could mean -- so Apply on this instrument is "no
    // matching function" at compile time, exactly the way it is on
    // hal::Racal1260's serial port and the way Connect( DcP1.dc()) is on a
    // supply with no isolation relay. The absence is the design.
    //
    // Nor connectDriver/disconnectDriver: this scope reaches its DUT point
    // through the matrix, and Measure closes and reopens that route around
    // each reading (see core::MeasureEngine). A script therefore never
    // Connects it -- it names the point at the Measure call instead.
    //
    inline auto setupDriver( const DSO8064ATriggerConfig & config) -> void
    {
        config.Instrument.configureTrigger( config);
    }

    inline auto setupDriver( const DSO8064ATimebaseConfig & config) -> void
    {
        config.Instrument.configureTimebase( config);
    }

    inline auto setupDriver( const DSO8064AAcquisitionConfig & config) -> void
    {
        config.Instrument.configureAcquisition( config);
    }

    inline auto setupDriver( const DSO8064AChannelConfig & config) -> void
    {
        config.Instrument.configureChannel( config);
    }

    //
    // ADL targets for core::ArmEngine and core::AwaitEngine -- the
    // triggered-acquisition pair, see core/acquire.hpp for why they are two
    // verbs and why Arm's post-condition is "armed and ready" rather than
    // "told to arm".
    //
    inline auto armDriver( const DSO8064ASingleConfig & config) -> void
    {
        config.Instrument.armSingle( config);
    }

    [[nodiscard]]
    inline auto awaitDriver( const DSO8064ASingleConfig & config) -> bool
    {
        return config.Instrument.awaitAcquisition( config);
    }

    //
    // ADL target for core::FetchEngine -- the trace verb, see core/trace.hpp.
    //
    [[nodiscard]]
    inline auto fetchDriver( const DSO8064AWaveformConfig & config) -> core::Waveform
    {
        return config.Instrument.fetchWaveform( config);
    }

    //
    // Which session slot a trace off this instrument files under, appended to
    // the instrument id: "Osc1.Channel3".
    //
    // Present because this scope has to have it. Four channels hold four
    // records at once, and the default "Osc1.Trace" would give all four one
    // slot -- so a test injecting a channel-1 trace would find a channel-3
    // Fetch taking it, and a recording of a run that captured two channels
    // would replay them into each other. Exactly the collision
    // core::Port::qualifiedBy prevents among this same scope's fifteen
    // measurements ("Output5V.Vbase" rather than a second "Output5V").
    //
    [[nodiscard]]
    inline auto traceQualifier( const DSO8064AWaveformConfig & config) -> std::string
    {
        return "Channel" + std::to_string( config.Channel);
    }

    //
    // ADL targets for the run journal -- see core/describe.hpp's own comment
    // on the describeConfig customization point.
    //
    // Field by field, and only the fields that were set, for the reason
    // hal::describeSetting exists: a Setup that named only the trigger level
    // is a different instruction from one that named the whole trigger, and a
    // rendering that filled in the rest would be inventing settings the
    // script never chose. Same reasoning hal::Racal1260's own describeConfig
    // gives about not collapsing framing to "9600 8N1".
    //
    inline auto describeConfig( const DSO8064ATriggerConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeCount(   "trigger.source",   config.EdgeSource),
                describeChoice(  "trigger.slope",    config.Slope),
                describeSetting( "trigger.level",    config.Level),
                describeChoice(  "trigger.sweep",    config.Sweep),
                describeChoice(  "trigger.coupling", config.Coupling),
                describeSetting( "trigger.holdoff",  config.Holdoff)
            })
        };
    }

    inline auto describeConfig( const DSO8064ATimebaseConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeSetting( "timebase.perDivision", config.TimePerDivision),
                describeSetting( "timebase.position",    config.Position),
                describeChoice(  "timebase.reference",   config.Reference)
            })
        };
    }

    inline auto describeConfig( const DSO8064AAcquisitionConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice(  "acquire.mode",       config.Mode),
                describeCount(   "acquire.points",     config.Points),
                //
                // The two automatic flags are rendered as the word rather
                // than as "=true"/"=false", because that is what they say: a
                // config with AutomaticPoints set to false was produced by
                // points( n) naming an explicit depth, and "pointsAuto=false"
                // beside "points=1000000" is the same fact twice in two
                // spellings.
                //
                config.AutomaticPoints.value_or( false)     ? std::string( "acquire.points=auto")     : std::string{},
                config.Averaging.value_or( false)           ? describeCount( "acquire.averages", config.AverageCount) : std::string{},
                config.Averaging.has_value() && !*config.Averaging ? std::string( "acquire.averaging=off") : std::string{},
                describeSetting( "acquire.sampleRate", config.SampleRate),
                config.AutomaticSampleRate.value_or( false) ? std::string( "acquire.sampleRate=auto") : std::string{}
            })
        };
    }

    inline auto describeConfig( const DSO8064AChannelConfig & config) -> core::SourceDescription
    {
        //
        // Every fragment carries the channel it belongs to, rather than the
        // channel being named once at the front. Two Setups on two channels
        // produce two log lines against the same InstrumentId, and a reader
        // scanning for "which channel was set to 100 mV/div" should not have
        // to carry a prefix in their head from the start of the line.
        //
        const auto prefix = "ch" + std::to_string( config.Channel) + ".";

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice(  prefix + "input",     config.Input),
                describeSetting( prefix + "perDiv",    config.VoltsPerDivision),
                describeSetting( prefix + "offset",    config.VerticalOffset),
                describeChoice(  prefix + "bandwidth", config.BandwidthLimit),
                describeChoice(  prefix + "probe",     config.Probe),
                describeChoice(  prefix + "display",   config.Display)
            })
        };
    }

    //
    // A trace's own line, which says only which channel it came off -- there is
    // nothing else in the config (see DSO8064AWaveformConfig on why). What the
    // trace *was* is the value column, and Fetch fills that with a summary
    // rather than the samples; see core::describeValue for a core::Waveform.
    //
    inline auto describeConfig( const DSO8064AWaveformConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            "ch" + std::to_string( config.Channel)
        };
    }

    inline auto describeConfig( const DSO8064ASingleConfig & config) -> core::SourceDescription
    {
        //
        // The defaults are rendered when the script did not name a timeout,
        // which is the one place in this file where an unset field is filled
        // in for the log. That is deliberate and specific to these two: a
        // timeout is the number that decides how a *failing* capture behaves,
        // so a log of a run that timed out has to say what it was waiting
        // for, and "the driver's default" is not an answer anyone reading a
        // report at 2am can act on.
        //
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "single.timeout=" + core::describeValue( config.Timeout.value_or( DSO8064A::kDefaultCaptureTimeout)),
                "single.armTimeout=" + core::describeValue( config.ArmTimeout.value_or( DSO8064A::kDefaultArmTimeout))
            })
        };
    }
} // namespace hal
