#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

//
// The slice of Pico Technology's ps5000a API this driver uses, as an interface.
//
// -- Why an interface over a library the vendor ships ---------------------
//
// The 5244D has no SCPI. Its only supported programming interface is the
// ps5000a library (ps5000a.dll, libps5000a.so, libps5000a.dylib): the
// firmware is uploaded and the USB protocol spoken by that library, and
// reimplementing either is neither documented nor something a test rig should
// depend on. So this is the one driver in the tree that cannot avoid a vendor
// binary -- and, like hal/io/visa_transport.hpp, it avoids everything else
// about one:
//
//   Loaded, not linked. No ps5000aApi.h, no import library, no find_package.
//   The library is opened at runtime and its sixteen entry points looked up by
//   name (src/library.cpp), so the framework builds on a machine with no
//   PicoSDK and the failure becomes "no ps5000a library found" at the moment
//   somebody asks for a reading.
//
//   An interface, so the rest of the driver is testable. Everything in
//   oscilloscope.cpp talks to Ps5000a, never to a function pointer; a test
//   hands in a fake and asserts the calls, which is the only way to check the
//   sequence (resolution, channels, timebase, run, poll, read) without a
//   bench.
//
// The signatures are declared by hand and so a typo is a crash rather than a
// compile error; each is written beside the vendor's own prototype in
// src/library.cpp for that reason. They were written from the PicoScope 5000
// Series (A API) Programmer's Guide and cross-checked against Pico's own
// Python wrappers. They have NOT been run against a unit yet -- see the
// README.
//
// Every method returns the SDK's PICO_STATUS unchanged (0 is PICO_OK) and
// leaves what to do about it to the caller, because what a non-zero status
// means depends on the call: a refused GetTimebase2 is how a timebase search
// learns where the valid ones start, and is not a fault at all.
//
namespace hal::pico_5244d
{
    using Status = std::uint32_t;

    inline constexpr Status kPicoOk = 0;

    //
    // The two statuses OpenUnit answers when the unit is on USB power alone
    // (PICO_POWER_SUPPLY_NOT_CONNECTED) or on a USB 2.0 port
    // (PICO_USB3_0_DEVICE_NON_USB3_0_PORT). Both are recoverable by telling the
    // driver to carry on with the power it has -- ChangePowerSource -- which a
    // two-channel unit can do. See Oscilloscope's open.
    //
    inline constexpr Status kPicoPowerSupplyNotConnected = 0x0000011A;
    inline constexpr Status kPicoUsb3DeviceOnNonUsb3Port = 0x0000011E;

    //
    // PICO_INFO selectors for GetUnitInfo.
    //
    inline constexpr std::uint32_t kInfoDriverVersion  = 0;
    inline constexpr std::uint32_t kInfoVariant        = 3;
    inline constexpr std::uint32_t kInfoBatchAndSerial = 4;

    //
    // The status in words where this driver knows them, or its hex value. The
    // table is partial on purpose -- PicoStatus.h is the reference and holds
    // several hundred -- and covers what a bring-up actually meets.
    //
    [[nodiscard]]
    auto describeStatus( Status status) -> std::string;

    //
    // The failure of one SDK call, naming it. A TransportError, because that is
    // what preflight's pool acquisition catches and what "the instrument is
    // not there / not answering" has meant everywhere else in this tree.
    //
    class PicoFault : public io::TransportError
    {
        public:
            PicoFault( std::string_view call, Status status);

            // With what the caller knows that the status does not say -- which
            // units are attached, say.
            PicoFault( std::string_view call, Status status, const std::string & detail);

            [[nodiscard]]
            auto status() const -> Status
            {
                return mStatus;
            }

        private:
            Status mStatus;
    };

    class Ps5000a
    {
        public:
            Ps5000a()                                      = default;
            Ps5000a( const Ps5000a &)                      = delete;
            auto operator=( const Ps5000a &) -> Ps5000a &  = delete;
            virtual ~Ps5000a()                             = default;

            // The serial numbers of every unit attached, comma separated.
            virtual auto enumerateUnits( std::string & serials)                                   -> Status = 0;

            // serial is empty for "the first unit found".
            virtual auto openUnit( std::int16_t & handle, const std::string & serial,
                                   std::int32_t resolution)                                       -> Status = 0;
            virtual auto closeUnit( std::int16_t handle)                                          -> Status = 0;
            virtual auto changePowerSource( std::int16_t handle, Status powerState)               -> Status = 0;
            virtual auto unitInfo( std::int16_t handle, std::uint32_t info, std::string & text)   -> Status = 0;

