#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "core/quantity_kind.hpp"

namespace core
{
    //
    // One measured value, as it happened during a run. mSequence is a
    // monotonic per-run counter (what a replay dequeues by, since order is
    // what determines correctness during playback); mWallClock is wall-clock
    // time purely for a human reading the file later -- nothing in Thorium
    // ever dequeues by it.
    //
    struct RecordedSample
    {
        std::uint64_t     mSequence;
        std::int64_t      mWallClockUnixMillis;
        std::string       mPointName;
        std::string       mInstrumentId;
        QuantityKind      mKind;
        QuantityVariant   mValue;
    };

    //
    // Flat TSV, one row per sample, in the order they were recorded:
    //   sequence  wallClockUnixMillis  pointName  instrumentId  kind  value
    //
    // TSV rather than JSON/CSV-with-quoting: every field here is a single
    // token (no embedded commas or whitespace to escape), so a plain
    // tab-delimited line is both trivially diffable between two bench runs
    // and trivially parsed back without a real CSV parser.
    //
    auto writeRecording( std::ostream & out, const std::vector<RecordedSample> & samples) -> void;

    [[nodiscard]]
    auto readRecording( std::istream & in) -> std::vector<RecordedSample>;
} // namespace core
