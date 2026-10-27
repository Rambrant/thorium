#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

//
// The words this driver's builders and ports use. Part of hal/pico_5244d.hpp.
//
// Deliberately narrower than hal::keysight_dsox1202g's vocabulary, and each
// missing enumerator is a fact about the PicoScope rather than an omission: it
// has no AC trigger coupling, no holdoff and no reject filter (its trigger is
// a comparator on the digitised signal), and it has no averaging or peak-detect
// acquisition type in block mode -- what it has instead is a *resolution*, 8 to
// 16 bits, traded against sample rate and channel count.
//
namespace hal::pico_5244d
{
    enum class Coupling
    {
        Dc,
        Ac
    };

    //
    // The 5244D's one bandwidth limiter is 20 MHz. Named Limited, as the
    // DSOX1202G's is, so a script that only says Full or Limited reads the same
    // on both.
    //
    enum class Bandwidth
    {
        Full,
        Limited
    };

    enum class ChannelDisplay
    {
        On,
        Off
    };

    //
    // ADC resolution. The value is the SDK's PS5000A_DEVICE_RESOLUTION, and it
    // is the one setting here that is not free: more bits means fewer samples a
    // second and, at 15 and 16 bits, fewer channels in use (the unit refuses a
    // combination it cannot do, and that refusal is surfaced as it is).
    //
    enum class Resolution
    {
        Bits8  = 0,
        Bits12 = 1,
        Bits14 = 2,
        Bits15 = 3,
        Bits16 = 4
    };

    enum class TriggerSweep
    {
        Auto,
        Normal
    };

    enum class TriggerSlope
    {
        Rising,
        Falling,
        Either
    };

    enum class TimebaseReference
    {
        Left,
        Center,
        Right
    };

    class Oscilloscope;

    template<unsigned N>
    concept ValidChannel = ( N >= 1 && N <= 2);

    template<unsigned N>
        requires ValidChannel<N>
    class Channel;

    //
    // A setting this instrument cannot honour -- a range above +/-20 V, a
    // window too long for the sample memory. Thrown before anything is sent, for
    // the reason the 33522B's is: a clamped setting runs on at the wrong value.
    //
    class SettingOutOfRange : public std::out_of_range
    {
        public:
            SettingOutOfRange( const std::string & instrument, const std::string_view setting,
                               const std::string & detail) :
                std::out_of_range( instrument + ": " + std::string( setting) + " " + detail
                                   + " -- see instruments/pico_5244d/README.md")
            {}
    };

    //
    // A capture, measurement or fetch needing something no Setup ever gave: a
    // channel with no range, a timebase with no scale. Unlike the DSOX1202G
    // there is no front-panel state to inherit, so a channel nobody gave a range
    // is not "whatever it was left at" but "off" -- and a script that measures
    // it has a bug the run should report, not a plausible number.
    //
    class NotSetUp : public std::logic_error
    {
        public:
            explicit NotSetUp( const std::string & what) : std::logic_error( what) {}
    };
} // namespace hal::pico_5244d
