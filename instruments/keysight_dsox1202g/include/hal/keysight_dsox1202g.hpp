#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/driver/describe.hpp"
#include "core/meta.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"
#include "core/quantities/waveform.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all.
//
// Two, not three, although three is current: this driver uses hal::ConfigBuilder
// (which is what 2 added) and nothing from hal/io/, because it does not open a
// session yet -- it answers from its own simulation hooks exactly as
// hal::keysight_dso8064a::DSO8064A does. The number is the oldest hal this
// driver can be compiled against, not the newest one in the tree when it was
// written; asking for 3 would refuse a hal that serves this driver perfectly.
// It becomes 3 the day the transport below lands.
//
THORIUM_REQUIRE_HAL_API( 2);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. Everything here would
// otherwise collide with hal::keysight_dso8064a, which declares its own
// Bandwidth, TriggerSlope, TimebaseReference and six more under the same
// names: two scopes in one tree is precisely the case the nesting was
// introduced for, and this is the first tree to actually hold two.
//
// The name carries the manufacturer, as every driver package here does, and
// the model as Keysight spells it on the front panel: DSOX1202G, the
// 2-channel InfiniiVision 1000 X-Series scope with the built-in waveform
// generator.
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
    // hal::keysight_dso8064a::ChannelInput folds coupling and input impedance
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
    // (:SINGle, see SingleConfig below), not a sweep setting, and modelling it
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
    // was last told (see the configs below on what nullopt means), where
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
    // hal::keysight_dso8064a::MeasurementFault carries nineteen of the
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
    // ---------------------------------------------------------------------
    // What a Setup call on this instrument boils down to
    // ---------------------------------------------------------------------
    //
    // Four config types, one per SCPI subsystem this scope has -- :TRIGger,
    // :TIMebase, :ACQuire and :CHANnel<N> -- rather than one flattened "scope
    // settings" bag, for the reasons hal::keysight_dso8064a's own configs give:
    // each Setup call then says which part of the instrument it touches, and
    // the split matches how the settings actually interact.
    //
    // Every field is std::optional and unset means "leave whatever is already
    // configured" -- the same convention core::MeasureSetup uses on the
    // sensing side. A script that sets only the trigger level must not
    // silently reset the slope to whatever this file would have defaulted it
    // to.
    //

    //
    // :TRIGger -- when to capture.
    //
    // Source, slope and level travel together because on the instrument they
    // are not independent: the slope applies to whichever source
    // :TRIGger[:EDGE]:SOURce last selected, and the level is stored per
    // channel.
    //
    struct TriggerConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // Which channel the edge trigger watches. A plain unsigned here,
        // although the builder that sets it takes the channel number as a
        // template argument checked against ValidChannel -- the check happens
        // where the number is written, and what survives into the config is
        // the value.
        //
        // Channels only, deliberately. This instrument's edge trigger also
        // accepts EXTernal (the Ext Trig BNC), LINE (the mains) and WGEN (its
        // own generator's sync), and none of the three is wired on this rig:
        // rig/wiring.inc routes signals to the front BNCs and nothing else.
        // Modelling a source no bench here can reach would mean maintaining a
        // setting nothing can exercise -- the same argument the Infiniium
        // driver makes for leaving the non-edge trigger kinds out. The day a
        // rig cables the Ext Trig input, this field grows a source enum and
        // the builder grows externalSource().
        //
        std::optional<unsigned>                     EdgeSource{};
        std::optional<TriggerSlope>                 Slope{};
        std::optional<core::quantities::Voltage>    Level{};
        std::optional<TriggerSweep>                 Sweep{};
        std::optional<TriggerCoupling>              Coupling{};

        //
        // The reject filters, which are their own command here rather than two
        // more couplings -- see TriggerReject.
        //
        std::optional<TriggerReject>                Reject{};

        //
        // :TRIGger:HOLDoff, 60 ns to 10 s on this instrument.
        //
        std::optional<core::quantities::Time>       Holdoff{};
    };

    //
    // :TIMebase -- how much time the record covers, and where the trigger sits
    // within it.
    //
    struct TimebaseConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // :TIMebase:SCALe, seconds per division -- the number written on the
        // front panel and in every test spec, not :TIMebase:RANGe, which is
        // the same fact times ten. Both exist on the instrument; carrying both
        // here would have let a script set them to values that disagree.
        //
        std::optional<core::quantities::Time>       TimePerDivision{};

        //
        // :TIMebase:POSition -- time between the trigger event and the
        // reference point below. Positive delays the record after the trigger;
        // negative shows what preceded it.
        //
        std::optional<core::quantities::Time>       Position{};
        std::optional<TimebaseReference>            Reference{};
    };

    //
    // :ACQuire -- how the samples that make up the record are taken.
    //
    // Two fields, where the Infiniium's equivalent has seven, and every one of
    // the five missing ones is missing because this instrument does not have
    // the setting:
    //
    //   memory depth   :ACQuire:POINts? is query-only here. How deep the
    //                  record is follows from the timebase and the mode; there
    //                  is no :ACQuire:POINts command and no :POINts:AUTO to
    //                  turn off, so points() and automaticPoints() have
    //                  nothing to send. (:WAVeform:POINts, which does exist,
    //                  is a different fact -- how much of the record to
    //                  transfer -- and belongs to the fetch, see
    //                  WaveformConfig.)
    //   sample rate    :ACQuire:SRATe? is query-only for the same reason.
    //   averaging      one of the types now, not a flag beside them -- see
    //                  AcquisitionType.
    //
    // A script ported from the other scope loses those calls at compile time
    // rather than having them silently accepted and ignored, which is the
    // whole reason this is a separate driver.
    //
    struct AcquisitionConfig
    {
        DSOX1202G &                                 Instrument;
        std::optional<AcquisitionType>              Type{};

        //
        // :ACQuire:COUNt, 2 to 65536 -- how many acquisitions are averaged
        // together, and meaningful only when Type is Averaged. Set through
        // averagedOver() on the builder, which sets both, so that "average,
        // over however many you were last told" is not expressible: that count
        // is inherited instrument state, and a reproducible test must not
        // depend on it.
        //
        std::optional<unsigned>                     AverageCount{};
    };

    //
    // :CHANnel<N> -- one input, vertically.
    //
    struct ChannelConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // Which input this configures. Not optional, unlike everything below
        // it: a channel config with no channel is not an underspecified
        // instruction, it is not an instruction at all. Filled in by
        // Channel<N>, so a script can only ever produce one of these by naming
        // a channel that ValidChannel accepts.
        //
        unsigned                                    Channel;

        //
        // Named InputCoupling rather than Coupling, which is what it renders
        // as and what the builder method is called: a member whose name is
        // also the name of the type used to declare it changes the meaning of
        // that name inside the class, and is ill-formed. The trigger's own
        // coupling field escapes this only because its enum is TriggerCoupling.
        //
        std::optional<Coupling>                     InputCoupling{};

        //
        // :CHANnel<N>:SCALe -- volts per division, the front-panel number
        // again, and again not its :RANGe sibling for the reason
        // TimebaseConfig gives about the horizontal axis.
        //
        std::optional<core::quantities::Voltage>    VoltsPerDivision{};

        //
        // :CHANnel<N>:OFFSet -- the voltage represented at the centre of the
        // screen. Offsetting a small signal that sits on a large DC level is
        // what lets the vertical scale be turned up far enough to resolve it,
        // and every measurement the scope then makes is relative to the trace,
        // so the offset does not bias the answer.
        //
        std::optional<core::quantities::Voltage>    VerticalOffset{};

        std::optional<Bandwidth>                    BandwidthLimit{};

        //
        // :CHANnel<N>:PROBe -- the probe's attenuation ratio, and a plain
        // number rather than an enum.
        //
        // The Infiniium driver's ProbeAdapter names four external divider
        // adapters because that is what that command takes: an adapter, from a
        // fixed list, valid only against particular probe models. This command
        // takes a ratio -- "the probe attenuation factor may be 0.1 to 10000"
        // -- and 10 here means a 10:1 probe whoever made it. An enum would
        // have had to invent a name per ratio and would still not cover the
        // range.
        //
        // A double, therefore, and the range is not enforced: this driver
        // cannot know what is clipped to the end of the probe, so a ratio
        // outside the instrument's range is a settings-conflict error on the
        // bench rather than a compile error here. Same line the Infiniium
        // driver draws around its own probe setting, and the same reason:
        // which probe is fitted is a bench fact, where the channel count is a
        // model fact.
        //
        // Note what this does NOT do: change what the input can survive. The
        // command scales the display, the measurements and the trigger levels
        // -- it does not attenuate anything. Telling the scope about a divider
        // that is not there produces readings ten times too large, with
        // nothing anywhere to catch it.
        //
        std::optional<double>                       ProbeAttenuation{};
        std::optional<ChannelDisplay>               Display{};
    };

    //
    // :SINGle, plus the two registers that make it usable -- the config behind
    // Arm and Await (see core/verbs/acquire.hpp).
    //
    // Both verbs take the same config because they are two halves of one
    // operation, and the timeouts belong to the halves rather than to the
    // instrument: a script that arms, drops a rail and waits is stating how
    // long each of those is allowed to take, and those are different numbers
    // for different tests on the same scope.
    //
    struct SingleConfig
    {
        DSOX1202G &                                 Instrument;

        //
        // How long Await will poll for the acquisition to finish before giving
        // up and reporting the capture as not completed. Unset means the
        // driver's own default (see DSOX1202G::kDefaultCaptureTimeout).
        //
        std::optional<core::quantities::Time>       Timeout{};

        //
        // How long Arm will poll :AER? for the scope to report itself armed
        // and ready. A separate number from the one above because it bounds a
        // different thing -- the instrument getting ready, which takes as long
        // as it takes regardless of the DUT, against the event arriving, which
        // is entirely about the DUT.
        //
        std::optional<core::quantities::Time>       ArmTimeout{};
    };

    //
    // :WAVeform -- which record to transfer off the instrument, for Fetch.
    //
    // Carries the channel and nothing else, as the Infiniium driver's does,
    // and for mostly the same reason: the format is fixed by this driver (the
    // samples come back scaled into volts, see fetchWaveform) and how many
    // points there are and how far apart they sit were settled when the scope
    // triggered.
    //
    // "Mostly", because this instrument does have one transfer setting worth a
    // name eventually: :WAVeform:POINts and :WAVeform:POINts:MODE decide how
    // much of the acquisition record is sent -- a decimated screenful or the
    // whole raw memory. That is a genuine choice, it costs transfer time, and
    // it is deliberately not modelled while there is no transport to spend
    // that time on. It is the first thing this builder should grow when there
    // is.
    //
    struct WaveformConfig
    {
        DSOX1202G &  Instrument;

        //
        // Not optional, for ChannelConfig::Channel's reason: a transfer with
        // no source is not an underspecified instruction, it is not one at
        // all.
        //
        unsigned     Channel;
    };

    //
    // ---------------------------------------------------------------------
    // The fluent chains a script builds before handing them to a verb
    // ---------------------------------------------------------------------
    //
    // Six of them, one per subsystem this scope configures, and all six get
    // their copy-modify-return shape from hal::ConfigBuilder (see
    // hal/driver/builder.hpp) -- so "how do I set X" reads the same way
    // whether X is sourced, sensed, framed or triggered, and a setter here is
    // one line naming the field it sets.
    //
    // The one exception is averagedOver(), which sets two fields because on
    // this instrument they are one decision -- see it for why the pairing
    // belongs in the config rather than in the driver.
    //

    class TriggerBuilder : public ConfigBuilder<TriggerBuilder, TriggerConfig>
    {
        public:
            using Config = TriggerConfig;

            explicit TriggerBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            //
            // Which channel the trigger watches, checked the same way
            // DSOX1202G::channel<N>() is -- edgeSource<3>() has no valid
            // instantiation on a two-channel scope, rather than being a
            // settings-conflict error discovered on the bench.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto edgeSource() const
            {
                return with( &Config::EdgeSource, N);
            }

            [[nodiscard]]
            auto slope( const TriggerSlope value) const
            {
                return with( &Config::Slope, value);
            }

            [[nodiscard]]
            auto level( const core::quantities::Voltage value) const
            {
                return with( &Config::Level, value);
            }

            [[nodiscard]]
            auto sweep( const TriggerSweep value) const
            {
                return with( &Config::Sweep, value);
            }

            [[nodiscard]]
            auto coupling( const TriggerCoupling value) const
            {
                return with( &Config::Coupling, value);
            }

            [[nodiscard]]
            auto reject( const TriggerReject value) const
            {
                return with( &Config::Reject, value);
            }

            [[nodiscard]]
            auto holdoff( const core::quantities::Time value) const
            {
                return with( &Config::Holdoff, value);
            }

    };

    class TimebaseBuilder : public ConfigBuilder<TimebaseBuilder, TimebaseConfig>
    {
        public:
            using Config = TimebaseConfig;

            explicit TimebaseBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]]
            auto timePerDivision( const core::quantities::Time value) const
            {
                return with( &Config::TimePerDivision, value);
            }

            [[nodiscard]]
            auto position( const core::quantities::Time value) const
            {
                return with( &Config::Position, value);
            }

            [[nodiscard]]
            auto reference( const TimebaseReference value) const
            {
                return with( &Config::Reference, value);
            }

    };

    class AcquisitionBuilder : public ConfigBuilder<AcquisitionBuilder, AcquisitionConfig>
    {
        public:
            using Config = AcquisitionConfig;

            explicit AcquisitionBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            //
            // type(), not mode(): :ACQuire:TYPE is the command, and this
            // instrument's :ACQuire:MODE is a different setting -- see
            // AcquisitionType. A script ported from the Infiniium driver hits
            // this rename at compile time, which is the right place to be
            // asked whether it meant the sampling type or real-time-against-
            // segmented.
            //
            [[nodiscard]]
            auto type( const AcquisitionType value) const
            {
                return with( &Config::Type, value);
            }

            //
            // Averaging on, over this many acquisitions -- one call, because
            // on this instrument it is one decision in two commands
            // (:ACQuire:TYPE AVERage and :ACQuire:COUNt). Setting the count
            // without the type would leave the count sitting unused until some
            // later script selected averaging and inherited it.
            //
            // Note this averages across successive triggers, so a single-shot
            // capture cannot use it -- there is only ever one trigger to
            // average. AcquisitionType::HighResolution is the one that
            // averages *within* a record, and is what a single-shot capture of
            // a slow event wants instead.
            //
            [[nodiscard]]
            auto averagedOver( const unsigned count) const
            {
                return changed( [count]( Config & config)
                                {
                                    config.Type         = AcquisitionType::Averaged;
                                    config.AverageCount = count;
                                });
            }

    };

    //
    // The per-channel chain.
    //
    // Note what it holds: the instrument and a channel *number*, never a
    // Channel<N>. That is the dangling-reference lesson core::Port learned
    // (see Channel's own comment below) -- `Osc1.channel<2>().coupling( ... )`
    // produces a temporary channel view which is gone by the end of the full
    // expression, well before Setup gets its hands on the config.
    //
    class ChannelBuilder : public ConfigBuilder<ChannelBuilder, ChannelConfig>
    {
        public:
            using Config = ChannelConfig;

            ChannelBuilder( DSOX1202G & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}

            [[nodiscard]]
            auto coupling( const Coupling value) const
            {
                return with( &Config::InputCoupling, value);
            }

            [[nodiscard]]
            auto voltsPerDivision( const core::quantities::Voltage value) const
            {
                return with( &Config::VoltsPerDivision, value);
            }

            [[nodiscard]]
            auto verticalOffset( const core::quantities::Voltage value) const
            {
                return with( &Config::VerticalOffset, value);
            }

            [[nodiscard]]
            auto bandwidth( const Bandwidth value) const
            {
                return with( &Config::BandwidthLimit, value);
            }

            //
            // The probe's divider ratio: 10.0 for an ordinary 10:1 probe, 1.0
            // for a direct BNC lead. See ChannelConfig::ProbeAttenuation on
            // why this is a number rather than one of a fixed set of adapters,
            // and on what it does and does not change.
            //
            [[nodiscard]]
            auto probeAttenuation( const double ratio) const
            {
                return with( &Config::ProbeAttenuation, ratio);
            }

            [[nodiscard]]
            auto display( const ChannelDisplay value) const
            {
                return with( &Config::Display, value);
            }

    };

    class WaveformBuilder : public ConfigBuilder<WaveformBuilder, WaveformConfig>
    {
        public:
            using Config = WaveformConfig;

            WaveformBuilder( DSOX1202G & instrument, const unsigned channel) :
                ConfigBuilder( Config{ instrument, channel })
            {}

            //
            // No setters yet -- see WaveformConfig on the one this instrument
            // has and why it waits for the transport. It is still a builder
            // rather than a bare config so that Fetch's argument reads like
            // every other verb's, and so that :WAVeform:POINts arrives as a
            // method rather than as a new type.
            //
    };

    class SingleBuilder : public ConfigBuilder<SingleBuilder, SingleConfig>
    {
        public:
            using Config = SingleConfig;

            explicit SingleBuilder( DSOX1202G & instrument) : ConfigBuilder( Config{ instrument }) {}

            [[nodiscard]]
            auto timeout( const core::quantities::Time value) const
            {
                return with( &Config::Timeout, value);
            }

            [[nodiscard]]
            auto armTimeout( const core::quantities::Time value) const
            {
                return with( &Config::ArmTimeout, value);
            }

    };

    //
    // One of the DSOX1202G's two physical input channels, narrowed to at
    // compile time via DSOX1202G::channel<N>().
    //
    // It is the one place a channel number is written in a script, and it
    // serves both things a script does to a channel -- configure it and
    // measure through it:
    //
    //     Setup(   Osc1.channel<2>().coupling( Coupling::Dc).voltsPerDivision( 100_mV));
    //     Measure( Osc1.channel<2>().vmin(), at( dut::Vout));
    //
    // Note there is deliberately no config() here, only on the builder each
    // setting method returns. So `Setup( Osc1.channel<2>())` -- a Setup that
    // names a channel and no setting, which can only be a mistake -- is "no
    // matching function" rather than a call that does nothing.
    //
    // Deliberately a thin, transient view over DSOX1202G&, never itself
    // retained by anything returned from it: every measurement method records
    // N onto the real DSOX1202G instance -- via setChannel(), the same way it
    // records which mode via setMode() -- and then hands back a
    // core::Port<Q, DSOX1202G> bound directly to that real, singular
    // instrument, not to this view.
    //
    // That is load-bearing rather than incidental, and the Infiniium driver's
    // own comment records why: an earlier version of that file had Port
    // referencing the channel view itself, so a Port obtained via
    // `osc1.channel<2>().vmax()` and held past the full expression that
    // created it referenced a temporary that no longer existed -- silent
    // dangling-reference UB, caught by a test rather than by the compiler.
    // Binding Port straight to the instrument removes the dangling risk at the
    // cost of the channel being instrument-level mutable state, which is the
    // same accepted sharp edge Mode already has: a Port handle read after a
    // later channel<M>() switch reads whichever channel is current at
    // rawMeasure() time. One documented sharp edge beats a second, worse,
    // undocumented one.
    //
    // The setting methods have no such edge, because they carry the channel
    // number into the config by value rather than leaving it on the
    // instrument -- see ChannelBuilder.
    //
    template<unsigned N>
        requires ValidChannel<N>
    class Channel
    {
        public:
            static constexpr unsigned Number = N;

            explicit Channel( DSOX1202G & instrument) : mInstrument( instrument) {}

            // --- Configuring this channel: hands off to the builder ---

            [[nodiscard]] auto coupling( Coupling value) const -> ChannelBuilder;
            [[nodiscard]] auto voltsPerDivision( core::quantities::Voltage value) const -> ChannelBuilder;
            [[nodiscard]] auto verticalOffset( core::quantities::Voltage value) const -> ChannelBuilder;
            [[nodiscard]] auto bandwidth( Bandwidth value) const -> ChannelBuilder;
            [[nodiscard]] auto probeAttenuation( double ratio) const -> ChannelBuilder;
            [[nodiscard]] auto display( ChannelDisplay value) const -> ChannelBuilder;

            //
            // The amplitude family -- the :MEASure:V... subset that answers
            // questions about levels. Each records both N (setChannel) and its
            // own mode (setMode) onto the real instrument before returning a
            // Port bound to that same instrument.
            //
            // vbase()/vtop() are not vmin()/vmax(), and the difference is the
            // one that matters for transient work. Min and max are the extreme
            // samples in the record, so a single spike moves them. Base and
            // top are the settled levels the waveform spends its time at, so
            // they are not moved by the spike at all -- which makes
            // vbase() - vmin() the size of a negative transient measured
            // against the rail it departed from.
            //
            // Eight, where the Infiniium driver has nine: there is no
            // :MEASure:VMIDdle on the 1000 X-Series, so vmiddle() is absent
            // rather than emulated as (vtop + vbase) / 2. A driver that
            // computed a reading the instrument did not take would be putting
            // a number in the run journal that no instrument ever answered,
            // and the journal's whole claim is that its numbers came off the
            // bench.
            //
            // vrms() and vaverage() carry one decision each that this
            // instrument makes explicit and the Infiniium does not: both
            // commands take an interval ({ CYCLe | DISPlay }) and :MEASure:VRMS
            // also takes a type ({ AC | DC }). This driver asks for neither, so
            // both take the instrument's documented defaults -- DISPlay, and
            // DC RMS. Whole-screen and DC-inclusive is what a rail measurement
            // wants; a script needing cycle-RMS of the ripple alone needs a
            // parameter here, and should get one rather than a comment saying
            // it was thought about.
            //
            [[nodiscard]] auto vpp()        -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vmax()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vmin()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vrms()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vaverage()   -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vbase()      -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vtop()       -> core::Port<core::quantities::Voltage, DSOX1202G>;
            [[nodiscard]] auto vamplitude() -> core::Port<core::quantities::Voltage, DSOX1202G>;

            //
            // The timing family. frequency()/period()/positiveWidth()/
            // negativeWidth() need nothing beyond "which channel";
            // riseTime()/fallTime() also carry the usual 10%/90% edge-timing
            // thresholds as a MeasureSetup, defaulted here so a bare
            // `.riseTime()` is still a complete, valid reading, and
            // overridable via the same chained-builder spelling as
            // core::Port's range()/nplc().
            //
            // Still not modeled: duty cycle, overshoot and preshoot, all of
            // which this instrument measures. All three are dimensionless
            // ratios, and this framework has no dimensionless quantity to
            // return them as -- core::quantities has PowerFactor and nothing
            // else without a unit, and borrowing it for a duty cycle would
            // make "0.45 of a power factor" the thing a criterion compares
            // against. That is a core question (a Ratio quantity, or a
            // criterion over a bare double), not a driver one, and inventing
            // an answer here would put it in the wrong file.
            //
            [[nodiscard]] auto frequency()      -> core::Port<core::quantities::Frequency, DSOX1202G>;
            [[nodiscard]] auto period()         -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto riseTime()       -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto fallTime()       -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto positiveWidth()  -> core::Port<core::quantities::Time, DSOX1202G>;
            [[nodiscard]] auto negativeWidth()  -> core::Port<core::quantities::Time, DSOX1202G>;

            //
            // The whole captured record off this channel, for Fetch -- see
            // core/verbs/trace.hpp.
            //
            // Not a Port and not a Measure: everything above answers one number
            // about this channel and reaches the DUT point named at the Measure
            // call, with the route closed and reopened around it. A trace is
            // already inside the instrument, arrived over whatever route the
            // capture was taken on, and is not a quantity a criterion can be
            // pointed at (see core::Waveform on what a script does with one
            // instead).
            //
            // Unlike the measurement methods above, this does NOT switch the
            // instrument's mode or selected channel -- the channel travels in
            // the config by value, so the sharp edge those fourteen carry does
            // not exist here. Same as the setting builders.
            //
            [[nodiscard]] auto waveform() const -> WaveformBuilder;

        private:
            DSOX1202G & mInstrument;
    };

    //
    // Keysight InfiniiVision DSOX1202G: this rig's actual scope -- two
    // channels, 2 GSa/s, 70 MHz as shipped and licence-upgradable to 200 --
    // replacing hal::keysight_dso8064a::DSO8064A, which was this bench's scope
    // when the only thing known about it was the legacy ATE script it had to
    // reproduce.
    //
    // That driver is still in the tree and still builds, which is deliberate:
    // it is a working, tested driver for a real instrument, and the day a
    // second bench has an Infiniium on it, it is what that bench uses. What
    // changed is which one rig/instrument.inc names.
    //
    // What it does, in the four groups a script uses:
    //
    //   Setup  -- :TRIGger, :TIMebase, :ACQuire instrument-wide, and
    //             :CHANnel<N> per input. Four builders, one per subsystem;
    //             see the config types above for why they are four and not
    //             one.
    //   Arm /  -- :SINGle, bracketing whatever event the script causes. See
    //   Await     core/verbs/acquire.hpp for why they are two verbs, and
    //             armSingle/awaitAcquisition below for the exact register
    //             sequence this instrument's guide prescribes.
    //   Measure-- the :MEASure amplitude and timing families, channel-scoped
    //             via channel<N>()/Channel above.
    //   Fetch  -- :WAVeform, the whole captured record off one channel rather
    //             than one number measured from it. See core/verbs/trace.hpp.
    //
    // Calling one of the Mode-tagged measurement methods switches the
    // instrument's current measurement mode *and* channel, the same way the
    // front-panel Meas menu switches which measurement is installed before a
    // reading is taken. Same accepted sharp edge hal::keysight_l4411a::L4411A's
    // AC/DC mode has: a port handle read after a later mode or channel switch
    // reads whichever is current at rawMeasure() time, not whichever was
    // active when the handle was obtained -- harmless for
    // Measure( port, at( ...))'s read-immediately-and-discard usage.
    //
    // -- What this driver does not do yet ------------------------------------
    //
    // Talk. Every reading below comes from this class's own simulation hooks,
    // exactly as the scope it replaces does, and the address it is constructed
    // with is stored and not opened. That is a deliberate first step and not
    // an oversight: replacing the instrument and replacing the transport are
    // two changes, and doing them at once would have made "the readings
    // changed" and "the wiring changed" indistinguishable in one diff. See
    // hal/io/ and hal::keysight_edu34450a::EDU34450A for what the second step
    // looks like -- that meter is the worked example of a driver that opens a
    // SCPI session, and this one follows it.
    //
    // Until then rig/instrument.inc says hal::Simulated{} for this row, which
    // is the honest column value for a driver that reads no address (see that
    // file's own rule on when a row may name a real bus address).
    //
    // Also still deliberately deferred:
    //   - Segmented acquisition (:ACQuire:MODE SEGMented) -- a licensed
    //     option, see AcquisitionType.
    //   - The waveform generator. This is the G model, so there is a 20 MHz
    //     function generator behind the front panel, and it is a *source*:
    //     modelling it means applyDriver, removeDriver and a place in the
    //     rig's safing sequence, not one more Setup. It is a second
    //     instrument sharing one chassis, and this rig does not use it.
    //   - Duty cycle, overshoot, preshoot -- see Channel's own comment on why
    //     these wait on a core decision, not a driver one.
    //   - The trigger kinds beyond edge (:TRIGger:MODE GLITch / PATTern / TV /
    //     and this model's serial-bus triggers). Edge is what this bench
    //     triggers on; the others are a large surface each with its own
    //     parameter set.
    //
    class DSOX1202G : public InstrumentTag
    {
        public:
            static constexpr unsigned channel_count = 2;

            //
            // No Vmiddle -- see Channel's comment on the :MEASure:VMIDdle this
            // instrument does not have.
            //
            enum class Mode
            {
                Vpp, Vmax, Vmin, Vrms, Vaverage, Vbase, Vtop, Vamplitude,
                Frequency, Period, RiseTime, FallTime, PositiveWidth, NegativeWidth
            };

            //
            // How long Await polls for the capture to complete, and Arm for
            // the scope to report itself armed, when the script does not say.
            //
            // Both are stated here rather than left implicit because a default
            // timeout is a real decision about how a failing test behaves: too
            // short and a slow DUT reports a transient that was there, too long
            // and a suite hangs on a DUT that is simply dead. The capture
            // default is the more generous of the two for that reason -- it is
            // waiting on the device under test, where arming is waiting only on
            // the instrument.
            //
            static constexpr core::quantities::Time kDefaultCaptureTimeout{ 5.0 };
            static constexpr core::quantities::Time kDefaultArmTimeout{ 1.0 };

            //
            // USB, and only USB.
            //
            // This is the one place where the difference between the 1000
            // X-Series and its 1200 X-Series successor is enforced rather than
            // described. The DSOX1202A/G has a single rear USB device port and
            // no network connector at all -- the programmer's guide says it in
            // as many words, "There is no LAN interface (only USB is
            // supported)", and lists :HARDcopy:NETWork:ADDRess among the
            // commands that exist on other models and not on this one. The
            // four-channel DSOX1204A/G is the model in that family that gained
            // a LAN interface.
            //
            // So Lan( "bench-osc1") on this driver's row is not a
            // configuration mistake to be discovered when a connection times
            // out; it is a sentence about a socket this box does not have, and
            // it fails to compile. That is the whole point of hal::ReachableOver
            // (see hal/driver/address.hpp) -- and this instrument is the
            // sharpest example of it in the tree, because the scope it
            // replaces accepted Gpib, Lan and Usb and this rig's row said Lan.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Usb>
            DSOX1202G( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            // Where the PC reaches this scope -- see hal/driver/address.hpp.
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
            // Narrows to one of the instrument's two physical channels --
            // channel<3>() or channel<0>() simply has no valid instantiation
            // (ValidChannel), a hard compile error, not a runtime range check.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto channel() -> Channel<N>
            {
                return Channel<N>{ *this };
            }

            // --- The instrument-wide builders ---

            [[nodiscard]]
            auto trigger() -> TriggerBuilder
            {
                return TriggerBuilder{ *this };
            }

            [[nodiscard]]
            auto timebase() -> TimebaseBuilder
            {
                return TimebaseBuilder{ *this };
            }

            [[nodiscard]]
            auto acquisition() -> AcquisitionBuilder
            {
                return AcquisitionBuilder{ *this };
            }

            //
            // The single-shot capture, for Arm and Await -- named after the
            // instrument's own :SINGle rather than after what the verbs do
            // with it, so that a reader holding the programmer's guide
            // recognises it. Which of the two verbs is being invoked is what
            // says whether this is arming or waiting.
            //
            [[nodiscard]]
            auto single() -> SingleBuilder
            {
                return SingleBuilder{ *this };
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
            // Nothing to do -- a scope is passive, exactly as
            // hal::keysight_l4411a::L4411A is; see that class's safe() for why
            // this is written out as an explicit empty body rather than simply
            // left absent.
            //
            // Note this deliberately does not reset mMode/mChannel, nor any of
            // the settings a Setup left behind. They are instrument state a
            // script set, not anything that can energise the DUT -- and safing
            // runs when a script has already died, so there is nobody left to
            // surprise with a mode change. Resetting them would only discard
            // the last thing the scope was told to look at, which is the one
            // piece of state worth still being able to read afterwards.
            //
            // The armed flag is the one thing that IS cleared, and for a reason
            // the others do not share: it is not a setting but a pending
            // expectation. A scope left armed after a script died is waiting
            // for an event that is no longer coming, and the next script's
            // Await would be answered by it.
            //
            // On real hardware this is where :STOP goes -- an armed scope left
            // running is also a scope that will happily trigger on the next
            // script's stimulus.
            //
            auto safe() -> void
            {
                mArmed = false;
            }

            // Switches the instrument's current measurement mode/channel --
            // called by Channel's builder methods, never by a script directly.
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
            // Four configure methods, one per subsystem, and each one a run of
            // ifs rather than a run of value_or. An unset field means "leave
            // what is already configured", so a Setup naming only the trigger
            // level must not reset the slope to some default the builder never
            // chose.
            //
            auto configureTrigger( const TriggerConfig & config) -> void
            {
                if( config.EdgeSource) { mTriggerSource   = config.EdgeSource; }
                if( config.Slope)      { mTriggerSlope    = config.Slope;      }
                if( config.Level)      { mTriggerLevel    = config.Level;      }
                if( config.Sweep)      { mTriggerSweep    = config.Sweep;      }
                if( config.Coupling)   { mTriggerCoupling = config.Coupling;   }
                if( config.Reject)     { mTriggerReject   = config.Reject;     }
                if( config.Holdoff)    { mTriggerHoldoff  = config.Holdoff;    }
            }

            auto configureTimebase( const TimebaseConfig & config) -> void
            {
                if( config.TimePerDivision) { mTimePerDivision   = config.TimePerDivision; }
                if( config.Position)        { mTimebasePosition  = config.Position;        }
                if( config.Reference)       { mTimebaseReference = config.Reference;       }
            }

            auto configureAcquisition( const AcquisitionConfig & config) -> void
            {
                if( config.Type)         { mAcquisitionType = config.Type;         }
                if( config.AverageCount) { mAverageCount    = config.AverageCount; }
            }

            auto configureChannel( const ChannelConfig & config) -> void
            {
                auto & data = atChannel( config.Channel);

                if( config.InputCoupling)    { data.InputCoupling    = config.InputCoupling;    }
                if( config.VoltsPerDivision) { data.VoltsPerDivision = config.VoltsPerDivision; }
                if( config.VerticalOffset)   { data.VerticalOffset   = config.VerticalOffset;   }
                if( config.BandwidthLimit)   { data.BandwidthLimit   = config.BandwidthLimit;   }
                if( config.ProbeAttenuation) { data.ProbeAttenuation = config.ProbeAttenuation; }
                if( config.Display)          { data.Display          = config.Display;          }
            }

            //
            // Arm: on real hardware this is the guide's own single-shot
            // sequence, which is worth following exactly because the ordering
            // is what makes it work -- ":STOP", "*OPC?" to let that settle,
            // ":SINGle" to arm, then poll ":AER?" until it answers 1 or the arm
            // timeout runs out. Keysight's example puts the instruction to
            // enable the DUT immediately after that loop, in a comment:
            // "Oscilloscope is armed and ready, enable DUT here."
            //
            // That is precisely the contract core::ArmEngine promises a script
            // -- Arm returns armed, not told-to-arm -- and it is why a script
            // may drop a rail on the very next line.
            //
            // Note :DIGitize is NOT how this is done, and the guide is explicit
            // about why: it blocks the instrument against further commands
            // until the acquisition completes, so a single-shot DUT that has
            // not been triggered yet can never be enabled.
            //
            // Simulated here, so what survives is the state that ordering
            // produces: armed, with no completed acquisition behind it.
            //
            auto armSingle( const SingleConfig &) -> void
            {
                mArmed     = true;
                mCompleted = false;
            }

            //
            // Await: poll ":OPERegister:CONDition?" and watch the RUN bit
            // (bit 3, 0x08) until it clears, or the timeout runs out. A cleared
            // RUN bit means the scope has stopped, which after a :SINGle means
            // the acquisition completed.
            //
            // Not :TER? (the trigger event register), which answers whether a
            // trigger happened and is cleared by being read -- a triggered
            // scope may still be filling its record. The RUN bit is the one
            // that says the capture is over and the record can be measured.
            //
            // Awaiting something that was never armed answers false rather
            // than throwing, and that is a deliberate reading of what the
            // mistake is. A script that measures a transient without having
            // armed a capture has not crashed -- it has measured whatever was
            // left in the acquisition buffer, which is a wrong answer, and the
            // check that this Await gates is exactly where a wrong answer
            // should be caught. Throwing would abandon the rest of the run over
            // a script bug the run itself is capable of reporting.
            //
            [[nodiscard]]
            auto awaitAcquisition( const SingleConfig &) -> bool
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
            // here -- (raw - yReference) * yIncrement + yOrigin, and the x pair
            // carried straight into core::Waveform::Timing.
            //
            // The scaling belongs here and not one layer up, which is what
            // core::Waveform stores values in units for: a raw level is a fact
            // about this digitiser at this vertical setting, and this driver is
            // the only thing that knows the encoding. A recording holding raw
            // levels would be unreadable without the instrument that wrote it.
            //
            // Simulated here, so what comes back is whatever setSimulatedTrace()
            // put there. Fetching a channel nothing was put on answers an empty
            // trace rather than throwing, for the reason awaitAcquisition
            // answers false rather than throwing.
            //
            [[nodiscard]]
            auto fetchWaveform( const WaveformConfig & config) -> core::Waveform
            {
                return atChannel( config.Channel).Trace;
            }

            // --- Test/simulation hooks -- real hardware has no such setters ---
            //
            // Channel is a plain runtime unsigned here (1-2): this is test
            // scaffolding setting up canned data, not the compile-time-checked
            // script-facing surface channel<N>() provides.

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
            // -- what this scope does when the trace does not support the
            // question being asked of it.
            //
            // No fault argument, unlike the Infiniium driver's hook of the same
            // name, and the missing parameter is the point: this instrument
            // answers +9.9E+37 and nothing else, so a hook that let a test
            // choose *which* reason came back would let tests be written
            // against an instrument that does not exist. See kUnmeasurable.
            //
            // Still per (channel, measurement) rather than per channel, because
            // that is how the instrument behaves: a clipped trace still has a
            // perfectly good period, and a flat trace with no edge on it has a
            // vmax and no rise time. A single "this channel is broken" flag
            // could not express either case.
            //
            auto setSimulatedUnmeasurable( const unsigned channel, const Mode mode) -> void
            {
                atChannel( channel).Unmeasurable.at( static_cast<std::size_t>( mode)) = true;
            }

            auto clearSimulatedUnmeasurable( const unsigned channel, const Mode mode) -> void
            {
                atChannel( channel).Unmeasurable.at( static_cast<std::size_t>( mode)) = false;
            }

            //
            // Whether the next armed capture completes. Defaults to true -- the
            // happy path, so a test that does not care about capture failure
            // says nothing about it -- and set false to exercise the timeout
            // branch.
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
            [[nodiscard]] auto triggerReject() const     -> std::optional<TriggerReject>     { return mTriggerReject;   }
            [[nodiscard]] auto triggerHoldoff() const    -> std::optional<core::quantities::Time> { return mTriggerHoldoff; }

            [[nodiscard]] auto timePerDivision() const   -> std::optional<core::quantities::Time> { return mTimePerDivision; }
            [[nodiscard]] auto timebasePosition() const  -> std::optional<core::quantities::Time> { return mTimebasePosition; }
            [[nodiscard]] auto timebaseReference() const -> std::optional<TimebaseReference> { return mTimebaseReference; }

            [[nodiscard]] auto acquisitionType() const   -> std::optional<AcquisitionType>   { return mAcquisitionType; }
            [[nodiscard]] auto averageCount() const      -> std::optional<unsigned>          { return mAverageCount;    }

            [[nodiscard]] auto channelCoupling( const unsigned channel) -> std::optional<Coupling>
            {
                return atChannel( channel).InputCoupling;
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

            [[nodiscard]] auto probeAttenuation( const unsigned channel) -> std::optional<double>
            {
                return atChannel( channel).ProbeAttenuation;
            }

            [[nodiscard]] auto channelDisplay( const unsigned channel) -> std::optional<ChannelDisplay>
            {
                return atChannel( channel).Display;
            }

            //
            // The one rawMeasure() Port<Q, DSOX1202G> actually calls --
            // mChannel/mMode are already instrument state by the time this runs
            // (set by whichever Channel<N> builder method produced the Port), so
            // this needs no channel argument of its own.
            //
            // Throws core::UnmeasurableReading when the instrument would have
            // answered +9.9E+37 -- see kUnmeasurable for why the message is
            // always the same one, and core::MeasureEngine for what catches it.
            // Throwing rather than returning a sentinel is what keeps every
            // driver's rawMeasure() returning the quantity it says it returns:
            // a sentinel would be a magic number every caller had to know to
            // test for, which is exactly the ISINVALID() arrangement this
            // replaces.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                const auto & data = atChannel( mChannel);

                if( data.Unmeasurable.at( static_cast<std::size_t>( mMode)))
                {
                    throw core::UnmeasurableReading( std::string( kUnmeasurable));
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
                    static_assert( !sizeof( QuantityT), "DSOX1202G has no port for this quantity");
                }
            }

        private:
            //
            // How many measurements a channel can be made to report as
            // unmeasurable -- derived from Mode's own enumerators by reflection
            // rather than written down beside them (see core/meta.hpp). Adding
            // a measurement to Mode therefore cannot leave this array a size
            // too short, because there is nothing to forget to update.
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
                core::quantities::Frequency  Freq{};
                core::quantities::Time       Period{};
                core::quantities::Time       RiseTime{};
                core::quantities::Time       FallTime{};
                core::quantities::Time       PositiveWidth{};
                core::quantities::Time       NegativeWidth{};

                // What a Setup left on this input, and which of its
                // measurements the instrument would refuse to answer.
                std::optional<Coupling>                   InputCoupling;
                std::optional<core::quantities::Voltage>  VoltsPerDivision;
                std::optional<core::quantities::Voltage>  VerticalOffset;
                std::optional<Bandwidth>                  BandwidthLimit;
                std::optional<double>                     ProbeAttenuation;
                std::optional<ChannelDisplay>             Display;

                //
                // A flag per measurement rather than an optional fault code:
                // there is one reason, so the only question is whether this
                // measurement answers. See kUnmeasurable.
                //
                std::array<bool, kModeCount> Unmeasurable{};

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
            std::optional<TriggerReject>                mTriggerReject;
            std::optional<core::quantities::Time>       mTriggerHoldoff;

            std::optional<core::quantities::Time>       mTimePerDivision;
            std::optional<core::quantities::Time>       mTimebasePosition;
            std::optional<TimebaseReference>            mTimebaseReference;

            std::optional<AcquisitionType>              mAcquisitionType;
            std::optional<unsigned>                     mAverageCount;

            bool                                        mArmed{ false };
            bool                                        mCompleted{ false };
            bool                                        mCaptureCompletes{ true };
    };

    //
    // ---------------------------------------------------------------------
    // Channel<N>, out of line
    // ---------------------------------------------------------------------
    //
    // Below DSOX1202G rather than inside the class template, because every one
    // of these needs the instrument to be a complete type.
    //

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::coupling( const Coupling value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.coupling( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::waveform() const -> WaveformBuilder
    {
        return WaveformBuilder{ mInstrument, N };
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::voltsPerDivision( const core::quantities::Voltage value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.voltsPerDivision( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::verticalOffset( const core::quantities::Voltage value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.verticalOffset( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::bandwidth( const Bandwidth value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.bandwidth( value);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::probeAttenuation( const double ratio) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.probeAttenuation( ratio);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::display( const ChannelDisplay value) const -> ChannelBuilder
    {
        return ChannelBuilder{ mInstrument, N }.display( value);
    }

    //
    // The measurement family. Every one of these is the same three lines --
    // record the channel, record the mode, hand back a Port on the real
    // instrument, qualified by which measurement it is.
    //
    // That qualifier is what stops fourteen different answers about one DUT pin
    // from sharing one session slot: a routed reading keys as "Output5V.Vbase"
    // rather than as "Output5V" (see core::MeasureEngine, and
    // core::Port::qualifiedBy for the mechanism). A DMM measuring a rail needs
    // no such thing -- there is only one voltage at a pin -- which is why the
    // qualifier is opt-in and a scope is what opts in.
    //
    // The list is generated from a macro rather than written out fourteen
    // times: written out, each body was four lines of which three were
    // identical, and the qualifier would have been a fourth thing to remember
    // to spell correctly per measurement -- with a typo producing not a compile
    // error but a session key nothing injects against.
    //
    // The qualifiers are deliberately the same words the Infiniium driver uses
    // ("Vbase", "RiseTime", ...), which is what makes this instrument swap
    // invisible to a recording: a run recorded against the old scope replays
    // against this one, and suite/tests inject the same keys they always did.
    // Two of that driver's keys have no counterpart here -- "Vmiddle", and any
    // trace off "Channel3"/"Channel4" -- and a recording carrying those is a
    // recording of a run this bench can no longer make.
    //
#define THORIUM_DSOX1202G_PORT( method, quantity, mode)                                          \
    template<unsigned N>                                                                         \
        requires ValidChannel<N>                                                                 \
    auto Channel<N>::method() -> core::Port<core::quantities::quantity, DSOX1202G>               \
    {                                                                                            \
        mInstrument.setChannel( N);                                                              \
        mInstrument.setMode( DSOX1202G::Mode::mode);                                             \
        return core::Port<core::quantities::quantity, DSOX1202G>{ mInstrument }.qualifiedBy( #mode); \
    }

    THORIUM_DSOX1202G_PORT( vpp,           Voltage,   Vpp)
    THORIUM_DSOX1202G_PORT( vmax,          Voltage,   Vmax)
    THORIUM_DSOX1202G_PORT( vmin,          Voltage,   Vmin)
    THORIUM_DSOX1202G_PORT( vrms,          Voltage,   Vrms)
    THORIUM_DSOX1202G_PORT( vaverage,      Voltage,   Vaverage)
    THORIUM_DSOX1202G_PORT( vbase,         Voltage,   Vbase)
    THORIUM_DSOX1202G_PORT( vtop,          Voltage,   Vtop)
    THORIUM_DSOX1202G_PORT( vamplitude,    Voltage,   Vamplitude)
    THORIUM_DSOX1202G_PORT( frequency,     Frequency, Frequency)
    THORIUM_DSOX1202G_PORT( period,        Time,      Period)
    THORIUM_DSOX1202G_PORT( positiveWidth, Time,      PositiveWidth)
    THORIUM_DSOX1202G_PORT( negativeWidth, Time,      NegativeWidth)

#undef THORIUM_DSOX1202G_PORT

    //
    // riseTime()/fallTime() are written out rather than going through the macro
    // above, because they are the two that carry a MeasureSetup: they seed the
    // usual 10%/90% thresholds up front, so a bare `.riseTime()` is a complete
    // reading.
    //
    // 10%/90% is also what this instrument does by default -- :MEASure:DEFine
    // THResholds's STANdard setting -- so the two agree, and the values are
    // stated here rather than left implicit because a threshold is part of what
    // a rise time *means*: two scopes disagreeing about it produce two
    // different numbers for one edge.
    //
    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::riseTime() -> core::Port<core::quantities::Time, DSOX1202G>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSOX1202G::Mode::RiseTime);
        return core::Port<core::quantities::Time, DSOX1202G>{ mInstrument }
                   .qualifiedBy( "RiseTime").lowThreshold( 0.1).highThreshold( 0.9);
    }

    template<unsigned N>
        requires ValidChannel<N>
    auto Channel<N>::fallTime() -> core::Port<core::quantities::Time, DSOX1202G>
    {
        mInstrument.setChannel( N);
        mInstrument.setMode( DSOX1202G::Mode::FallTime);
        return core::Port<core::quantities::Time, DSOX1202G>{ mInstrument }
                   .qualifiedBy( "FallTime").lowThreshold( 0.1).highThreshold( 0.9);
    }

    //
    // ---------------------------------------------------------------------
    // ADL customization points
    // ---------------------------------------------------------------------
    //
    // Four setupDriver overloads, one per config type -- see core/verbs/source.hpp
    // on why configuring is a verb of its own rather than a flavour of Apply.
    //
    // Note what this driver deliberately does NOT define: applyDriver and
    // removeDriver. A scope has no output to energise -- there is nothing an
    // Apply( Osc1.trigger()) could mean -- so Apply on this instrument is "no
    // matching function" at compile time, exactly the way it is on
    // hal::racal1260::Racal1260's serial port. The absence is the design.
    //
    // That is worth one extra line on this model specifically, because it is
    // the one claim here that a datasheet could be read as contradicting: the
    // G in DSOX1202G is a built-in 20 MHz waveform generator, which genuinely
    // is an output that energises things. It is not modelled (see this
    // driver's own comment on what is deferred), so this remains true of the
    // driver; the day somebody models it, this instrument grows applyDriver and
    // a place in rig/wiring.inc's safing order, and stops being passive.
    //
    // Nor connectDriver/disconnectDriver: this scope reaches its DUT point
    // through the matrix, and Measure closes and reopens that route around each
    // reading (see core::MeasureEngine). A script therefore never Connects it
    // -- it names the point at the Measure call instead.
    //
    inline auto setupDriver( const TriggerConfig & config) -> void
    {
        config.Instrument.configureTrigger( config);
    }

    inline auto setupDriver( const TimebaseConfig & config) -> void
    {
        config.Instrument.configureTimebase( config);
    }

    inline auto setupDriver( const AcquisitionConfig & config) -> void
    {
        config.Instrument.configureAcquisition( config);
    }

    inline auto setupDriver( const ChannelConfig & config) -> void
    {
        config.Instrument.configureChannel( config);
    }

    //
    // ADL targets for core::ArmEngine and core::AwaitEngine -- the
    // triggered-acquisition pair, see core/verbs/acquire.hpp for why they are
    // two verbs and why Arm's post-condition is "armed and ready" rather than
    // "told to arm".
    //
    inline auto armDriver( const SingleConfig & config) -> void
    {
        config.Instrument.armSingle( config);
    }

    [[nodiscard]]
    inline auto awaitDriver( const SingleConfig & config) -> bool
    {
        return config.Instrument.awaitAcquisition( config);
    }

    //
    // ADL target for core::FetchEngine -- the trace verb, see core/verbs/trace.hpp.
    //
    [[nodiscard]]
    inline auto fetchDriver( const WaveformConfig & config) -> core::Waveform
    {
        return config.Instrument.fetchWaveform( config);
    }

    //
    // Which session slot a trace off this instrument files under, appended to
    // the instrument id: "Osc1.Channel2".
    //
    // Present because this scope has to have it. Two channels hold two records
    // at once, and the default "Osc1.Trace" would give both one slot -- so a
    // test injecting a channel-1 trace would find a channel-2 Fetch taking it,
    // and a recording of a run that captured both would replay them into each
    // other. Two is fewer than the Infiniium's four and changes nothing about
    // the argument: one slot for two records is already one too few.
    //
    [[nodiscard]]
    inline auto traceQualifier( const WaveformConfig & config) -> std::string
    {
        return "Channel" + std::to_string( config.Channel);
    }

    //
    // A probe ratio as a log fragment: "10x", "0.1x".
    //
    // Written here rather than reached for in hal/driver/describe.hpp because
    // neither helper there fits, and the reason is the same one that made this
    // a double in the first place. describeSetting takes a
    // core::quantities::Quantity and a ratio has no unit; describeCount is
    // constrained to integrals and 0.1 is a legal attenuation. So this driver
    // renders its own, and renders it the way a probe is labelled -- 10x, not
    // 10.000000 -- since that is the number written on the switch the operator
    // slid.
    //
    [[nodiscard]]
    inline auto describeAttenuation( const std::string_view name, const std::optional<double> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        auto digits = std::to_string( value.value());

        //
        // Trailing zeros go, but only from a fractional part, and the guard is
        // not defensive padding -- it is a bug this file already had. What
        // std::to_string does to a double is not fixed across standard
        // versions: it used to be printf's %f, so 10.0 arrived as "10.000000"
        // and needed trimming, and it is now the shortest round-trip form, so
        // 10.0 arrives as "10" and trimming it turns a 10:1 probe into a 1:1
        // one. Stripping only past a '.' is correct under both.
        //
        if( digits.contains( '.'))
        {
            while( !digits.empty() && digits.back() == '0')
            {
                digits.pop_back();
            }

            if( !digits.empty() && digits.back() == '.')
            {
                digits.pop_back();
            }
        }

        return std::string( name) + "=" + digits + "x";
    }

    //
    // ADL targets for the run journal -- see core/driver/describe.hpp's own
    // comment on the describeConfig customization point.
    //
    // Field by field, and only the fields that were set, for the reason
    // hal::describeSetting exists: a Setup that named only the trigger level is
    // a different instruction from one that named the whole trigger, and a
    // rendering that filled in the rest would be inventing settings the script
    // never chose.
    //
    inline auto describeConfig( const TriggerConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeCount(   "trigger.source",   config.EdgeSource),
                describeChoice(  "trigger.slope",    config.Slope),
                describeSetting( "trigger.level",    config.Level),
                describeChoice(  "trigger.sweep",    config.Sweep),
                describeChoice(  "trigger.coupling", config.Coupling),
                describeChoice(  "trigger.reject",   config.Reject),
                describeSetting( "trigger.holdoff",  config.Holdoff)
            })
        };
    }

    inline auto describeConfig( const TimebaseConfig & config) -> core::SourceDescription
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

    //
    // Two fields, and the averaging count rendered only when averaging is what
    // was selected. A count beside "type=HighResolution" would be a number the
    // instrument is not using, which is worse than no number: a reader
    // diagnosing a noisy capture would spend time on it.
    //
    inline auto describeConfig( const AcquisitionConfig & config) -> core::SourceDescription
    {
        const auto averaging = config.Type == AcquisitionType::Averaged;

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice( "acquire.type",     config.Type),
                averaging ? describeCount( "acquire.averages", config.AverageCount) : std::string{}
            })
        };
    }

    inline auto describeConfig( const ChannelConfig & config) -> core::SourceDescription
    {
        //
        // Every fragment carries the channel it belongs to, rather than the
        // channel being named once at the front. Two Setups on two channels
        // produce two log lines against the same InstrumentId, and a reader
        // scanning for "which channel was set to 100 mV/div" should not have to
        // carry a prefix in their head from the start of the line.
        //
        const auto prefix = "ch" + std::to_string( config.Channel) + ".";

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeChoice(      prefix + "coupling",  config.InputCoupling),
                describeSetting(     prefix + "perDiv",    config.VoltsPerDivision),
                describeSetting(     prefix + "offset",    config.VerticalOffset),
                describeChoice(      prefix + "bandwidth", config.BandwidthLimit),
                describeAttenuation( prefix + "probe",     config.ProbeAttenuation),
                describeChoice(      prefix + "display",   config.Display)
            })
        };
    }

    //
    // A trace's own line, which says only which channel it came off -- there is
    // nothing else in the config (see WaveformConfig on why). What the trace
    // *was* is the value column, and Fetch fills that with a summary rather
    // than the samples; see core::describeValue for a core::Waveform.
    //
    inline auto describeConfig( const WaveformConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            "ch" + std::to_string( config.Channel)
        };
    }

    inline auto describeConfig( const SingleConfig & config) -> core::SourceDescription
    {
        //
        // The defaults are rendered when the script did not name a timeout,
        // which is the one place in this file where an unset field is filled in
        // for the log. That is deliberate and specific to these two: a timeout
        // is the number that decides how a *failing* capture behaves, so a log
        // of a run that timed out has to say what it was waiting for, and "the
        // driver's default" is not an answer anyone reading a report at 2am can
        // act on.
        //
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "single.timeout=" + core::describeValue( config.Timeout.value_or( DSOX1202G::kDefaultCaptureTimeout)),
                "single.armTimeout=" + core::describeValue( config.ArmTimeout.value_or( DSOX1202G::kDefaultArmTimeout))
            })
        };
    }
} // namespace hal::keysight_dsox1202g
