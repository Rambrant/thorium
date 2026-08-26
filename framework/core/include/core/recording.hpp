#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/bytes.hpp"
#include "core/quantity_kind.hpp"
#include "core/waveform.hpp"

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
    // The fourth alternative is a captured trace (see core::FetchEngine in
    // core/trace.hpp) -- an observation like the other three, and one a replay
    // has to reproduce for the same reason.
    //
    using RecordedValue = std::variant<QuantityVariant, Bytes, bool, Waveform>;

    //
    // One observation, as it happened during a run. mSequence is a monotonic
    // per-run counter (what a replay dequeues by, since order is what
    // determines correctness during playback); mWallClock is wall-clock time
    // purely for a human reading the file later -- nothing in Thorium ever
    // dequeues by it.
    //
    // mTestId is which catalog test was running when the observation was made,
    // and it is what lets one test be replayed out of a whole run's recording
    // (see core::ScriptedSession::loadFromFile, which owns that rule -- this
    // file only reads and writes the column). Stamped from the journal, which
    // already knows -- see core::RecordingSession::record.
    //
    // A field on the sample rather than structure in the file (a boundary line
    // between one test's rows and the next) because a row that carries its own
    // attribution stays independently meaningful. Rows can be filtered, sorted
    // or grepped out of a recording and each one still says which test it
    // belongs to; with a boundary line, any of those operations silently
    // reattributes everything after the line it dropped.
    //
    // Empty is not a legal value: an observation made outside any test carries
    // kRunScope. See that constant for why the distinction is spelled rather
    // than left blank.
    //
    struct RecordedSample
    {
        std::uint64_t   mSequence;
        std::int64_t    mWallClockUnixMillis;
        std::string     mTestId;
        std::string     mPointName;
        std::string     mInstrumentId;
        RecordedValue   mValue;
    };

    //
    // The test column's value for an observation that belongs to the run rather
    // than to any one test -- what a hook reads, whether it is the run's
    // RUN_SETUP/RUN_TEARDOWN or one group's own SETUP/TEARDOWN, since none of
    // them runs inside a test bracket (see runHook in framework/runner/src/main.cpp).
    //
    // Spelled rather than left empty, for two reasons. The reader's one
    // concession to an empty field is the value column, and it is deliberate
    // and narrow: an empty payload is a real observation, and every other
    // column being non-empty is what makes a short row a malformed row rather
    // than a row with a hole in it. A blank test column would spend that
    // guarantee to say something a token says better.
    //
    // The second is that a replay has to treat these rows differently from
    // every other row -- they are never filtered out, because a hook still runs
    // when a single test is replayed and still needs its readings. A rule that
    // important should key off a value the format names, not off absence.
    //
    // Angle brackets for the same structural reason kPayloadKind has them: a
    // test id is a C++ identifier (see the TEST entries in
    // suite/test_catalog.inc), '<' is not a character an identifier may
    // contain, and so no test added later can take this token.
    //
    inline constexpr std::string_view kRunScope = "<run>";

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
    // The kind column's value for a captured trace, spelled with the same
    // angle brackets and distinct from a QuantityKind's name for the same
    // structural reason -- see kPayloadKind above.
    //
    // A trace row is the one row in this format that is not seven fields wide.
    // Where every other row is
    //
    //   sequence  millis  test  point  instrument  kind  value
    //
    // a trace row carries the three facts that make its samples mean something
    // between the kind and them:
    //
    //   sequence  millis  test  point  instrument  <trace>  unit  xOrigin  xIncrement  samples
    //
    // In the row rather than in the sample file beside it, deliberately. The
    // sidecar is the *payload* and nothing else -- no header, no encoding, so
    // that ordinary tools can open one -- and the timebase is exactly the part
    // of a trace a reader diffing two recordings wants to see without opening
    // anything. Two runs whose captures differ only in their sample rate differ
    // visibly here.
    //
    // The samples column then follows the payload rule above unchanged: unspaced
    // hex while it is small enough to read, a sidecar reference once it is not.
    // Same encoding, same threshold, same '@'.
    //
    inline constexpr std::string_view kTraceKind = "<trace>";

    //
    // How a trace's samples are encoded, in the row or in a sidecar: eight
    // octets per sample, IEEE-754 double, little-endian, in order.
    //
    // Written little-endian explicitly rather than by copying the host's bytes,
    // so a recording taken on the bench replays on whatever is at the desk. The
    // shuffling costs nothing against the transfer that produced the trace.
    //
    // Binary rather than the text every other value column uses, and this is
    // the one place that trade is worth making: a four-thousand-point trace is
    // 32 KB this way and upwards of 80 KB as text, and unlike every other value
    // in this file nobody is going to read it. Text would also have to decide
    // how many digits a sample keeps, and any answer to that is a recording
    // that does not replay to the same verdict it recorded.
    //
    inline constexpr std::size_t kSampleOctets = 8;

    //
    // Flat TSV, one row per sample, in the order they were recorded:
    //   sequence  wallClockUnixMillis  testId  pointName  instrumentId  kind  value
    //
    // Ordered so that the row reads as a sentence left to right: when it
    // happened, which test was running, which DUT point, through which
    // instrument, and then what came back. The value stays last, which is not
    // presentation -- the reader takes the value as "whatever remains of the
    // line", which is what lets a trace row carry three extra columns ahead of
    // its samples without the reader needing to count fields per kind.
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
    // ---------------------------------------------------------------------
    // Heavy payloads, and why they leave the file
    // ---------------------------------------------------------------------
    //
    // Hex inline is right for a console reply and wrong for anything large. A
    // four-kilobyte frame is a twelve-thousand-character row; a captured trace
    // would be tens of megabytes on one line. That does not merely make the
    // file big -- it destroys the two properties the format was chosen for,
    // since a row nobody can read is not diffable and readRecording's getline
    // would have to allocate the whole of it to parse the row it sits in.
    //
    // So above kMaxInlinePayload octets the payload leaves the file and the row
    // keeps a reference to it: the value column becomes '@' followed by a file
    // name in the recording's sidecar directory (see sidecarDirectoryFor). The
    // kind column is unchanged -- <bytes> is still what the row holds, and
    // where the octets are written down is not a fact about the observation.
    //
    // '@' cannot collide with an inline value for the same structural reason
    // kPayloadKind's angle brackets cannot collide with a unit's name: an
    // inline payload is hex, '@' is not a hex digit, and no future encoding of
    // a *number* can take that character either.
    //
    // The blobs are named by a hash of their own contents, not by the sequence
    // number of the row that first wrote one, and that is the load-bearing
    // choice here rather than an implementation detail:
    //
    //   - It keeps the file diffable, which was the whole point. A row reading
    //     "@0000012.bin" would be byte-identical between a run whose DUT
    //     answered correctly and one whose DUT did not; a row naming the
    //     content differs exactly when the content does.
    //   - A run that reads the same frame fifty times stores it once.
    //   - Nothing depends on write order, so the same observations produce the
    //     same sidecar directory whatever order they were recorded in.
    //
    // The hash is 64-bit FNV-1a, which is not a cryptographic digest and does
    // not need to be -- but two different payloads landing on one name would be
    // silently wrong data in a replay, which is the one failure this file
    // cannot have. So a writer that finds the name already taken reads the
    // blob back and compares: equal is the dedup case above, and unequal throws
    // rather than overwriting. A collision is therefore a loud error, not a
    // corrupt recording.
    //
    inline constexpr std::size_t kMaxInlinePayload = 128;

    // The character that makes a value column a reference rather than a value.
    inline constexpr char kSidecarMarker = '@';

    //
    // A line beginning with this is skipped on the way in, along with a blank
    // one.
    //
    // The syntax exists because a recording is an input as well as an output --
    // --replay reads one, and nothing stops a file being authored rather than
    // captured (which is the point of --skeleton, whose output is a valid
    // recording with placeholder values in it). Such a file needs to be able to
    // say what it is and what its numbers stand for, and a format whose only
    // reader is a machine still has a person holding it open in an editor.
    //
    // What this framework itself writes in comments is provenance and prose,
    // never data: the selection header below, and --skeleton's warning that its
    // values are placeholders. Nothing in a comment is ever read back. That is
    // the line to hold -- the moment a replay took a fact from a comment rather
    // than from the rows, an edited or stale header would outrank the
    // observations underneath it.
    //
    inline constexpr char kCommentMarker = '#';

    //
    // The header every recording this framework writes begins with: which tests
    // the run that produced it was asked to run.
    //
    //   # select=<all>
    //   # select=SupplyRail,StatusRegister
    //
    // Provenance for a person, and the question they are most likely to have
    // about a file that arrived from a bench: is this a whole run, or somebody's
    // narrow capture? A recording is as long as the run is, and two files a
    // fortnight apart are otherwise indistinguishable without reading every row.
    //
    // Deliberately not read back, and not what a selected replay checks itself
    // against -- see the comment on kCommentMarker directly above, and
    // core::ScriptedSession::loadFromFile, which keys its refusal on the test
    // column of the rows. The rows are what a replay actually consumes, so they
    // are the only honest answer to "does this file cover that test": a header
    // can be edited, can be copied between files, and can outlive the rows it
    // described. This one says what the run was told to do; the rows say what it
    // did.
    //
    // A comment rather than a column, unlike the test id beside each reading.
    // The selection is one fact about the run, not a fact about an observation,
    // and a column would repeat it identically on every row -- which is both
    // waste and a thing that can disagree with itself.
    //
    inline constexpr std::string_view kSelectionField = "select=";

    // What the selection header carries for a run that was given no --select.
    inline constexpr std::string_view kEverySelection = "<all>";

    //
    // Writes that header. One function so the two callers that write a
    // recording -- --record and --skeleton (see framework/runner/src/main.cpp) -- cannot
    // spell it two ways.
    //
    // Takes the stream rather than being a RecordingWriter member because it
    // has to come first, and a writer is constructed to append rows.
    //
    auto writeSelectionHeader(
        std::ostream &                         out,
        const std::vector<std::string_view> &  selection) -> void;

    //
    // Where a recording's heavy payloads live: the recording's own path with
    // ".d" appended, so "readings.tsv" pairs with "readings.tsv.d".
    //
    // Appended rather than substituted for the extension, which would be
    // prettier: "readings.d" is what both readings.tsv and readings.csv would
    // derive, and two recordings in one directory sharing a blob store is a way
    // to replay one run's payloads into another's. Appending is total and
    // cannot collide.
    //
    // One function so that the writer and the reader derive it the same way --
    // this pairing is the only thing that makes a recording findable from its
    // TSV, and two spellings of it would eventually be two rules.
    //
    [[nodiscard]]
    auto sidecarDirectoryFor( const std::filesystem::path & recording) -> std::filesystem::path;

    //
    // Writes rows one at a time, as they are observed.
    //
    // A writer object rather than only the whole-vector function below, because
    // holding a run's observations in memory until it ends stops being free the
    // moment one of them is large. core::RecordingSession accumulated every
    // sample and dumped them at the end, which is fine for a few hundred
    // voltages and is not fine for a hundred captured traces -- and the run
    // that most needs its recording is the long soak that is most likely to be
    // killed before it reaches the end.
    //
    // The sidecar directory is created lazily, on the first payload that needs
    // it, so a run that records nothing heavy leaves a single file behind
    // exactly as it did before.
    //
    // An empty sidecar directory means "inline everything", which is what the
    // whole-vector function below defaults to: a caller writing into a
    // std::ostringstream has nowhere to put a blob, and a test asserting on the
    // text of a recording wants the payload in it. Nothing is lost in that mode
    // -- a large payload simply produces a large row.
    //
    class RecordingWriter
    {
        public:
            explicit RecordingWriter( std::ostream & out, std::filesystem::path sidecarDirectory = {})
                : mOut( out), mSidecarDirectory( std::move( sidecarDirectory)) {}

            auto write( const RecordedSample & sample) -> void;

        private:
            //
            // The value column for a block of octets: the hex itself while it
            // is small, a reference to a sidecar file once it is not. Shared by
            // a payload row and a trace row's samples, which are the same
            // question asked of different contents.
            //
            [[nodiscard]]
            auto valueColumn( const Bytes & payload, std::string_view extension) -> std::string;

            //
            // Writes the octets to the sidecar directory if they are not
            // already there and answers the reference the value column carries.
            //
            [[nodiscard]]
            auto spill( const Bytes & payload, std::string_view extension) -> std::string;

            std::ostream &         mOut;
            std::filesystem::path  mSidecarDirectory;
            bool                   mDirectoryReady{ false };
    };

    //
    // Every sample at once -- what a caller with the whole run already in hand
    // uses. Equivalent to feeding a RecordingWriter each sample in turn.
    //
    auto writeRecording(
        std::ostream &                       out,
        const std::vector<RecordedSample> &  samples,
        const std::filesystem::path &        sidecarDirectory = {}) -> void;

    //
    // Reads a recording back, resolving any sidecar reference against
    // sidecarDirectory.
    //
    // Defaulted to empty, in which case a reference is a hard error rather than
    // an absent payload: a caller reading a recording it cannot fully resolve
    // has half a recording, and half a recording replayed is a run whose
    // payloads come from somewhere the file does not say.
    //
    [[nodiscard]]
    auto readRecording(
        std::istream &                 in,
        const std::filesystem::path &  sidecarDirectory = {}) -> std::vector<RecordedSample>;
} // namespace core
