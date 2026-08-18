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
        if( !std::ranges::all_of( value, isRenderableAsText))
        {
            return "<" + value.hex() + ">";
        }

        std::string result = "\"";

        for( const auto octet : value)
        {
            switch( static_cast<unsigned char>( octet))
            {
                case '\t': result += "\\t";  break;
                case '\r': result += "\\r";  break;
                case '\n': result += "\\n";  break;
                case '\\': result += "\\\\"; break;
                case '"':  result += "\\\""; break;
                default:   result += static_cast<char>( octet); break;
            }
        }

        return result + "\"";
    }
} // namespace core