            virtual auto setDeviceResolution( std::int16_t handle, std::int32_t resolution)       -> Status = 0;
            virtual auto maximumValue( std::int16_t handle, std::int16_t & adcMaximum)            -> Status = 0;

            virtual auto setChannel( std::int16_t handle, std::int32_t channel, bool enabled,
                                     std::int32_t coupling, std::int32_t range, float offset)     -> Status = 0;
            virtual auto analogueOffsetLimits( std::int16_t handle, std::int32_t range,
                                               std::int32_t coupling, float & maximum,
                                               float & minimum)                                   -> Status = 0;
            virtual auto setBandwidthFilter( std::int16_t handle, std::int32_t channel,
                                             std::int32_t bandwidth)                              -> Status = 0;

            virtual auto setSimpleTrigger( std::int16_t handle, bool enable, std::int32_t source,
                                           std::int16_t thresholdAdc, std::int32_t direction,
                                           std::uint32_t delaySamples,
                                           std::int16_t autoTriggerMs)                            -> Status = 0;

            virtual auto timebase( std::int16_t handle, std::uint32_t timebase, std::int32_t samples,
                                   float & intervalNs, std::int32_t & maxSamples)                 -> Status = 0;

            virtual auto setDataBuffer( std::int16_t handle, std::int32_t channel,
                                        std::int16_t * buffer, std::int32_t length)               -> Status = 0;
            virtual auto runBlock( std::int16_t handle, std::int32_t preTrigger,
                                   std::int32_t postTrigger, std::uint32_t timebase,
                                   std::int32_t & timeIndisposedMs)                               -> Status = 0;
            virtual auto isReady( std::int16_t handle, bool & ready)                              -> Status = 0;
            virtual auto stop( std::int16_t handle)                                               -> Status = 0;
            virtual auto getValues( std::int16_t handle, std::uint32_t & samples,
                                    std::int16_t & overflowMask)                                  -> Status = 0;

            //
            // The built-in signal generator, one continuous waveform, started
            // at once: offset and peak-to-peak in microvolts, the SDK's
            // PS5000A_WAVE_TYPE, a frequency in hertz. See
            // WaveformGenerator for what is sent for which shape.
            //
            virtual auto setSigGen( std::int16_t handle, std::int32_t offsetMicrovolts,
                                    std::uint32_t peakToPeakMicrovolts, std::int32_t waveType,
                                    std::int32_t operation, double hertz)                                 -> Status = 0;

            //
            // The arbitrary waveform generator, which is how a triangle and the
            // two ramps are made: the built-in generator plays those at half
            // the frequency asked for, so the driver draws one period itself.
            //
            struct ArbitraryLimits
            {
                std::int16_t  MinValue{ 0 };
                std::int16_t  MaxValue{ 0 };
                std::uint32_t MinSize{ 0 };
                std::uint32_t MaxSize{ 0 };
            };

            virtual auto arbitraryLimits( std::int16_t handle, ArbitraryLimits & limits)      -> Status = 0;

            // The phase step that plays a buffer of this many samples at this frequency.
            virtual auto frequencyToPhase( std::int16_t handle, double hertz, std::uint32_t samples,
                                           std::uint32_t & phase)                             -> Status = 0;

            // One period, played continuously from now on at a constant phase step.
            virtual auto setSigGenArbitrary( std::int16_t handle, std::int32_t offsetMicrovolts,
                                             std::uint32_t peakToPeakMicrovolts, std::uint32_t phase,
                                             std::int16_t * samples, std::int32_t count)      -> Status = 0;
    };

    //
    // The vendor's library, loaded once per process and kept -- never
    // unloaded, for the reason visa_transport.cpp gives. THORIUM_PS5000A_LIBRARY
    // names one explicitly (a PicoSDK in a place no loader looks, which on
    // macOS is the usual case: PicoScope 7 keeps its libraries inside its
    // application bundle).
    //
    // Throws io::UnsupportedTransport saying what was tried if none loads, or
    // if one loads that lacks an entry point.
    //
    [[nodiscard]]
    auto loadPs5000a() -> std::shared_ptr<Ps5000a>;

    // Which file loaded, or nothing -- for a bring-up report and for a test.
    [[nodiscard]]
    auto ps5000aLibraryPath() -> std::string;
} // namespace hal::pico_5244d
