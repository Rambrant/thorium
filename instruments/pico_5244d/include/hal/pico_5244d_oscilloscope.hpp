#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"
#include "core/quantities/waveform.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"

#include "hal/pico_5244d/library.hpp"
#include "hal/pico_5244d/unit.hpp"
#include "hal/pico_5244d/vocabulary.hpp"
#include "hal/pico_5244d/trigger.hpp"
#include "hal/pico_5244d/timebase.hpp"
#include "hal/pico_5244d/acquisition.hpp"
#include "hal/pico_5244d/channel.hpp"
#include "hal/pico_5244d/single.hpp"
#include "hal/pico_5244d/fetch.hpp"

//
// The instrument itself. Everything that touches the SDK is in
// src/oscilloscope.cpp. Part of hal/pico_5244d.hpp -- include that.
//
namespace hal::pico_5244d
{
    struct Plan;

    //
    // Pico Technology PicoScope 5244D: two channels, 200 MHz, 1 GS/s at 8 bits,
    // 8 to 16 bit flexible resolution, 512 MS shared sample memory, USB 3.
    //
    // The script-facing surface is the DSOX1202G's -- Setup, Arm/Await, Measure,
    // Fetch, the same fourteen ports -- so a script moves between the two with
    // the differences its compiler reports (range() for voltsPerDivision(), no
    // trigger coupling). What is underneath is not alike at all:
    //
    //   - There is no SCPI and no session. The unit is driven through Pico's
    //     ps5000a library, loaded at runtime (library.hpp), and `Buses` is USB
    //     only -- the serial number in hal::Usb is what OpenUnit is given.
    //
    //   - It is one face of its box. The built-in signal generator is the
    //     other (hal::pico_5244d::WaveformGenerator, a source), a row of its own
    //     on the same box, and the two share the one open unit through
    //     hal::BoxConnection exactly as the DSOX1202G's share an SCPI session --
    //     see unit.hpp for what is shared and why it is not a session.
    //
    //   - It is a digitiser, not an instrument that measures. A Measure here is
    //     a block capture followed by the calculation in measurements.hpp. See
    //     "What a Measure does" below.
    //
    // -- What a Measure does -------------------------------------------------
    //
    // After a completed Arm/Await, Measure and Fetch read *that* capture, as a
    // stopped DSOX1202G measures its frozen record: Vbase and Vmin of one dip
    // come from the one acquisition. With nothing held, each Measure takes a
    // fresh free-running block capture (no trigger) and measures that. Any
    // Setup, a new Arm and safe() drop the held capture, so a changed range is
    // never measured against a record taken at the old one.
    //
    // A capture whose input overflowed its range is not measured: the extreme
    // samples are wrong in a way no criterion can see, so it is an
    // UnmeasurableReading saying so.
    //
    // -- Deliberately not modelled --------------------------------------------
    //
    //   - Streaming, rapid-block, ETS, the MSO's digital channels, advanced
    //     triggers, downsampling modes.
    //
    class Oscilloscope : public InstrumentTag
    {
        public:
            static constexpr unsigned channel_count = 2;

            enum class Mode
            {
                Vpp, Vmax, Vmin, Vrms, Vaverage, Vbase, Vtop, Vamplitude,
                Frequency, Period, RiseTime, FallTime, PositiveWidth, NegativeWidth
            };

            static constexpr core::quantities::Time kDefaultCaptureTimeout{ 5.0 };
            static constexpr core::quantities::Time kDefaultArmTimeout{ 1.0 };

            //
            // How long an Auto-sweep trigger waits for a crossing before the
            // unit triggers itself, in ms. Long enough that a signal crossing
            // its level at 1 Hz is caught rather than raced; short enough that
            // a silent input does not hold a Measure for the capture timeout.
            //
            static constexpr int kAutoTriggerMs = 1000;

            // Samples in a window when the Setup does not say.
            static constexpr unsigned kDefaultPoints = 2000;

            //
            // USB only. The unit's other connector is its DC power inlet.
            //
            using Buses = BackPanel<Usb>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            Oscilloscope( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            [[nodiscard]] auto address() const -> const Address & { return mConnection.address(); }

            //
            // Nothing at the other end: a Simulated address and no library
            // handed in. An injected library wins, as an injected transport does
            // on the SCPI drivers -- that is how this driver's tests drive the
            // calls without a bench.
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return mConnection.isSimulated();
            }

            //
            // Talk through this library instead of the vendor's -- installed on
            // the box, so the generator face at this address uses it too. Drops
            // any open unit and sends nothing.
            //
            auto useApi( std::shared_ptr<Ps5000a> api) -> void
            {
                resetCapture();

                detail::installApi( mConnection, std::move( api));
            }

            //
            // Point this face at a different unit (preflight's --address). Its
            // sibling faces keep the unit they have, as on every box.
            //
            auto useAddress( const Address & address) -> void
            {
                resetCapture();

                mConnection.useAddress( address);
            }

