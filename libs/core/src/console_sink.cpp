#include "core/console_sink.hpp"

#include <ostream>

namespace core
{
    namespace
    {
        //
        // SGR sequences matching core::RtfSink's colour table, colour for
        // colour, so the terminal view and the RTF file are recognisably the
        // same log rather than two differently-themed ones. Bright variants for
        // pass/fail: these are the two lines an operator scans for.
        //
        auto ansiFor( const Emphasis emphasis) -> std::string_view
        {
            switch( emphasis)
            {
                case Emphasis::Plain:   return "";
                case Emphasis::Heading: return "\033[1;34m";
                case Emphasis::Detail:  return "\033[90m";
                case Emphasis::Pass:    return "\033[1;32m";
                case Emphasis::Fail:    return "\033[1;31m";
                case Emphasis::Warning: return "\033[1;33m";
            }

            return "";
        }

        constexpr std::string_view kReset = "\033[0m";
    } // namespace

    ConsoleSink::ConsoleSink( std::ostream & out, const bool colour) :
        mOut( &out),
        mColour( colour)
    {}

    auto ConsoleSink::writeAll( const std::vector<ReportLine> & lines) -> void
    {
        if( lines.empty())
        {
            return;
        }

        for( const auto & line : lines)
        {
            const auto colour = mColour ? ansiFor( line.Style) : std::string_view{};

            //
            // The reset is emitted only when something was actually set, so a
            // --no-color run's output is byte-for-byte plain text rather than
            // plain text sprinkled with bare resets.
            //
            if( colour.empty())
            {
                *mOut << line.Text << '\n';
            }
            else
            {
                *mOut << colour << line.Text << kReset << '\n';
            }
        }

        //
        // Flushed per group, not left to the stream: an operator watching a
        // long test needs the line for the measurement that just happened, not
        // whenever the buffer happens to fill.
        //
        mOut->flush();
    }

    auto ConsoleSink::onRunStart( const RunInfo & info) -> void
    {
        writeAll( humanHeaderLines( info));
    }

    auto ConsoleSink::onTestStart( const std::string_view group, const std::string_view test, const std::string_view description) -> void
    {
        writeAll( humanTestHeadingLines( group, test, description));
    }

    auto ConsoleSink::onEvent( const JournalEvent & event) -> void
    {
        writeAll( humanEventLines( event));
    }

    auto ConsoleSink::onTestEnd( const std::string_view group, const std::string_view test, const bool passed) -> void
    {
        writeAll( humanTestResultLines( group, test, passed));
    }

    auto ConsoleSink::onRunEnd( const bool allPassed) -> void
    {
        writeAll( humanSummaryLines( allPassed));
    }
} // namespace core
