#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace console
{
    //
    // A small JSON reader, for the three documents this program is handed:
    // manifest.json, --describe-options, and one line of --events.
    //
    // Written here rather than depended on, and that is worth defending because
    // "write your own JSON parser" is usually the wrong answer. Three things
    // make it the right one here:
    //
    //   - the inputs are not arbitrary. All three are produced a few hundred
    //     lines away, by code in this repository, with an escaper
    //     (core::jsonEscape) that has its own test. This is not parsing the
    //     internet.
    //   - a dependency here costs more than it does anywhere else. This program
    //     exists to be built on three platforms by whatever compiler each one
    //     has, with as little to install as possible; adding a fetch step for
    //     something this size trades a real cost against an imagined one.
    //   - it is on the hot path of a live view. An event line is parsed per
    //     reading, while an operator watches.
    //
    // What it therefore does NOT do, deliberately: \u escapes beyond what
    // core::jsonEscape emits (\u00XX control bytes, which it decodes; surrogate
    // pairs, which that escaper cannot produce because it passes UTF-8 through
    // untouched), numbers outside double, and duplicate keys. A malformed
    // document is a parse failure with a position, never a guess.
    //
    class Json
    {
        public:
            enum class Type { Null, Bool, Number, String, Array, Object };

            //
            // Parsing answers a value or nothing. Nothing is not an exception,
            // because the most likely producer of a malformed document is a
            // child process that died mid-line, and a half-written event is an
            // ordinary thing for this program to see rather than an error it
            // should unwind for. See console::EventStream, which discards exactly
            // that and keeps reading.
            //
            [[nodiscard]]
            static auto parse( std::string_view text) -> std::unique_ptr<Json>;

            [[nodiscard]] auto type() const -> Type { return mType; }

            //
            // Accessors that answer a default rather than throwing or
            // asserting. Every caller in this program is reading a field that
            // may legitimately be absent -- the event schema omits empty
            // strings and unset optionals rather than writing null -- so
            // "absent" is the common case and must not be the exceptional path.
            //
            [[nodiscard]] auto boolean( bool fallback = false) const -> bool;
            [[nodiscard]] auto number( double fallback = 0.0) const -> double;
            [[nodiscard]] auto text( std::string_view fallback = {}) const -> std::string;

            // nullptr when this is not an object, or has no such key.
            [[nodiscard]] auto at( std::string_view key) const -> const Json *;

            // A field's value, or the fallback -- the shorthand every consumer
            // in this program actually wants.
            [[nodiscard]] auto textAt( std::string_view key, std::string_view fallback = {}) const -> std::string;
            [[nodiscard]] auto boolAt( std::string_view key, bool fallback = false) const -> bool;

            //
            // Empty for anything that is not an array, so a caller can iterate
            // an absent field without checking first -- "no instruments" and
            // "not an array" both mean nothing to draw.
            //
            [[nodiscard]] auto items() const -> const std::vector<std::unique_ptr<Json>> &;

            [[nodiscard]] auto has( std::string_view key) const -> bool { return at( key) != nullptr; }

        private:
            friend class JsonParser;

            Type                                     mType{ Type::Null };
            bool                                     mBool{ false };
            double                                   mNumber{ 0.0 };
            std::string                              mText;
            std::vector<std::unique_ptr<Json>>       mItems;
            std::map<std::string, std::size_t, std::less<>>  mKeys;   // key -> index into mItems
    };
} // namespace console