            //
            // "Pico Technology,5244D,<serial>,<driver version>" off the unit,
            // opening it if need be -- shaped like an *IDN? so the preflight
            // banner and pool acquisition, which read the third field as the
            // serial, need no special case.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            // Stop and release the box's unit, for every face. The next command
            // by any of them opens it again.
            auto closeSession() -> void;

            [[nodiscard]] auto id() const -> InstrumentId { return mId; }

            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto channel() -> Channel<N>
            {
                return Channel<N>{ *this };
            }

            [[nodiscard]] auto trigger()     -> TriggerBuilder     { return TriggerBuilder{ *this }; }
            [[nodiscard]] auto timebase()    -> TimebaseBuilder    { return TimebaseBuilder{ *this }; }
            [[nodiscard]] auto acquisition() -> AcquisitionBuilder { return AcquisitionBuilder{ *this }; }
            [[nodiscard]] auto single()      -> SingleBuilder      { return SingleBuilder{ *this }; }

            [[nodiscard]] auto mode() const -> Mode { return mMode; }
            [[nodiscard]] auto channelNumber() const -> unsigned { return mChannel; }

            //
            // Stop a capture left running and forget the held one, down a unit
            // that is already open and never opening one -- the argument the
            // DSOX1202G's safe() makes, here with the same two halves: the
            // armed flag is a pending expectation, and the unit would
            // otherwise go on waiting for the next script's stimulus.
            //
            auto safe() -> void;

            auto setMode( const Mode mode) -> void { mMode = mode; }
            auto setChannel( const unsigned channel) -> void { mChannel = channel; }

            auto configureTrigger( const TriggerConfig & config) -> void;
            auto configureTimebase( const TimebaseConfig & config) -> void;
            auto configureAcquisition( const AcquisitionConfig & config) -> void;
            auto configureChannel( const ChannelConfig & config) -> void;

            //
            // Arm: start a block capture and return once a trigger can produce
            // a complete record -- which is when the pre-trigger samples have
            // been taken. The SDK offers no "armed" flag (the DSOX1202G's
            // :AER?), so this waits out the fill time; see SingleConfig.
            //
            auto armSingle( const SingleConfig & config) -> void;

            //
            // Await: poll until the capture is ready or the timeout runs out,
            // then read it. A timeout answers false and stops the unit, a
            // result and not a fault, as on the DSOX1202G. Awaiting what was
            // never armed answers false.
            //
            [[nodiscard]]
            auto awaitAcquisition( const SingleConfig & config) -> bool;

            [[nodiscard]]
            auto fetchWaveform( const WaveformConfig & config) -> core::Waveform;

            // --- Test/simulation hooks ---
            //
            // One hook, not a dozen: a simulated channel holds a trace, and
            // every measurement is computed from it by the same code a real
            // capture goes through. A hook per measurement would let a test
            // assert a Vbase no trace could produce.

            auto setSimulatedTrace( const unsigned channel, core::Waveform trace) -> void
            {
                atChannel( channel).Trace = std::move( trace);
            }

            auto setSimulatedCaptureCompletes( const bool completes) -> void { mCaptureCompletes = completes; }

            [[nodiscard]] auto isArmed() const -> bool { return mArmed; }
            [[nodiscard]] auto lastAcquisitionCompleted() const -> bool { return mCompleted; }

            // --- Settings readback ---

            [[nodiscard]] auto triggerSource() const     -> std::optional<unsigned>                  { return mTriggerSource; }
            [[nodiscard]] auto triggerSlope() const      -> std::optional<TriggerSlope>              { return mTriggerSlope; }
            [[nodiscard]] auto triggerLevel() const      -> std::optional<core::quantities::Voltage> { return mTriggerLevel; }
            [[nodiscard]] auto triggerSweep() const      -> std::optional<TriggerSweep>              { return mTriggerSweep; }
            [[nodiscard]] auto timePerDivision() const   -> std::optional<core::quantities::Time>    { return mTimePerDivision; }
            [[nodiscard]] auto timebasePosition() const  -> std::optional<core::quantities::Time>    { return mTimebasePosition; }
            [[nodiscard]] auto timebaseReference() const -> std::optional<TimebaseReference>         { return mTimebaseReference; }
            [[nodiscard]] auto points() const            -> std::optional<unsigned>                  { return mPoints; }
            [[nodiscard]] auto resolution() const        -> std::optional<Resolution>                { return mResolution; }

            [[nodiscard]] auto channelCoupling( const unsigned channel) -> std::optional<Coupling>                      { return atChannel( channel).InputCoupling; }
            [[nodiscard]] auto channelRange( const unsigned channel) -> std::optional<core::quantities::Voltage>        { return atChannel( channel).Range; }
            [[nodiscard]] auto verticalOffset( const unsigned channel) -> std::optional<core::quantities::Voltage>      { return atChannel( channel).VerticalOffset; }
            [[nodiscard]] auto channelBandwidth( const unsigned channel) -> std::optional<Bandwidth>                    { return atChannel( channel).BandwidthLimit; }
            [[nodiscard]] auto probeAttenuation( const unsigned channel) -> std::optional<double>                       { return atChannel( channel).ProbeAttenuation; }
            [[nodiscard]] auto channelDisplay( const unsigned channel) -> std::optional<ChannelDisplay>                 { return atChannel( channel).Display; }

