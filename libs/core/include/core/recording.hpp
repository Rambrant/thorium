#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "core/bytes.hpp"
#include "core/quantity_kind.hpp"

namespace core
{
    //
    // What one recorded observation actually holds: a measured quantity, or a
    // byte payload read off a byte-oriented instrument (see
    // core::ISession::fetchData in core/session.hpp).
    //
    // A variant rather than a quantity plus an "is it really bytes" flag beside
    // it, on purpose. A struct with both fields present and only one of them
    // meaningful is a shape this file already learned the cost of once: the
    // kind and the value used to be stored separately here, and a sample whose
    // kind said Voltage while its value held a Current was expressible and
    // silently wrong -- see core::MeasureEngine's own comment on the log line
    // that used to be assembled from exactly that mismatch. A variant cannot be
    // in that state at all, and the kind is derivable from it (its index)
    // rather than being a second fact to keep in step.
    //
    // The third alternative is a plain bool: whether a triggered acquisition
    // completed within its timeout (see core::AwaitEngine in core/acquire.hpp).
    // It earns a place here for the same reason the payload did -- it is an
    // observation a run makes, and a replay that could not reproduce it would
    // take the measurements that follow from the file while asking absent
    // hardware whether there was anything to measure.
    //
    // A bool rather than folding it into QuantityVariant as a dimensionless
    // number, and the argument is core::ISession::fetchData's, restated: a
    // QuantityVariant is a closed set of numbers *in units*, indexed by
    // QuantityKind and unwrapped for criteria that carry an epsilon. "The
    // capture completed" has no unit, no tolerance, and nothing to interpolate
    // -- 0.5 of it is not a thing.
    //
    using RecordedValue = std::variant<QuantityVariant, Bytes, bool>;

    //
    // One observation, as it happened during a run. mSequence is a monotonic
    // per-run counter (what a replay dequeues by, since order is what
    // determines correctness during playback); mWallClock is wall-clock time
    // purely for a human reading the file later -- nothing in Thorium ever
    // dequeues by it.
    //
    struct RecordedSample
    {
        std::uint64_t   mSequence;
        std::int64_t    mWallClockUnixMillis;
        std::string     mPointName;
        std::string     mInstrumentId;
        RecordedValue   mValue;
    };

    //
    // The kind column's value for a byte payload, where a quantity row carries
    // its QuantityKind's own name.
    //
    // Spelled with angle brackets so it cannot ever collide with one. A
    // QuantityKind enumerator is a C++ identifier, '<' is not a character an
    // identifier may contain, and so no unit added to core/quantity_kind.hpp in
    // future can take this token -- a structural guarantee, rather than a
    // static_assert somebody has to remember to keep pointed at the right list.
    //
    inline constexpr std::string_view kPayloadKind = "<bytes>";

    //
    // The kind column's value for a completion flag, spelled with the same
    // angle brackets and guaranteed distinct from a QuantityKind's name for
    // the same structural reason -- see kPayloadKind directly above.
    //
    inline constexpr std::string_view kFlagKind = "<flag>";

    //
    // Flat TSV, one row per sample, in the order they were recorded:
    //   sequence  wallClockUnixMillis  pointName  instrumentId  kind  value
    //
    // TSV rather than JSON/CSV-with-quoting: every field here is a single
    // token (no embedded commas or whitespace to escape), so a plain
    // tab-delimited line is both trivially diffable between two bench runs
    // and trivially parsed back without a real CSV parser.
    //
    // A payload row keeps that property by writing its value as unspaced
    // uppercase hex ("4F4B0D") rather than as the text it may well be. Bytes
    // renders itself as readable text elsewhere (see core::describeValue), and
    // deliberately not here: a reply is free to contain a tab, a newline or a
    // NUL, each of which would end the row early or split it, and a recording
    // that cannot round-trip an arbitrary payload is no use for replaying the
    // runs where the payload was the problem.
    //
    auto writeRecording( std::ostream & out, const std::vector<RecordedSample> & samples) -> void;

    [[nodiscard]]
    auto readRecording( std::istream & in) -> std::vector<RecordedSample>;
} // namespace core
