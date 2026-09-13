#include "protocol/json.hpp"

#include <cstdlib>

namespace ui
{
    namespace
    {
        const std::vector<std::unique_ptr<Json>> kNoItems{};
    }

    //
    // A recursive-descent parser over the whole text, with one cursor.
    //
    // A class rather than a pack of free functions so the cursor is not
    // threaded through every call by reference -- that is the whole reason, and
    // it is why the class has no other members.
    //
    class JsonParser
    {
        public:
            explicit JsonParser( const std::string_view text) :
                mText( text)
            {}

            auto parseValue() -> std::unique_ptr<Json>
            {
                skipSpace();

                if( mAt >= mText.size())
                {
                    return nullptr;
                }

                switch( mText[ mAt])
                {
                    case '{':  return parseObject();
                    case '[':  return parseArray();
                    case '"':  return parseString();
                    case 't':  return parseLiteral( "true",  true);
                    case 'f':  return parseLiteral( "false", false);
                    case 'n':  return parseNull();
                    default:   return parseNumber();
                }
            }

            auto atEndIgnoringSpace() -> bool
            {
                skipSpace();

                return mAt >= mText.size();
            }

        private:
            auto skipSpace() -> void
            {
                while( mAt < mText.size() &&
                       ( mText[ mAt] == ' ' || mText[ mAt] == '\t' || mText[ mAt] == '\n' || mText[ mAt] == '\r'))
                {
                    ++mAt;
                }
            }

            auto take( const char expected) -> bool
            {
                skipSpace();

                if( mAt < mText.size() && mText[ mAt] == expected)
                {
                    ++mAt;

                    return true;
                }

                return false;
            }

            auto parseNull() -> std::unique_ptr<Json>
            {
                if( mText.compare( mAt, 4, "null") != 0)
                {
                    return nullptr;
                }

                mAt += 4;

                return std::make_unique<Json>();
            }

            auto parseLiteral( const std::string_view spelling, const bool value) -> std::unique_ptr<Json>
            {
                if( mText.compare( mAt, spelling.size(), spelling) != 0)
                {
                    return nullptr;
                }

                mAt += spelling.size();

                auto node   = std::make_unique<Json>();
                node->mType = Json::Type::Bool;
                node->mBool = value;

                return node;
            }

            auto parseNumber() -> std::unique_ptr<Json>
            {
                const auto start = mAt;

                if( mAt < mText.size() && ( mText[ mAt] == '-' || mText[ mAt] == '+'))
                {
                    ++mAt;
                }

                bool any = false;

                while( mAt < mText.size() &&
                       ( ( mText[ mAt] >= '0' && mText[ mAt] <= '9') ||
                         mText[ mAt] == '.' || mText[ mAt] == 'e' || mText[ mAt] == 'E' ||
                         mText[ mAt] == '-' || mText[ mAt] == '+'))
                {
                    any = any || ( mText[ mAt] >= '0' && mText[ mAt] <= '9');

                    ++mAt;
                }

                if( !any)
                {
                    return nullptr;
                }

                //
                // strtod over the substring, which needs a NUL-terminated copy.
                // The copy is of a number, so it is a handful of bytes; doing
                // the conversion by hand would be a second implementation of
                // something the C library has had right for decades, including
                // the exponent forms core::EventSink's std::to_string can emit.
                //
                const std::string  digits( mText.substr( start, mAt - start));

                auto node     = std::make_unique<Json>();
                node->mType   = Json::Type::Number;
                node->mNumber = std::strtod( digits.c_str(), nullptr);

                return node;
            }