            //
            // The one rawMeasure() Port<Q, Oscilloscope> calls. Throws
            // core::UnmeasurableReading -- with the *specific* reason, which the
            // DSOX1202G cannot give -- when the record does not support the
            // question; anything else (a unit that is gone, a channel never set
            // up) is a fault and propagates.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & setup) -> QuantityT
            {
                static_assert( std::is_same_v<QuantityT, core::quantities::Voltage>
                            || std::is_same_v<QuantityT, core::quantities::Frequency>
                            || std::is_same_v<QuantityT, core::quantities::Time>,
                               "PicoScope 5244D has no port for this quantity");

                return QuantityT{ measureNow( mMode, mChannel, setup.LowThreshold, setup.HighThreshold) };
            }

            // Everything below is the implementation's; it is here because a
            // class cannot declare its members elsewhere.
            struct Pending;
            struct Capture;

        private:
            [[nodiscard]]
            auto measureNow( Mode mode, unsigned channel,
                             std::optional<double> lowThreshold,
                             std::optional<double> highThreshold) -> double;

            struct ChannelState
            {
                std::optional<Coupling>                     InputCoupling;
                std::optional<core::quantities::Voltage>    Range;
                std::optional<core::quantities::Voltage>    VerticalOffset;
                std::optional<Bandwidth>                    BandwidthLimit;
                std::optional<double>                       ProbeAttenuation;
                std::optional<ChannelDisplay>               Display;

                // What a simulated capture of this input holds.
                core::Waveform                              Trace{};
            };

            [[nodiscard]]
            auto atChannel( const unsigned channel) -> ChannelState &
            {
                return mChannels.at( channel - 1);
            }

            //
            // The box's open unit, opened and checked on first use by either
            // face, with this face's setup applied to it if it is not the unit
            // the setup was last applied to.
            //
            [[nodiscard]] auto session() -> Unit &;
            [[nodiscard]] auto planCapture( Unit & unit) -> Plan;
            [[nodiscard]] auto beginCapture( Unit & unit, bool triggered) -> std::shared_ptr<Pending>;
            [[nodiscard]] auto finishCapture( Unit & unit, Pending & pending,
                                              core::quantities::Time timeout) -> std::shared_ptr<const Capture>;
            auto pushChannel( Unit & unit, unsigned channel) -> void;
            auto pushAllChannels( Unit & unit) -> void;

            // Put the unit in the resolution this face was told, if it is not;
            // true if that changed it (and so the channels need pushing again).
            [[nodiscard]] auto applyResolution( Unit & unit) -> bool;

            // Forget the capture, the armed state and the record of what the
            // unit was given: for a different unit, or none.
            auto resetCapture() -> void;

            // The capture a Measure or Fetch reads: held, simulated, or fresh.
            [[nodiscard]] auto capture() -> std::shared_ptr<const Capture>;
            [[nodiscard]] auto freshCapture() -> std::shared_ptr<const Capture>;
            [[nodiscard]] auto simulatedCapture() -> std::shared_ptr<const Capture>;
            auto invalidate() -> void;

            // The conversion of ADC counts to volts at the probe tip, for one
            // channel, as last given to the unit.
            struct Scaling
            {
                double Range{};         // volts at the instrument's input, full scale
                double PicoOffset{};    // the SDK's analog offset: volts added before digitising
                double Attenuation{ 1.0 };
            };

            InstrumentId                    mId;

            //
            // The box's connection, shared with the generator face on this
            // address -- see unit.hpp.
            //
            BoxConnection                   mConnection;

            // Which open of the unit this face's setup was last applied to.
            std::uint64_t                   mAppliedTo{ 0 };
            std::array<std::optional<Scaling>, channel_count> mApplied{};

            std::shared_ptr<Pending>        mPending;
            std::shared_ptr<const Capture>  mHeld;

            Mode                            mMode{ Mode::Vpp };
            unsigned                        mChannel{ 1 };
            std::array<ChannelState, channel_count> mChannels;

            std::optional<unsigned>                     mTriggerSource;
            std::optional<TriggerSlope>                 mTriggerSlope;
            std::optional<core::quantities::Voltage>    mTriggerLevel;
            std::optional<TriggerSweep>                 mTriggerSweep;

            std::optional<core::quantities::Time>       mTimePerDivision;
            std::optional<core::quantities::Time>       mTimebasePosition;
            std::optional<TimebaseReference>            mTimebaseReference;
            std::optional<unsigned>                     mPoints;

            std::optional<Resolution>                   mResolution;

            bool                                        mArmed{ false };
            bool                                        mCompleted{ false };
            bool                                        mCaptureCompletes{ true };
    };
} // namespace hal::pico_5244d

#include "hal/pico_5244d/channel_view.hpp"
#include "hal/pico_5244d/customization.hpp"
