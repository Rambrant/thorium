#pragma once

#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/journal.hpp"
#include "core/report.hpp"

namespace core
{
    //
    // The human-readable log, as a colour-coded RTF file.
    //
    // RTF rather than HTML or plain text for a specific reason: this file is an
    // attachment to a test report. It opens, already formatted, in Word, Pages,
    // TextEdit, and LibreOffice with nothing installed and no rendering
    // decisions left to whoever opens it, and it prints as what it looks like
    // on screen. Its content is exactly what core/report.hpp composes -- see
    // that header on why the console view and this file cannot drift apart.
    //
    // ---------------------------------------------------------------------
    // Readable *during* the run, which is the unusual part of this class
    // ---------------------------------------------------------------------
    // An RTF document is a single brace-delimited group: the closing '}' is
    // the last byte of the file. So the naive "append as you go, close the
    // group at the end" approach produces a file that is not a valid RTF
    // document at any point until the run finishes -- exactly when nobody
    // needs to read it, and useless for a run that crashes or a person
    // watching a long soak test.
    //
    // Instead, every flush here writes the closing brace, flushes, and then
    // seeks back one byte so the next write overwrites it (see
    // flushDocument()). The file on disk is therefore a complete, valid,
    // openable RTF document after every single logged event, at the cost of
    // one byte rewritten per flush. An operator can open it mid-run, and a run
    // killed at any instant leaves a readable log rather than a truncated one.
    //
    // That trick is why this sink owns a std::ofstream directly rather than
    // taking a std::ostream & the way core::writeRecording does: it needs a
    // seekable stream, which an arbitrary ostream is not.
    //
    class RtfSink : public IJournalSink
    {
        public:
            //
            // Opens path for writing and emits the RTF preamble immediately,
            // so the file is a valid (if empty) document before the first
            // event -- a run that fails during setup still leaves something
            // openable behind. Throws if the file cannot be opened: a run
            // asked to produce a log and silently not producing one is the
            // failure mode this exists to avoid.
            //
            explicit RtfSink( const std::string & path);

            //
            // Closes the document if onRunEnd never arrived -- an exception
            // escaping past the runner, or a caller that simply forgot. The
            // seek-back scheme above means the file was already valid, so
            // this only finalises it (stops rewriting the trailer) rather
            // than rescuing it.
            //
            ~RtfSink() override;

            RtfSink( const RtfSink &)                     = delete;
            auto operator=( const RtfSink &) -> RtfSink & = delete;

            auto onRunStart( const RunInfo & info) -> void override;
            auto onTestStart( std::string_view group, std::string_view test, std::string_view description) -> void override;
            auto onEvent( const JournalEvent & event) -> void override;
            auto onTestEnd( std::string_view group, std::string_view test, bool passed) -> void override;
            auto onRunEnd( bool allPassed) -> void override;

            //
            // RTF escaping, exposed for its own test: '\', '{' and '}' are
            // RTF's own syntax and a criterion description containing one
            // would otherwise corrupt the document, and any byte above ASCII
            // needs \'hh form since the header declares \ansi.
            //
            [[nodiscard]]
            static auto escape( std::string_view text) -> std::string;

        private:
            auto write( const ReportLine & line) -> void;
            auto writeAll( const std::vector<ReportLine> & lines) -> void;

            // Writes the closing brace, flushes, and seeks back over it -- see
            // this class's own comment.
            auto flushDocument() -> void;

            // Writes the closing brace for good and stops rewriting it.
            auto finalise() -> void;

            std::ofstream  mOut;
            bool           mFinalised{ false };
    };
} // namespace core
