#include "core/journal/rtf_sink.hpp"

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
        // One font, monospaced, and it is the document default (\deff0 in the
        // preamble below) as well as the one every span selects -- the human log
        // is a column layout (see core/src/journal/report.cpp's width constants), and a
        // proportional font throws every column out of alignment, which defeats
        // the reason the padding is there.
        //
        // Courier New rather than something better-looking, because it is the
        // one monospaced font present out of the box on every platform a run log
        // gets opened on: Windows and macOS both ship it, and LibreOffice
        // substitutes the metric-compatible Liberation Mono for it on Linux.
        // This used to name Menlo, which exists only on macOS -- on Windows the
        // reader found no such font and fell back to its default *proportional*
        // face, so every column in the file was ragged. A log that is only
        // legible on the machine it was written on is half a log.
        //
        // \fprq1 (fixed pitch) alongside \fmodern is the belt to that braces:
        // both tell a reader that has to substitute anyway to reach for a
        // monospaced face rather than a proportional one. \fcharset0 is ANSI,
        // matching the \ansicpg1252 the preamble declares.
        //
        // Deliberately the only entry. The table used to carry an unused
        // proportional \f0 alongside it, which is how the default font
        // (\deff0) came to be one the document never actually uses -- a trap
        // for any span later emitted without an explicit font selector.
        //
        constexpr std::string_view kFontTable =
            "{\\fonttbl"
            "{\\f0\\fmodern\\fprq1\\fcharset0 Courier New;}"
            "}";

        //
        // One UTF-16 code unit as RTF's \uN escape. N is a *signed* 16-bit
        // value, so anything at or above 0x8000 is written as its negative
        // equivalent -- a positive 0xB1-and-up would be out of range and is
        // what makes some readers reject the document outright.
        //
        // The trailing '?' is the mandatory fallback character, for a reader
        // too old to understand \u at all. It is what shows up instead of the
        // real glyph in that case, which is the correct outcome: a visible
        // placeholder, not a silently dropped character.
        //
        auto appendUnicodeUnit( std::string & out, const unsigned long unit) -> void
        {
            const long value = ( unit >= 0x8000) ? static_cast<long>( unit) - 0x10000 : static_cast<long>( unit);

            out += "\\u" + std::to_string( value) + "?";
        }

        //
        // Decodes one UTF-8 sequence starting at index i (advancing it past
        // what was consumed) and appends the RTF escape for it.
        //
        // This exists because the obvious thing -- escaping each byte above
        // ASCII as \'hh, which is what this file did first -- is wrong, not
        // merely imperfect. The preamble declares \ansi with codepage 1252, so
        // a reader interprets those bytes one at a time in cp1252: the two
        // bytes of a UTF-8 "å" (C3 A5) come out as "Ã¥". Anything non-ASCII
        // reaching a log -- an operator name from the environment, a unit
        // symbol, a description written by somebody with a keyboard that has
        // more than 26 letters on it -- would be quietly mangled in the file
        // while looking correct on the console beside it.
        //
        // An invalid or truncated sequence falls back to \'hh for that single
        // byte. That is deliberately not an error: this is a log, and one
        // undecodable byte in a description must not cost the whole document.
        //
        auto appendUtf8Sequence( std::string & out, const std::string_view text, std::size_t & i) -> void
        {
            const auto lead = static_cast<unsigned char>( text[ i]);

            auto asSingleByte = [&]()
            {
                std::array<char, 8> escaped{};
                std::snprintf( escaped.data(), escaped.size(), "\\'%02x", static_cast<unsigned>( lead));
                out += escaped.data();
            };

            // How many continuation bytes this lead byte promises, and the code
            // point bits it contributes itself.
            std::size_t   extra = 0;
            unsigned long code  = 0;

            if(      ( lead & 0xE0) == 0xC0) { extra = 1; code = lead & 0x1Fu; }
            else if( ( lead & 0xF0) == 0xE0) { extra = 2; code = lead & 0x0Fu; }
            else if( ( lead & 0xF8) == 0xF0) { extra = 3; code = lead & 0x07u; }
            else
            {
                // A stray continuation byte, or one of the invalid 0xF8-0xFF
                // leads -- not the start of anything decodable.
                asSingleByte();
                return;
            }

            if( i + extra >= text.size())
            {
                asSingleByte();
                return;
            }

            for( std::size_t n = 1; n <= extra; ++n)
            {
                const auto continuation = static_cast<unsigned char>( text[ i + n]);

                if( ( continuation & 0xC0) != 0x80)
                {
                    asSingleByte();
                    return;
                }

                code = ( code << 6) | ( continuation & 0x3Fu);
            }

            i += extra;

            if( code > 0xFFFF)
            {
                // Outside the BMP: RTF's \u is a UTF-16 code unit, so this needs
                // a surrogate pair rather than one escape.
                const unsigned long adjusted = code - 0x10000;

                appendUnicodeUnit( out, 0xD800 + ( adjusted >> 10));
                appendUnicodeUnit( out, 0xDC00 + ( adjusted & 0x3FF));
            }
            else
            {
                appendUnicodeUnit( out, code);
            }
        }

        //
        // Per-span character formatting: colour index plus bold/size. Returned
        // as a prefix rather than applied with a persistent state change, so
        // every span is self-contained -- a reader (or a repair tool) can
        // truncate this file anywhere and what remains still renders correctly,
        // and no span's formatting leaks into the one after it.
        //
        // One font (\f0, monospaced -- see kFontTable) and one body size
        // (\fs20) throughout, with hierarchy carried by weight and colour
        // instead. That is not a style preference: the log is a column grid
        // (see core/src/journal/report.cpp's width constants), several lines now mix two
        // emphases *within* a line (a measurement plus its grey description),
        // and a size or font change mid-line would break the alignment the
        // padding exists to create. Headings get a modest bump because they sit
        // on their own line, where nothing has to line up with them.
        //
        auto emphasisFormat( const Emphasis emphasis) -> std::string_view
        {
            switch( emphasis)
            {
                case Emphasis::Plain:   return "\\f0\\fs20\\cf1 ";
                case Emphasis::Heading: return "\\f0\\fs22\\b\\cf4 ";
                case Emphasis::Detail:  return "\\f0\\fs20\\cf5 ";
                case Emphasis::Pass:    return "\\f0\\fs20\\b\\cf2 ";
                case Emphasis::Fail:    return "\\f0\\fs20\\b\\cf3 ";
                case Emphasis::Warning: return "\\f0\\fs20\\b\\cf6 ";
            }

            return "\\f0\\fs20\\cf1 ";
        }
    } // namespace

    auto RtfSink::escape( const std::string_view text) -> std::string
    {
        std::string result;
        result.reserve( text.size() + text.size() / 8);

        // Indexed rather than range-based: a multi-byte UTF-8 sequence is
        // consumed as a unit, which needs the loop variable to be advanceable
        // from inside the body (see appendUtf8Sequence).
        for( std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[ i];

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
                        // Decoded as UTF-8 and re-emitted as \uN -- see
                        // appendUtf8Sequence on why byte-at-a-time \'hh is
                        // actively wrong here.
                        appendUtf8Sequence( result, text, i);
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
        // No throwing out of a destructor -- finishDocument() only writes and
        // seeks, and a failure to write the last byte of an already-valid
        // document is not worth terminating a process over.
        finishDocument();
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

    auto RtfSink::finishDocument() -> void
    {
        if( mFinalised)
        {
            return;
        }

        mOut << "}";
        mOut.flush();

        mFinalised = true;
    }

    //
    // One brace-delimited group per span, then the paragraph break. Grouping per
    // span rather than per line is what lets a line carry more than one
    // emphasis -- a measurement in body text with its description in grey (see
    // core::ReportSpan) -- and it keeps every span self-contained, so no span's
    // formatting can leak into the next.
    //
    // A line with no spans at all is the blank line separating test blocks: just
    // the paragraph break.
    //
    auto RtfSink::write( const ReportLine & line) -> void
    {
        for( const auto & span : line.Spans)
        {
            mOut << "{" << emphasisFormat( span.Style) << escape( span.Text) << "}";
        }

        mOut << "\\par\n";
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
} // namespace core
