#pragma once

#include <string>
#include <string_view>

namespace core
{
    //
    // JSON string escaping, and nothing else.
    //
    // Extracted from core::SarifSink, which had the only copy, when a second
    // JSON stream appeared (core/journal/event_sink.hpp). Two escapers would be
    // two answers to "is a tab \t or 	", and the one that matters is the
    // one nobody tests -- so there is one implementation and SarifSink::escape
    // now forwards to it, keeping its own name for the tests and consumers that
    // already call it.
    //
    // Deliberately not a JSON *writer*. The two sinks build very different
    // documents -- one nested and indented for a person to open, one flat and
    // one-line-per-event for a program to read a line at a time -- and a shared
    // writer would have to model both. What they genuinely share is this: the
    // rule for turning a criterion's description into a string a parser will
    // accept.
    //
    [[nodiscard]]
    auto jsonEscape( std::string_view text) -> std::string;

    //
    // The same, quoted. Every string written into either document goes through
    // this: there is no "this one cannot contain a quote" shortcut, because the
    // strings involved are prose out of dut/adapter.inc and the criteria tables
    // and nothing checks them for punctuation.
    //
    [[nodiscard]]
    auto jsonQuoted( std::string_view text) -> std::string;
} // namespace core
