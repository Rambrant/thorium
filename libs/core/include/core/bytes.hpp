#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace core
{
    //
    // What a byte-oriented instrument sends and receives: an ordered,
    // possibly-empty sequence of octets, and nothing else.
    //
    // Deliberately NOT a core::quantities::Quantity, and deliberately not
    // squeezed into the QuantityVariant the measurement seam works in. A
    // reading is one number in one unit, and every part of that machinery --
    // epsilon tolerances, unit symbols, the kind-to-symbol table, the
    // numeric column both logs carry -- assumes it. A serial reply is not a
    // number that happens to be spelled in bytes: it has a length, its
    // elements have positions, two of them compare equal or they do not, and
    // "5.02 of them" means nothing. Giving it its own type is what keeps
    // Measure()'s guarantees honest -- see core::ISession::fetchData in
    // core/session.hpp for the parallel seam this travels through, which
    // exists for exactly the same reason.
    //
    // Deliberately NOT a std::string either, which is what the legacy test
    // language this replaces used. A std::string of protocol bytes invites
    // every text operation that is wrong for one -- a locale-aware compare, a
    // strlen that stops at the first NUL in a binary frame, an implicit
    // conversion into a log line that then renders 0x1B as an escape
    // sequence. std::byte elements make each of those a compile error rather
    // than a wrong answer, which is this codebase's whole argument.
    //
    // It is still cheap to *build* one from text, because most console
    // protocols are text: the string_view and char-pointer constructors are
    // implicit on purpose, so Write( Ser1.rs232(), "RD 30\r") reads the way
    // the protocol document reads. Going the other way is explicit
    // (text() below) -- widening a byte sequence into characters is a claim
    // about encoding, and a claim belongs at a call site.
    //
    class Bytes
    {
        public:
            Bytes() = default;

            explicit Bytes( std::vector<std::byte> data) : mData( std::move( data)) {}

            Bytes( std::initializer_list<std::byte> data) : mData( data) {}

            //
            // Implicit, and only for text: see this class's own comment. Each
            // char becomes one byte with its bit pattern unchanged -- via
            // unsigned char, so a char that happens to be signed and negative
            // on this platform yields the octet that was actually on the wire
            // rather than an implementation-defined conversion.
            //
            Bytes( std::string_view text);
            Bytes( const char * text);

            //
            // "1B 5B 41" / "1b5b41" -- whitespace between pairs optional, so a
            // protocol document's own spacing can be pasted in as it stands.
            // Throws std::invalid_argument on a non-hex digit or an odd digit
            // count, rather than silently dropping half a byte.
            //
            [[nodiscard]]
            static auto fromHex( std::string_view hex) -> Bytes;

            [[nodiscard]] auto size()  const -> std::size_t { return mData.size();  }
            [[nodiscard]] auto empty() const -> bool        { return mData.empty(); }

            [[nodiscard]] auto begin() const { return mData.begin(); }
            [[nodiscard]] auto end()   const { return mData.end();   }

            [[nodiscard]] auto data() const -> std::span<const std::byte> { return mData; }

            //
            // Bounds-checked, and the only element accessor -- there is no
            // unchecked operator[]. A reply that came back shorter than the
            // protocol says is exactly the failure this type exists around, and
            // reading past its end must be a std::out_of_range a script author
            // sees, not whatever was in the allocation.
            //
            [[nodiscard]] auto at( std::size_t index) const -> std::byte;

            //
            // The bytes as characters, for a text protocol -- explicit, see
            // this class's own comment.
            //
            [[nodiscard]] auto text() const -> std::string;

            // "1B 5B 41", space-separated, uppercase. Always available.
            [[nodiscard]] auto hex() const -> std::string;

            //
            // A contiguous subsequence -- the field-at-an-offset access a
            // fixed-layout frame needs. Throws std::out_of_range if the
            // requested window runs past the end, for the same reason at()
            // does.
            //
            [[nodiscard]] auto slice( std::size_t offset, std::size_t count) const -> Bytes;

            //
            // Everything up to (not including) the first occurrence of
            // delimiter, or the whole sequence when it does not occur. What
            // strips a terminator off a reply before it is compared -- see
            // hal::Racal1260's terminator(), which is what a read stops at but
            // deliberately still returns.
            //
            [[nodiscard]] auto before( const Bytes & delimiter) const -> Bytes;

            [[nodiscard]] auto startsWith( const Bytes & prefix) const -> bool;
            [[nodiscard]] auto endsWith(   const Bytes & suffix) const -> bool;

            [[nodiscard]] auto operator==( const Bytes &) const -> bool = default;
            [[nodiscard]] auto operator<=>( const Bytes &) const = default;

        private:
            std::vector<std::byte> mData;
    };

    //
    // How a payload is written down in both logs -- one rule, chosen per
    // payload rather than per byte:
    //
    //   all bytes printable ASCII or \t \r \n  ->  "RD 30\r"   (escaped text)
    //   anything else                          ->  <1B 5B 41>  (hex)
    //
    // The alternative -- always escaping, so a binary frame renders as
    // "\x1B\x5B\x41" -- was rejected on purpose: it is longer than the hex for
    // every byte it describes, it makes a frame's byte count impossible to see
    // at a glance, and it invites a reader to mistake a protocol's literal
    // backslash for an escape. Choosing per payload means a text console reads
    // as text and a binary frame reads as a frame, and the delimiters (quotes
    // versus angle brackets) say without ambiguity which of the two a reader is
    // looking at.
    //
    // A non-template overload rather than another branch in core/format.hpp's
    // describeValue chain, so core/format.hpp keeps knowing nothing about this
    // header. It is found by ADL at every call site that has a Bytes to
    // describe, which by definition has included this file.
    //
    //
    // The same payload, as something a criteria table can hold.
    //
    // core::Bytes cannot be one, and the reason is worth stating because it is
    // not a limitation anybody chose: a CRIT entry expands to a `static
    // constexpr` member (see core/criterion.hpp), so an expected value has to
    // be a literal type whose storage survives to run time. Bytes owns a
    // std::vector -- constexpr-constructible in C++26, but not as a
    // static constexpr object, because the allocation cannot persist past
    // constant evaluation. So `CRIT( ..., EQ( Bytes( "ACK")), ...)` is not a
    // near miss to be worked around; it cannot compile.
    //
    // A fixed-extent std::array can. The length comes from the literal at the
    // call site, which is exactly where it is known, and nothing about a
    // *criterion* wanted a growable payload anyway -- an expected reply is a
    // constant of the test specification.
    //
    // Comparison is heterogeneous, against the Bytes a Read actually returns.
    // Length is compared first, so a reply that merely starts with the expected
    // bytes is not equal to it -- "the DUT answered OK" and "the DUT answered
    // OK and then kept talking" are different observations.
    //
    template<std::size_t N>
    struct BytePattern
    {
        std::array<std::byte, N> Data{};

        [[nodiscard]]
        constexpr auto size() const -> std::size_t
        {
            return N;
        }

        [[nodiscard]]
        auto operator==( const Bytes & actual) const -> bool
        {
            return actual.size() == N && std::equal( Data.begin(), Data.end(), actual.begin());
        }

        [[nodiscard]]
        constexpr auto operator==( const BytePattern & other) const -> bool = default;
    };

    [[nodiscard]]
    auto describeValue( const Bytes & value) -> std::string;

    //
    // Rendered exactly as the payload it stands for, so a criterion's stated
    // limit and the reading it was checked against are written the same way in
    // the report -- "= \"ACK\"" above "\"ACK\"", not one of them in some
    // pattern-specific notation the reader has to translate.
    //
    template<std::size_t N>
    [[nodiscard]]
    auto describeValue( const BytePattern<N> & value) -> std::string
    {
        return describeValue( Bytes( std::vector<std::byte>( value.Data.begin(), value.Data.end())));
    }

    namespace quantities
    {
        //
        // Reopened here so a criteria table reaches this with the same bare
        // spelling it reaches EQ, MASK and _V by (see core/active_criteria.hpp,
        // which does `using namespace core::quantities`):
        //
        //     CRIT( FS_Console_Ack, EQ( bytes( "ACK")), "...")
        //
        // Same reason core/predicates.hpp reopens it rather than inventing a
        // namespace of its own.
        //
        using core::BytePattern;

        //
        // N - 1, dropping the literal's terminating NUL. A protocol whose reply
        // genuinely ends in a NUL says so with hexBytes below, where the byte is
        // written out and visible, rather than by relying on a subtlety of how
        // string literals are stored.
        //
        template<std::size_t N>
        [[nodiscard]]
        constexpr auto bytes( const char ( &text)[ N]) -> BytePattern<N - 1>
        {
            BytePattern<N - 1> pattern{};

            for( std::size_t i = 0; i + 1 < N; ++i)
            {
                pattern.Data[ i] = static_cast<std::byte>( static_cast<unsigned char>( text[ i]));
            }

            return pattern;
        }
    } // namespace quantities
} // namespace core
