#include "hal/pico_5244d/library.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace hal::pico_5244d
{
    auto describeStatus( const Status status) -> std::string
    {
        //
        // PicoStatus.h, the low end of it: the codes a first bring-up meets.
        // Anything else is shown as hex, which is what the vendor's header and
        // forum answers are indexed by anyway.
        //
        static constexpr std::array<const char *, 0x12> kLow{
            "PICO_OK",
            "PICO_MAX_UNITS_OPENED -- the maximum number of units is already open",
            "PICO_MEMORY_FAIL",
            "PICO_NOT_FOUND -- no unit found (not plugged in, or already open in another program)",
            "PICO_FW_FAIL -- firmware download failed",
            "PICO_OPEN_OPERATION_IN_PROGRESS",
            "PICO_OPERATION_FAILED",
            "PICO_NOT_RESPONDING",
            "PICO_CONFIG_FAIL",
            "PICO_KERNEL_DRIVER_TOO_OLD",
            "PICO_EEPROM_CORRUPT",
            "PICO_OS_NOT_SUPPORTED",
            "PICO_INVALID_HANDLE",
            "PICO_INVALID_PARAMETER",
            "PICO_INVALID_TIMEBASE",
            "PICO_INVALID_VOLTAGE_RANGE",
            "PICO_INVALID_CHANNEL",
            "PICO_INVALID_TRIGGER_CHANNEL" };

        char hex[ 16];
        std::snprintf( hex, sizeof hex, "0x%08X", static_cast<unsigned>( status));

        if( status < kLow.size())
        {
            return std::string( hex) + " " + kLow[ status];
        }

        if( status == kPicoPowerSupplyNotConnected)
        {
            return std::string( hex) + " PICO_POWER_SUPPLY_NOT_CONNECTED";
        }

        if( status == kPicoUsb3DeviceOnNonUsb3Port)
        {
            return std::string( hex) + " PICO_USB3_0_DEVICE_NON_USB3_0_PORT";
        }

        return std::string( hex);
    }

    PicoFault::PicoFault( const std::string_view call, const Status status) :
        io::TransportError( "PicoScope " + std::string( call) + " failed: " + describeStatus( status)),
        mStatus( status)
    {}

    PicoFault::PicoFault( const std::string_view call, const Status status, const std::string & detail) :
        io::TransportError( "PicoScope " + std::string( call) + " failed: " + describeStatus( status) + " -- " + detail),
        mStatus( status)
    {}

    namespace
    {
#if defined( _WIN32) && !defined( _WIN64)
    #define THORIUM_PICO_CALL __stdcall
#else
    #define THORIUM_PICO_CALL
#endif

        //
        // The vendor's prototypes, from ps5000aApi.h. PICO_STATUS is uint32_t,
        // the handle int16_t, enums are 32-bit, and "int8_t *" is the SDK's
        // spelling of char *. Every name is the SDK's, so a line can be checked
        // against the header by eye.
        //
        struct Entry
        {
            // ps5000aEnumerateUnits( int16_t * count, int8_t * serials, int16_t * serialLth)
            Status ( THORIUM_PICO_CALL * EnumerateUnits)( std::int16_t *, char *, std::int16_t *);

            // ps5000aOpenUnit( int16_t * handle, int8_t * serial, PS5000A_DEVICE_RESOLUTION resolution)
            Status ( THORIUM_PICO_CALL * OpenUnit)( std::int16_t *, const char *, std::int32_t);

            // ps5000aCloseUnit( int16_t handle)
            Status ( THORIUM_PICO_CALL * CloseUnit)( std::int16_t);

            // ps5000aChangePowerSource( int16_t handle, PICO_STATUS powerState)
            Status ( THORIUM_PICO_CALL * ChangePowerSource)( std::int16_t, Status);

            // ps5000aGetUnitInfo( int16_t handle, int8_t * string, int16_t stringLength,
            //                     int16_t * requiredSize, PICO_INFO info)
            Status ( THORIUM_PICO_CALL * GetUnitInfo)( std::int16_t, char *, std::int16_t, std::int16_t *, std::uint32_t);

            // ps5000aSetDeviceResolution( int16_t handle, PS5000A_DEVICE_RESOLUTION resolution)
            Status ( THORIUM_PICO_CALL * SetDeviceResolution)( std::int16_t, std::int32_t);

            // ps5000aMaximumValue( int16_t handle, int16_t * value)
            Status ( THORIUM_PICO_CALL * MaximumValue)( std::int16_t, std::int16_t *);

            // ps5000aSetChannel( int16_t handle, PS5000A_CHANNEL channel, int16_t enabled,
            //                    PS5000A_COUPLING type, PS5000A_RANGE range, float analogOffset)
            Status ( THORIUM_PICO_CALL * SetChannel)( std::int16_t, std::int32_t, std::int16_t, std::int32_t,
                                                      std::int32_t, float);

            // ps5000aGetAnalogueOffset( int16_t handle, PS5000A_RANGE range, PS5000A_COUPLING coupling,
            //                           float * maximumVoltage, float * minimumVoltage)
            Status ( THORIUM_PICO_CALL * GetAnalogueOffset)( std::int16_t, std::int32_t, std::int32_t,
                                                             float *, float *);

            // ps5000aSetBandwidthFilter( int16_t handle, PS5000A_CHANNEL channel,
            //                            PS5000A_BANDWIDTH_LIMITER bandwidth)
            Status ( THORIUM_PICO_CALL * SetBandwidthFilter)( std::int16_t, std::int32_t, std::int32_t);

            // ps5000aSetSimpleTrigger( int16_t handle, int16_t enable, PS5000A_CHANNEL source,
            //                          int16_t threshold, PS5000A_THRESHOLD_DIRECTION direction,
            //                          uint32_t delay, int16_t autoTrigger_ms)
            Status ( THORIUM_PICO_CALL * SetSimpleTrigger)( std::int16_t, std::int16_t, std::int32_t, std::int16_t,
                                                            std::int32_t, std::uint32_t, std::int16_t);

            // ps5000aGetTimebase2( int16_t handle, uint32_t timebase, int32_t noSamples,
            //                      float * timeIntervalNanoseconds, int32_t * maxSamples,
            //                      uint32_t segmentIndex)
            Status ( THORIUM_PICO_CALL * GetTimebase2)( std::int16_t, std::uint32_t, std::int32_t, float *,
                                                        std::int32_t *, std::uint32_t);

            // ps5000aSetDataBuffer( int16_t handle, PS5000A_CHANNEL channel, int16_t * buffer,
            //                       int32_t bufferLth, uint32_t segmentIndex, PS5000A_RATIO_MODE mode)
            Status ( THORIUM_PICO_CALL * SetDataBuffer)( std::int16_t, std::int32_t, std::int16_t *, std::int32_t,
                                                         std::uint32_t, std::int32_t);

            // ps5000aRunBlock( int16_t handle, int32_t noOfPreTriggerSamples,
            //                  int32_t noOfPostTriggerSamples, uint32_t timebase,
            //                  int32_t * timeIndisposedMs, uint32_t segmentIndex,
            //                  ps5000aBlockReady lpReady, void * pParameter)
            Status ( THORIUM_PICO_CALL * RunBlock)( std::int16_t, std::int32_t, std::int32_t, std::uint32_t,
                                                    std::int32_t *, std::uint32_t, void *, void *);

            // ps5000aIsReady( int16_t handle, int16_t * ready)
            Status ( THORIUM_PICO_CALL * IsReady)( std::int16_t, std::int16_t *);

            // ps5000aStop( int16_t handle)
            Status ( THORIUM_PICO_CALL * Stop)( std::int16_t);

            // ps5000aGetValues( int16_t handle, uint32_t startIndex, uint32_t * noOfSamples,
            //                   uint32_t downSampleRatio, PS5000A_RATIO_MODE downSampleRatioMode,
            //                   uint32_t segmentIndex, int16_t * overflow)
            Status ( THORIUM_PICO_CALL * GetValues)( std::int16_t, std::uint32_t, std::uint32_t *, std::uint32_t,
                                                     std::int32_t, std::uint32_t, std::int16_t *);

            // ps5000aSetSigGenBuiltInV2( int16_t handle, int32_t offsetVoltage, uint32_t pkToPk,
            //                            PS5000A_WAVE_TYPE waveType, double startFrequency,
            //                            double stopFrequency, double increment, double dwellTime,
            //                            PS5000A_SWEEP_TYPE sweepType, PS5000A_EXTRA_OPERATIONS operation,
            //                            uint32_t shots, uint32_t sweeps, PS5000A_SIGGEN_TRIG_TYPE triggerType,
            //                            PS5000A_SIGGEN_TRIG_SOURCE triggerSource, int16_t extInThreshold)
            Status ( THORIUM_PICO_CALL * SetSigGenBuiltInV2)( std::int16_t, std::int32_t, std::uint32_t, std::int32_t,
                                                              double, double, double, double, std::int32_t,
                                                              std::int32_t, std::uint32_t, std::uint32_t,
                                                              std::int32_t, std::int32_t, std::int16_t);
        };

#ifdef _WIN32
        using LibraryHandle = HMODULE;

        auto loadLibraryNamed( const char * name) -> LibraryHandle
        {
            return ::LoadLibraryA( name);
        }

        auto symbolIn( const LibraryHandle library, const char * name) -> void *
        {
            return reinterpret_cast<void *>( ::GetProcAddress( library, name));
        }

        constexpr std::array kCandidates{ "ps5000a.dll", "C:\\Program Files\\PicoTechnology\\SDK\\bin\\ps5000a.dll", "C:\\Program Files (x86)\\PicoTechnology\\SDK\\bin\\ps5000a.dll", "C:\\Program Files\\Pico Technology\\PicoScope 7 T&M Stable\\ps5000a.dll" };
#else
        using LibraryHandle = void *;

        auto loadLibraryNamed( const char * name) -> LibraryHandle
        {
            return ::dlopen( name, RTLD_NOW | RTLD_LOCAL);
        }

        auto symbolIn( const LibraryHandle library, const char * name) -> void *
        {
            return ::dlsym( library, name);
        }

        //
        // Linux: the sonames Pico's packages install. macOS: the bare name,
        // for a PicoSDK installed where the loader looks, and nothing else --
        // PicoScope 7's own copy lives inside its application bundle at a path
        // that changes with the release, which is what
        // THORIUM_PS5000A_LIBRARY is for.
        //
        constexpr std::array kCandidates{
            "libps5000a.so.2",
            "libps5000a.so",
            "libps5000a.dylib",
            "/Library/Frameworks/PicoSDK.framework/Libraries/libps5000a/libps5000a.dylib" };
#endif

        struct Loaded
        {
            Entry       Api{};
            std::string Path;
        };

        auto tryLoad( const char * candidate, Loaded & out) -> bool
        {
            const LibraryHandle library = loadLibraryNamed( candidate);

            if( library == nullptr)
            {
                return false;
            }

            const auto bind = [ library]( auto & slot, const char * name)
            {
                slot = reinterpret_cast<std::remove_reference_t<decltype( slot)>>( symbolIn( library, name));

                return slot != nullptr;
            };

            Entry api{};

            //
            // Every entry point or none -- a library that loaded without one is
            // not one this driver can use, and finding out mid-capture would
            // be worse than finding out here.
            //
            const bool complete =
                   bind( api.EnumerateUnits,    "ps5000aEnumerateUnits")
                && bind( api.OpenUnit,          "ps5000aOpenUnit")
                && bind( api.CloseUnit,         "ps5000aCloseUnit")
                && bind( api.ChangePowerSource, "ps5000aChangePowerSource")
                && bind( api.GetUnitInfo,       "ps5000aGetUnitInfo")
                && bind( api.SetDeviceResolution, "ps5000aSetDeviceResolution")
                && bind( api.MaximumValue,      "ps5000aMaximumValue")
                && bind( api.SetChannel,        "ps5000aSetChannel")
                && bind( api.GetAnalogueOffset, "ps5000aGetAnalogueOffset")
                && bind( api.SetBandwidthFilter, "ps5000aSetBandwidthFilter")
                && bind( api.SetSimpleTrigger,  "ps5000aSetSimpleTrigger")
                && bind( api.GetTimebase2,      "ps5000aGetTimebase2")
                && bind( api.SetDataBuffer,     "ps5000aSetDataBuffer")
                && bind( api.RunBlock,          "ps5000aRunBlock")
                && bind( api.IsReady,           "ps5000aIsReady")
                && bind( api.Stop,              "ps5000aStop")
                && bind( api.GetValues,         "ps5000aGetValues")
                && bind( api.SetSigGenBuiltInV2, "ps5000aSetSigGenBuiltInV2");

            if( !complete)
            {
                return false;
            }

            out.Api  = api;
            out.Path = candidate;

            return true;
        }

        auto candidateList() -> std::vector<std::string>
        {
            std::vector<std::string> candidates;

            if( const char * override_ = std::getenv( "THORIUM_PS5000A_LIBRARY");
                override_ != nullptr && *override_ != '\0')
            {
                candidates.emplace_back( override_);
            }

            for( const char * candidate : kCandidates)
            {
                candidates.emplace_back( candidate);
            }

            return candidates;
        }

        //
        // Loaded once, kept for the life of the process, initialised on first
        // use rather than during static initialisation (a rig's instruments
        // are globals; nothing vendor-shaped may run before main()).
        //
        auto loaded() -> const Loaded *
        {
            static const Loaded * const found = []() -> const Loaded *
            {
                static Loaded entry;

                for( const auto & candidate : candidateList())
                {
                    if( tryLoad( candidate.c_str(), entry))
                    {
                        return &entry;
                    }
                }

                return nullptr;
            }();

            return found;
        }

        class Library final : public Ps5000a
        {
            public:
                explicit Library( const Entry & api) : mApi( api) {}

                auto enumerateUnits( std::string & serials) -> Status override
                {
                    //
                    // A buffer big enough for any bench's worth of serials (each
                    // is about ten characters); serialLth is its size on entry
                    // and the length of the list on exit.
                    //
                    std::vector<char> buffer( 512, '\0');

                    std::int16_t count  = 0;
                    std::int16_t length = static_cast<std::int16_t>( buffer.size() - 1);

                    const Status status = mApi.EnumerateUnits( &count, buffer.data(), &length);

                    serials = status == kPicoOk ? std::string( buffer.data()) : std::string{};

                    return status;
                }

                auto openUnit( std::int16_t & handle, const std::string & serial,
                               const std::int32_t resolution) -> Status override
                {
                    return mApi.OpenUnit( &handle, serial.empty() ? nullptr : serial.c_str(), resolution);
                }

                auto closeUnit( const std::int16_t handle) -> Status override
                {
                    return mApi.CloseUnit( handle);
                }

                auto changePowerSource( const std::int16_t handle, const Status powerState) -> Status override
                {
                    return mApi.ChangePowerSource( handle, powerState);
                }

                auto unitInfo( const std::int16_t handle, const std::uint32_t info, std::string & text) -> Status override
                {
                    std::vector<char> buffer( 256, '\0');
                    std::int16_t      required = 0;

                    const Status status = mApi.GetUnitInfo(
                        handle, buffer.data(), static_cast<std::int16_t>( buffer.size() - 1), &required, info);

                    text = buffer.data();

                    return status;
                }

                auto setDeviceResolution( const std::int16_t handle, const std::int32_t resolution) -> Status override
                {
                    return mApi.SetDeviceResolution( handle, resolution);
                }

                auto maximumValue( const std::int16_t handle, std::int16_t & adcMaximum) -> Status override
                {
                    return mApi.MaximumValue( handle, &adcMaximum);
                }

                auto setChannel( const std::int16_t handle, const std::int32_t channel, const bool enabled,
                                 const std::int32_t coupling, const std::int32_t range,
                                 const float offset) -> Status override
                {
                    return mApi.SetChannel( handle, channel, enabled ? 1 : 0, coupling, range, offset);
                }

                auto analogueOffsetLimits( const std::int16_t handle, const std::int32_t range,
                                           const std::int32_t coupling, float & maximum,
                                           float & minimum) -> Status override
                {
                    return mApi.GetAnalogueOffset( handle, range, coupling, &maximum, &minimum);
                }

                auto setBandwidthFilter( const std::int16_t handle, const std::int32_t channel,
                                         const std::int32_t bandwidth) -> Status override
                {
                    return mApi.SetBandwidthFilter( handle, channel, bandwidth);
                }

                auto setSimpleTrigger( const std::int16_t handle, const bool enable, const std::int32_t source,
                                       const std::int16_t thresholdAdc, const std::int32_t direction,
                                       const std::uint32_t delaySamples,
                                       const std::int16_t autoTriggerMs) -> Status override
                {
                    return mApi.SetSimpleTrigger( handle, enable ? 1 : 0, source, thresholdAdc, direction,
                                                  delaySamples, autoTriggerMs);
                }

                auto timebase( const std::int16_t handle, const std::uint32_t timebase, const std::int32_t samples,
                               float & intervalNs, std::int32_t & maxSamples) -> Status override
                {
                    return mApi.GetTimebase2( handle, timebase, samples, &intervalNs, &maxSamples, 0);
                }

                auto setDataBuffer( const std::int16_t handle, const std::int32_t channel,
                                    std::int16_t * buffer, const std::int32_t length) -> Status override
                {
                    // Segment 0, and ratio mode NONE (0): block mode, no downsampling.
                    return mApi.SetDataBuffer( handle, channel, buffer, length, 0, 0);
                }

                auto runBlock( const std::int16_t handle, const std::int32_t preTrigger,
                               const std::int32_t postTrigger, const std::uint32_t timebase,
                               std::int32_t & timeIndisposedMs) -> Status override
                {
                    // No callback: readiness is polled with IsReady, which is
                    // the same on every platform and needs no thread of ours.
                    return mApi.RunBlock( handle, preTrigger, postTrigger, timebase, &timeIndisposedMs, 0,
                                          nullptr, nullptr);
                }

                auto isReady( const std::int16_t handle, bool & ready) -> Status override
                {
                    std::int16_t flag = 0;

                    const Status status = mApi.IsReady( handle, &flag);

                    ready = flag != 0;

                    return status;
                }

                auto stop( const std::int16_t handle) -> Status override
                {
                    return mApi.Stop( handle);
                }

                auto getValues( const std::int16_t handle, std::uint32_t & samples,
                                std::int16_t & overflowMask) -> Status override
                {
                    return mApi.GetValues( handle, 0, &samples, 1, 0, 0, &overflowMask);
                }

                //
                // One continuous waveform: start and stop frequency the same
                // (no sweep), no shots or sweeps (so it runs until changed) and
                // no trigger source (so it starts now). Every other argument is
                // the enum's zero, which is "up", "no extra operation" and
                // "rising" -- none of which matters with the rest as stated.
                //
                auto setSigGen( const std::int16_t handle, const std::int32_t offsetMicrovolts,
                                const std::uint32_t peakToPeakMicrovolts, const std::int32_t waveType,
                                const double hertz) -> Status override
                {
                    return mApi.SetSigGenBuiltInV2( handle, offsetMicrovolts, peakToPeakMicrovolts, waveType,
                                                    hertz, hertz, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0);
                }

            private:
                Entry mApi;
        };
    } // namespace

    auto ps5000aLibraryPath() -> std::string
    {
        const auto * entry = loaded();

        return entry ? entry->Path : std::string{};
    }

    auto loadPs5000a() -> std::shared_ptr<Ps5000a>
    {
        const auto * entry = loaded();

        if( entry == nullptr)
        {
            std::string tried;

            for( const auto & candidate : candidateList())
            {
                tried += ( tried.empty() ? "" : ", ") + candidate;
            }

            throw io::UnsupportedTransport(
                "no ps5000a library found (tried " + tried + "). Install the PicoSDK or PicoScope, "
                "or set THORIUM_PS5000A_LIBRARY to the full path of the library");
        }

        return std::make_shared<Library>( entry->Api);
    }
} // namespace hal::pico_5244d