            auto parseString() -> std::unique_ptr<Json>
            {
                if( !take( '"'))
                {
                    return nullptr;
                }

                std::string value;

                while( mAt < mText.size() && mText[ mAt] != '"')
                {
                    if( mText[ mAt] != '\\')
                    {
                        value += mText[ mAt++];

                        continue;
                    }

                    if( ++mAt >= mText.size())
                    {
                        return nullptr;   // a trailing backslash: truncated
                    }

                    switch( mText[ mAt++])
                    {
                        case '"':  value += '"';  break;
                        case '\\': value += '\\'; break;
                        case '/':  value += '/';  break;
                        case 'b':  value += '\b'; break;
                        case 'f':  value += '\f'; break;
                        case 'n':  value += '\n'; break;
                        case 'r':  value += '\r'; break;
                        case 't':  value += '\t'; break;

                        case 'u':
                        {
                            //
                            // Only the range core::jsonEscape actually emits:
                            // \u00XX for a control byte it could not spell any
                            // other way. Anything else in this producer's
                            // output is passed-through UTF-8, never escaped --
                            // so a surrogate pair arriving here would mean the
                            // document came from somewhere else, and guessing
                            // at it would be worse than refusing.
                            //
                            if( mAt + 4 > mText.size())
                            {
                                return nullptr;
                            }

                            const std::string  hex( mText.substr( mAt, 4));
                            char *             end  = nullptr;
                            const auto         code = std::strtoul( hex.c_str(), &end, 16);

                            if( end != hex.c_str() + 4 || code > 0xFF)
                            {
                                return nullptr;
                            }

                            value += static_cast<char>( code);
                            mAt   += 4;

                            break;
                        }

                        default:
                            return nullptr;
                    }
                }

                if( mAt >= mText.size())
                {
                    return nullptr;   // unterminated
                }

                ++mAt;   // the closing quote

                auto node   = std::make_unique<Json>();
                node->mType = Json::Type::String;
                node->mText = std::move( value);

                return node;
            }

            auto parseArray() -> std::unique_ptr<Json>
            {
                if( !take( '['))
                {
                    return nullptr;
                }

                auto node   = std::make_unique<Json>();
                node->mType = Json::Type::Array;

                if( take( ']'))
                {
                    return node;
                }

                for( ;;)
                {
                    auto item = parseValue();

                    if( !item)
                    {
                        return nullptr;
                    }

                    node->mItems.push_back( std::move( item));

                    if( take( ','))
                    {
                        continue;
                    }

                    return take( ']') ? std::move( node) : nullptr;
                }
            }

            auto parseObject() -> std::unique_ptr<Json>
            {
                if( !take( '{'))
                {
                    return nullptr;
                }

                auto node   = std::make_unique<Json>();
                node->mType = Json::Type::Object;

                if( take( '}'))
                {
                    return node;
                }

                for( ;;)
                {
                    skipSpace();

                    auto key = parseString();

                    if( !key || !take( ':'))
                    {
                        return nullptr;
                    }

                    auto value = parseValue();

                    if( !value)
                    {
                        return nullptr;
                    }

                    //
                    // The key index is built alongside the items rather than
                    // instead of them, so an object keeps its declaration order
                    // for anything that iterates it (the option model is a list
                    // whose order is the order the flags are declared in) while
                    // still answering at() without a scan.
                    //
                    node->mKeys.emplace( key->mText, node->mItems.size());
                    node->mItems.push_back( std::move( value));

                    if( take( ','))
                    {
                        continue;
                    }

                    return take( '}') ? std::move( node) : nullptr;
                }
            }

            std::string_view  mText;
            std::size_t       mAt{ 0 };
    };

    auto Json::parse( const std::string_view text) -> std::unique_ptr<Json>
    {
        JsonParser parser( text);

        auto value = parser.parseValue();

        //
        // Trailing content is a failure, not something to ignore. Two JSON
        // objects on one line of the event stream would mean the stream had
        // lost a newline, and a reader that silently took the first would show
        // half a run.
        //
        if( !value || !parser.atEndIgnoringSpace())
        {
            return nullptr;
        }

        return value;
    }

    auto Json::boolean( const bool fallback) const -> bool
    {
        return mType == Type::Bool ? mBool : fallback;
    }

    auto Json::number( const double fallback) const -> double
    {
        return mType == Type::Number ? mNumber : fallback;
    }

    auto Json::text( const std::string_view fallback) const -> std::string
    {
        return mType == Type::String ? mText : std::string( fallback);
    }

    auto Json::at( const std::string_view key) const -> const Json *
    {
        if( mType != Type::Object)
        {
            return nullptr;
        }

        const auto found = mKeys.find( key);

        return found == mKeys.end() ? nullptr : mItems[ found->second].get();
    }

    auto Json::textAt( const std::string_view key, const std::string_view fallback) const -> std::string
    {
        const auto * field = at( key);

        return field ? field->text( fallback) : std::string( fallback);
    }

    auto Json::boolAt( const std::string_view key, const bool fallback) const -> bool
    {
        const auto * field = at( key);

        return field ? field->boolean( fallback) : fallback;
    }

    auto Json::items() const -> const std::vector<std::unique_ptr<Json>> &
    {
        return mType == Type::Array ? mItems : kNoItems;
    }
} // namespace ui
