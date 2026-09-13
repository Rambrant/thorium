#include "core/journal/json.hpp"

#include <array>
#include <cstdio>

namespace core
{
    auto jsonEscape( const std::string_view text) -> std::string
    {
        std::string result;
        result.reserve( text.size() + text.size() / 8);

        for( const char c : text)
        {
            switch( c)
            {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;

                default:
                    if( static_cast<unsigned char>( c) < 0x20)
                    {
                        //
                        // JSON requires every remaining control byte to be
                        // escaped as \u00XX -- unlike the RTF sink, which can
                        // drop them, a raw control byte here makes the document
                        // invalid rather than merely ugly.
                        //
                        std::array<char, 8> escaped{};
                        std::snprintf( escaped.data(), escaped.size(), "\\u%04x", static_cast<unsigned>( static_cast<unsigned char>( c)));
                        result += escaped.data();
                        break;
                    }

                    //
                    // Bytes above ASCII are passed through untouched: JSON is
                    // UTF-8 by default, so valid UTF-8 input stays valid, and
                    // re-encoding it would only risk breaking multi-byte
                    // sequences this has no reason to decode.
                    //
                    result += c;
                    break;
            }
        }

        return result;
    }

    auto jsonQuoted( const std::string_view text) -> std::string
    {
        return "\"" + jsonEscape( text) + "\"";
    }
} // namespace core
