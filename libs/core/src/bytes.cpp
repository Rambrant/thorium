#include "core/bytes.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace core
{
    namespace
    {
        //
        // Through unsigned char, never through char directly -- see the
        // string_view constructor's own comment in core/bytes.hpp.
        //
        [[nodiscard]]
        auto toByte( const char c) -> std::byte
        {
            return static_cast<std::byte>( static_cast<unsigned char>( c));
        }

        [[nodiscard]]
        auto hexDigit( const char c) -> int
        {
            if( c >= '0' && c <= '9') { return c - '0';      }
            if( c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            if( c >= 'A' && c <= 'F') { return c - 'A' + 10; }

            return -1;
        }

        //
        // What describeValue's text branch will render without an escape, plus
        // the three control codes a console protocol actually uses. Space is
        // printable and included; DEL (0x7F) is not.
        //
        [[nodiscard]]
        auto isRenderableAsText( const std::byte value) -> bool
        {
            const auto octet = static_cast<unsigned char>( value);

            return ( octet >= 0x20 && octet < 0x7F) || octet == '\t' || octet == '\r' || octet == '\n';
        }

        //
        // One octet, as describeValue's text branch spells it. Separate from
        // that function purely so the loop there reads as the choice it is
        // making rather than as two rendering rules inlined into one body.
        //
        auto appendAsText( std::string & out, const std::byte octet) -> void
        {
            switch( static_cast<unsigned char>( octet))
            {
                case '\t': out += "\\t";  break;
                case '\r': out += "\\r";  break;
                case '\n': out += "\\n";  break;
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                default:   out += static_cast<char>( octet); break;
            }
        }

        //
        // One octet as two uppercase digits, space-separated from whatever is
        // already there -- Bytes::hex()'s rule, applied one octet at a time so
        // describeValue can stop early rather than building the hex of a
        // four-megabyte payload and then discarding all but its head.
        //
        auto appendAsHex( std::string & out, const std::byte octet) -> void
        {
            std::array<char, 4> buffer{};

            std::snprintf( buffer.data(), buffer.size(), "%02X", static_cast<unsigned>( static_cast<unsigned char>( octet)));

            if( !out.empty())
            {
                out += ' ';
            }

            out += buffer.data();
        }
    } // namespace

    Bytes::Bytes( const std::string_view text)
    {
        mData.reserve( text.size());

        for( const auto c : text)
        {
            mData.push_back( toByte( c));
        }
    }

    //
    // Delegating rather than duplicating: a const char* is a string_view's
    // argument, and the only reason this overload exists at all is that a
    // string literal would otherwise need one user-defined conversion too many
    // to reach Bytes through string_view at a Write() call site.
    //
    Bytes::Bytes( const char * text) : Bytes( std::string_view( text)) {}

    auto Bytes::fromHex( const std::string_view hex) -> Bytes
    {
        std::vector<std::byte> data;

        int  high    = -1;
        bool pending = false;

        for( const auto c : hex)
        {
            if( c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                continue;
            }

            const auto digit = hexDigit( c);

            if( digit < 0)
            {
                throw std::invalid_argument( std::string( "Bytes::fromHex: '") + c + "' is not a hex digit");
            }

            if( !pending)
            {
                high    = digit;
                pending = true;
            }
            else
            {
                data.push_back( static_cast<std::byte>( ( high << 4) | digit));
                pending = false;
            }
        }

        if( pending)
        {
            throw std::invalid_argument( "Bytes::fromHex: odd number of hex digits -- every byte needs two");
        }

        return Bytes{ std::move( data) };
    }

    auto Bytes::at( const std::size_t index) const -> std::byte
    {
        if( index >= mData.size())
        {
            throw std::out_of_range(
                "Bytes::at: index " + std::to_string( index) + " is past the end of a " +
                std::to_string( mData.size()) + "-byte payload");
        }

        return mData[ index];
    }

    auto Bytes::text() const -> std::string
    {
        std::string result;

        result.reserve( mData.size());

        for( const auto value : mData)
        {
            result.push_back( static_cast<char>( static_cast<unsigned char>( value)));
        }

        return result;
    }

    auto Bytes::hex() const -> std::string
    {
        std::string result;

        for( const auto value : mData)
        {
            std::array<char, 4> buffer{};

            std::snprintf( buffer.data(), buffer.size(), "%02X", static_cast<unsigned>( static_cast<unsigned char>( value)));

            if( !result.empty())
            {
                result += ' ';
            }

            result += buffer.data();
        }

        return result;
    }

    auto Bytes::slice( const std::size_t offset, const std::size_t count) const -> Bytes
    {
        //
        // offset > size() checked separately from the sum, so a caller passing
        // a huge count cannot wrap the addition round and appear in range.
        //
        if( offset > mData.size() || count > mData.size() - offset)
        {
            throw std::out_of_range(
                "Bytes::slice: [" + std::to_string( offset) + ", " + std::to_string( offset + count) +
                ") is past the end of a " + std::to_string( mData.size()) + "-byte payload");
        }

        return Bytes{ std::vector<std::byte>( mData.begin() + static_cast<std::ptrdiff_t>( offset),
                                              mData.begin() + static_cast<std::ptrdiff_t>( offset + count)) };
    }

    auto Bytes::before( const Bytes & delimiter) const -> Bytes
    {
        //
        // An empty delimiter never occurs, rather than occurring everywhere at
        // position zero: before( Bytes{}) returning an empty payload would
        // silently discard a whole reply, and a driver whose terminator was
        // never configured is exactly how that empty delimiter arrives here.
        //
        if( delimiter.empty())
        {
            return *this;
        }

        const auto found = std::search( mData.begin(), mData.end(), delimiter.begin(), delimiter.end());

        return Bytes{ std::vector<std::byte>( mData.begin(), found) };
    }

    auto Bytes::startsWith( const Bytes & prefix) const -> bool
    {
        return prefix.size() <= mData.size() && std::equal( prefix.begin(), prefix.end(), mData.begin());
    }

    auto Bytes::endsWith( const Bytes & suffix) const -> bool
    {
        return suffix.size() <= mData.size() &&
               std::equal( suffix.begin(), suffix.end(), mData.end() - static_cast<std::ptrdiff_t>( suffix.size()));
    }

    auto describeValue( const Bytes & value) -> std::string
    {
        //
        // The encoding is chosen from the whole payload even though only its
        // head is rendered. Deciding from the head alone would be cheaper and
        // would lie about exactly the payload worth being careful with -- a
        // firmware image whose first forty bytes happen to be an ASCII banner
        // would be announced as text, in quotes, and a reader would take the
        // abridged head for the start of a string.
        //
        const auto asText = std::ranges::all_of( value, isRenderableAsText);

        std::string body;
        bool        abridged = false;

        for( const auto octet : value)
        {
            //
            // Tested before the octet is appended rather than after, so the
            // body never exceeds the bound by the width of whatever the last
            // rendering happened to be -- an escape is two characters and a hex
            // pair with its separator is three.
            //
            if( body.size() >= kMaxDescribedBody)
            {
                abridged = true;
                break;
            }

            if( asText)
            {
                appendAsText( body, octet);
            }
            else
            {
                appendAsHex( body, octet);
            }
        }

        const auto open  = asText ? "\"" : "<";
        const auto close = asText ? "\"" : ">";

        if( !abridged)
        {
            return open + body + close;
        }

        //
        // The ellipsis goes inside the delimiters and the true length outside
        // them: what was cut is part of the payload, and how much there was of
        // it is a fact about the payload rather than a character in it.
        //
        return open + body + ( asText ? "..." : " ...") + close +
               " (" + std::to_string( value.size()) + " bytes)";
    }
} // namespace core
