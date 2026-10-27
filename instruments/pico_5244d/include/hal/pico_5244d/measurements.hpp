#pragma once

#include <expected>
#include <span>
#include <string_view>

//
// The scope's :MEASure subsystem, done on the host.
//
// A PicoScope is a digitiser: it returns samples and nothing else. Every
// number the DSOX1202G answers from firmware -- Vpp, Vbase, a rise time -- is
// computed here from the record a block capture returned, with the definitions
// an oscilloscope uses:
//
//   Vmax, Vmin   the extreme samples. One spike moves them.
//   Vtop, Vbase  the settled levels: the most populated histogram bin in the
//                upper and in the lower half of the record's range, refined to
//                the mean of the samples in that bin. A spike does not move
//                them -- which is why a dip's depth is vbase - vmin.
//   Vamplitude   Vtop - Vbase.
//   Vrms         DC RMS of the whole record: sqrt( mean( v^2)).
//   Vaverage     the mean of the whole record.
//   Period       the mean spacing of rising crossings of the 50 % level, found
//                with hysteresis so noise on the edge is not counted as edges.
//   Rise, Fall   the time between the low and high threshold fractions of
//                Vamplitude on the first complete edge, interpolated between
//                samples.
//   Widths       the time between a crossing of the 50 % level and the next
//                crossing the other way, on the first complete pulse.
//
// Pure: no instrument, no state, no I/O. That is the point of keeping them
// apart from the driver -- every definition above is testable against a
// waveform whose answer is known, which the DSOX1202G's firmware never was.
//
// Each returns a number or the reason there is none. The reason is carried
// into core::UnmeasurableReading, so unlike the DSOX1202G -- which says "could
// not" and nothing else -- a script's whenUnmeasurable handler can tell "no
// edge in the record" from "the trace is flat".
//
namespace hal::pico_5244d::measure
{
    using Result = std::expected<double, std::string_view>;

    struct Levels
    {
        double Base{};
        double Top{};
    };

    [[nodiscard]] auto vmax( std::span<const double> samples)     -> Result;
    [[nodiscard]] auto vmin( std::span<const double> samples)     -> Result;
    [[nodiscard]] auto vpp( std::span<const double> samples)      -> Result;
    [[nodiscard]] auto vrms( std::span<const double> samples)     -> Result;
    [[nodiscard]] auto vaverage( std::span<const double> samples) -> Result;

    // Fails for an empty or flat record: a level needs two to tell apart.
    [[nodiscard]] auto levels( std::span<const double> samples)   -> std::expected<Levels, std::string_view>;

    [[nodiscard]] auto vbase( std::span<const double> samples)      -> Result;
    [[nodiscard]] auto vtop( std::span<const double> samples)       -> Result;
    [[nodiscard]] auto vamplitude( std::span<const double> samples) -> Result;

    // interval is the time between samples, in seconds.
    [[nodiscard]] auto period( std::span<const double> samples, double interval)    -> Result;
    [[nodiscard]] auto frequency( std::span<const double> samples, double interval) -> Result;

    // low and high are fractions of the amplitude, 0 < low < high < 1.
    [[nodiscard]] auto riseTime( std::span<const double> samples, double interval, double low, double high) -> Result;
    [[nodiscard]] auto fallTime( std::span<const double> samples, double interval, double low, double high) -> Result;

    [[nodiscard]] auto positiveWidth( std::span<const double> samples, double interval) -> Result;
    [[nodiscard]] auto negativeWidth( std::span<const double> samples, double interval) -> Result;
} // namespace hal::pico_5244d::measure
