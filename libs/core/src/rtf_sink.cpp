#include "core/rtf_sink.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>

namespace core
{
    namespace
    {
        //
        // The colour table, in RTF's own 1-based index order. Written out here
        // rather than derived from Emphasis so the table's order and the
        // emphasisFormat() indices below are visibly the same list -- RTF
        // refers to colours by position, so getting them out of step silently
        // recolours the whole log rather than failing.
        //
        //   1 black   body text
        //   2 green   passed
        //   3 red     failed
        //   4 navy    headings
        //   5 grey    supporting metadata
        //   6 amber   warnings
        //
        constexpr std::string_view kColourTable =
            "{\\colortbl ;"
            "\\red0\\green0\\blue0;"
            "\\red0\\green128\\blue0;"
            "\\red192\\green0\\blue0;"
            "\\red0\\green51\\blue153;"
            "\\red112\\green112\\blue112;"
            "\\red176\\green112\\blue0;"
            "}";

        //
        // \f1 is the monospaced font -- the human log is a column layout (see
        // core/report.cpp's width constants), and a proportional font throws
        // every column out of alignment, which defeats the reason the padding
        // is there.
        //
        constexpr std::string_view kFontTable =
            "{\\fonttbl"
            "{\\f0\\fswiss Helvetica;}"
            "{\\f1\\fmodern Menlo;}"
            "}";

        //
        // Per-line character formatting: colour index plus bold/size. Returned
        // as a prefix rather than applied with a persistent state change, so
        // every line is self-contained -- a reader (or a repair tool) can
        // truncate this file anywhere and what remains still renders correctly.
        //
        auto emphasisFormat( const Emphasis emphasis) -> std::string_view
        {
            switch( emphasis)
            {
                case Emphasis::Plain:   return "\\f1\\fs20\\cf1 ";
                case Emphasis::Heading: return "\\f0\\fs26\\b\\cf4 ";
                case Emphasis::Detail:  return "\\f1\\fs18\\cf5 ";
                case Emphasis::Pass:    return "\\f1\\fs20\\b\\cf2 ";
                case Emphasis::Fail:    return "\\f1\\fs20\\b\\cf3 ";
                case Emphasis::Warning: return "\\f1\\fs20\\b\\cf6 ";
            }

            return "\\f1\\fs20\\cf1 ";
        }
    } // namespace

    auto RtfSink::escape( const std::string_view text) -> std::string
    {
        std::string result;
        result.reserve( text.size() + text.size() / 8);

        for( const char c : text)
        {
            switch( c)
            {
                case '\\': result += "\\\\"; break;
                case '{':  result += "\\{";  break;
                case '}':  result += "\\}";  break;

                //
                // A newline inside a single logged line would otherwise be
                // ignored by an RTF reader (raw newlines are whitespace in the
                // format); \line keeps it visible without starting a new
                // paragraph, which is what \par is for and what write() below
                // already appends per line.
                //
                case '\n': result += "\\line "; break;
                case '\r': break;
                case '\t': result += "\\tab ";  break;

                default:
                    if( static_cast<unsigned char>( c) < 0x20)
                    {
                        // Other control bytes have no rendering; dropped
                        // rather than escaped, since none can legitimately
                        // reach here from a point name or description.
                        break;
                    }

                    if( static_cast<unsigned char>( c) > 0x7F)
                    {
                        //
                        // \ansi declared in the preamble means bytes above
                        // ASCII must be given as \'hh. Emitted byte by byte,
                        // so UTF-8 input survives as its own bytes for a
                        // reader that interprets them that way, rather than
                        // being dropped.
                        //
                        std::array<char, 8> escaped{};
                        std::snprintf( escaped.data(), escaped.size(), "\\'%02x", static_cast<unsigned>( static_cast<unsigned char>( c)));
                        result += escaped.data();
                        break;
                    }

                    result += c;
                    break;
            }
        }

        return result;
    }

    RtfSink::RtfSink( const std::string & path) :
        //
        // Binary mode: the seek-back-over-the-trailer scheme in flushDocument()
        // needs a byte offset that means the same thing on write and on seek,
        // which text mode does not guarantee. RTF is ASCII either way, so
        // nothing is lost by writing the line breaks ourselves.
        //
        mOut( path, std::ios::out | std::ios::binary | std::ios::trunc)
    {
        if( !mOut)
        {
            throw std::runtime_error( "RtfSink: could not open '" + path + "' for writing");
        }

        mOut << "{\\rtf1\\ansi\\ansicpg1252\\deff0\n"
             << kFontTable << "\n"
             << kColourTable << "\n";

        flushDocument();
    }

    RtfSink::~RtfSink()
    {
        //
        // No throwing out of a destructor -- finalise() only writes and seeks,
        // and a failure to write the last byte of an already-valid document is
        // not worth terminating a process over.
        finalise();
    }

    auto RtfSink::flushDocument() -> void
    {
        if( mFinalised)
        {
            return;
        }

        mOut << '}';
        mOut.flush();

        //
        // Back over the brace just written, so the next line overwrites it.
        // If the seek fails the document is still valid on disk -- it just has
        // a stray trailer -- so this is deliberately not treated as fatal.
        //
        mOut.seekp( -1, std::ios_base::cur);
    }

    auto RtfSink::finalise() -> void
    {
        if( mFinalised)
        {
            return;
        }

        mOut << "}";
        mOut.flush();

        mFinalised = true;
    }

    auto RtfSink::write( const ReportLine & line) -> void
    {
        mOut << "{" << emphasisFormat( line.Style) << escape( line.Text) << "}\\par\n";
    }

    auto RtfSink::writeAll( const std::vector<ReportLine> & lines) -> void
    {
        // Nothing to say -- an event the human stream doesn't carry (see
        // core::isHumanRelevant). Returning early keeps the trailer rewrite
        // proportional to what is actually logged rather than to every verb.
        if( lines.empty())
        {
            return;
        }

        for( const auto & line : lines)
        {
            write( line);
        }

        //
        // Flushed per group of lines rather than per line: a mid-run reader
        // should never see half of a test's heading, and the write cost of a
        // one-byte rewrite is per flush.
        //
        flushDocument();
    }

    auto RtfSink::onRunStart( const RunInfo & info) -> void
    {
        writeAll( humanHeaderLines( info));
    }

    auto RtfSink::onTestStart( const std::string_view group, const std::string_view test, const std::string_view description) -> void
    {
        writeAll( humanTestHeadingLines( group, test, description));
    }

    auto RtfSink::onEvent( const JournalEvent & event) -> void
    {
        writeAll( humanEventLines( event));
    }

    auto RtfSink::onTestEnd( const std::string_view group, const std::string_view test, const bool passed) -> void
    {
        writeAll( humanTestResultLines( group, test, passed));
    }

    auto RtfSink::onRunEnd( const bool allPassed) -> void
    {
        for( const auto & line : humanSummaryLines( allPassed))
        {
            write( line);
        }

        finalise();
    }
} // namespace core
