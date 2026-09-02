#include "hal/io/scpi.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <utility>

namespace hal::io
{
    namespace
    {
        //
        // Leading and trailing whitespace off a reply. Instruments differ about
        // whether they pad a response, and a driver comparing a model string
        // against "34450A" should not have to know which kind it is talking to.
        //
        [[nodiscard]]
        auto trimmed( const std::string_view text) -> std::string_view
        {
            constexpr std::string_view whitespace = " \t\r\n";

            const auto first = text.find_first_not_of( whitespace);

            if( first == std::string_view::npos)
            {
                return {};
            }

            const auto last = text.find_last_not_of( whitespace);

            return text.substr( first, last - first + 1);
        }

        //
        // std::from_chars over strtod/strtol for every number read off the
        // wire, and the reason is not speed.
        //
        // strtod's decimal separator is LC_NUMERIC's, so on a machine whose
        // locale is Swedish -- which is the machine this was written on --
        // "+5.021E+00" parses as 5 and the run reports a five-volt rail as
        // 5.000 V. from_chars is defined to be locale-independent, always the
        // C locale, which is the only correct answer for bytes that came from
        // an instrument.
        //
        // What it costs is the leading plus. SCPI sends "+1.86850000E-03" and
        // from_chars rejects a leading '+' for both integers and floats
        // (deliberately -- it parses exactly what the standard's number grammar
        // says), so the sign is stripped first. That is the whole reason this
        // helper exists rather than a bare from_chars at each call site: the
        // detail is easy to omit and produces a parse failure on every positive
        // reading, which is most of them.
        //
        template<typename NumberT>
        [[nodiscard]]
        auto parseNumber( const std::string_view text) -> std::optional<NumberT>
        {
            auto digits = trimmed( text);

            if( digits.starts_with( '+'))
            {
                digits.remove_prefix( 1);
            }

            NumberT value{};

            const auto [ end, error] = std::from_chars( digits.data(), digits.data() + digits.size(), value);

            if( error != std::errc{} || end != digits.data() + digits.size())
            {
                return std::nullopt;
            }

            return value;
        }

        //
        // "-113,\"Undefined header\"" -> { -113, "Undefined header" }.
        //
        // Hand-parsed rather than scanf'd, because the message half can contain
        // anything including commas ("Settings conflict; DC voltage, autorange")
        // and a format string that stopped at the second comma would truncate
        // exactly the errors worth reading. The number ends at the first comma;
        // everything after it, minus the quotes, is the message.
        //
        // Returns nothing if the reply is not shaped like a queue entry at all,
        // which is treated by the caller as "stop reading the queue" rather
        // than as an error about an error -- see nextError().
        //
        [[nodiscard]]
        auto parseQueueEntry( const std::string_view reply) -> std::optional<ScpiError>
        {
            const auto comma = reply.find( ',');

            if( comma == std::string_view::npos)
            {
                return std::nullopt;
            }

            const auto code = parseNumber<int>( reply.substr( 0, comma));

            if( !code)
            {
                return std::nullopt;
            }

            auto message = trimmed( reply.substr( comma + 1));

            if( message.size() >= 2 && message.front() == '"' && message.back() == '"')
            {
                message = message.substr( 1, message.size() - 2);
            }

            return ScpiError{ *code, std::string( message) };
        }
    } // namespace

    ScpiSession::ScpiSession( std::unique_ptr<ITransport> transport) : mTransport( std::move( transport))
    {
        if( !mTransport)
        {
            //
            // A null transport is a caller bug rather than a bench condition,
            // and it is worth refusing here: every method below would otherwise
            // fail on the first dereference, several stack frames from whoever
            // constructed this.
            //
            throw TransportError( "a SCPI session needs a transport");
        }
    }

    auto ScpiSession::write( const std::string_view command) -> void
    {
        mTransport->send( command);
    }

    auto ScpiSession::checked( const std::string_view command) -> void
    {
        mTransport->send( command);

        //
        // The first entry names the fault; the rest are drained so they cannot
        // be attributed to the next command (see this function's declaration).
        // Draining happens before the throw, which is the ordering that
        // matters: an exception thrown with entries still queued would leave
        // the instrument's queue holding this command's errors for whoever
        // asks next.
        //
        std::optional<ScpiError> first = nextError();

        if( !first)
        {
            return;
        }

        clearErrors();

        throw ScpiFault( mTransport->description(), command, std::move( *first));
    }

    auto ScpiSession::query( const std::string_view question) -> std::string
    {
        mTransport->send( question);

        return std::string( trimmed( mTransport->receive()));
    }

    auto ScpiSession::queryChecked( const std::string_view question) -> std::string
    {
        try
        {
            return query( question);
        }
        catch( const TransportTimeout &)
        {
            //
            // Silence is how a SCPI instrument refuses a query, so the queue is
            // where the actual diagnosis is. Asked only on this path, and with
            // its own failure swallowed: if the instrument will not answer
            // SYST:ERR? either, the original timeout is the more useful of the
            // two exceptions and the one to let out.
            //
            try
            {
                if( auto error = nextError())
                {
                    clearErrors();

                    throw ScpiFault( mTransport->description(), question, std::move( *error));
                }
            }
            catch( const TransportError &)
            {
            }

            throw;
        }
    }

    auto ScpiSession::queryNumber( const std::string_view question) -> double
    {
        const std::string reply = queryChecked( question);

        const auto value = parseNumber<double>( reply);

        if( !value)
        {
            throw TransportError(
                mTransport->description() + " answered \"" + reply + "\" to \"" + std::string( question)
                + "\", which is not a number -- the session is out of step with its replies");
        }

        return *value;
    }

    auto ScpiSession::queryNumbers( const std::string_view question) -> std::vector<double>
    {
        const std::string reply = queryChecked( question);

        if( reply.empty())
        {
            throw TransportError(
                mTransport->description() + " answered nothing to \"" + std::string( question) + "\"");
        }

        std::vector<double> values;

        for( std::size_t start = 0; start <= reply.size(); )
        {
            const auto comma = reply.find( ',', start);
            const auto field = std::string_view( reply).substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);

            if( const auto value = parseNumber<double>( field))
            {
                values.push_back( *value);
            }
            else
            {
                throw TransportError(
                    mTransport->description() + " answered \"" + reply + "\" to \"" + std::string( question)
                    + "\", whose field \"" + std::string( field) + "\" is not a number"
                    + " -- the session is out of step with its replies");
            }

            if( comma == std::string::npos)
            {
                break;
            }

            start = comma + 1;
        }

        return values;
    }

    auto ScpiSession::identify() -> std::string
    {
        return queryChecked( "*IDN?");
    }

    auto ScpiSession::reset() -> void
    {
        write( "*RST");
        write( "*CLS");
    }

    auto ScpiSession::waitForComplete() -> void
    {
        //
        // The reply is "1" and is discarded: what is wanted is the *blocking*,
        // not the value. An instrument that answers at all has finished, which
        // is the entire content of the answer.
        //
        static_cast<void>( queryChecked( "*OPC?"));
    }

    auto ScpiSession::nextError() -> std::optional<ScpiError>
    {
        const std::string reply = query( "SYST:ERR?");

        auto entry = parseQueueEntry( reply);

        if( !entry || entry->Code == 0)
        {
            //
            // "+0,\"No error\"" is the empty queue, and anything unparseable is
            // treated the same way on purpose: a reply that is not a queue
            // entry means this session is reading somebody else's answer, and
            // the honest report of that is the desynchronisation that
            // queryNumber() raises on the next real reading -- not a second
            // exception from inside the error path, which would mask whatever
            // was being diagnosed.
            //
            return std::nullopt;
        }

        return entry;
    }

    auto ScpiSession::clearErrors() -> void
    {
        //
        // Bounded at the queue's own depth (20 on the meters here, and the
        // standard's minimum) plus one for the read that finds it empty. An
        // unbounded loop here would hang against an instrument whose queue
        // refills as fast as it is read, which is exactly the state a wedged
        // instrument is in.
        //
        constexpr int kMaximumQueueDepth = 21;

        for( int drained = 0; drained < kMaximumQueueDepth; ++drained)
        {
            if( !nextError())
            {
                return;
            }
        }
    }

    auto ScpiSession::description() const -> std::string
    {
        return mTransport->description();
    }

    auto ScpiSession::transport() -> ITransport &
    {
        return *mTransport;
    }

    auto ScpiSession::isOverload( const double reading) -> bool
    {
        return std::abs( reading) >= kOverloadThreshold;
    }

    auto ScpiSession::number( const double value) -> std::string
    {
        //
        // std::format's default double formatting: the shortest string that
        // round-trips, in the C locale regardless of what the process's locale
        // is. Both halves matter -- a decimal comma is a syntax error to every
        // instrument parser, and it is what std::to_string produces on a
        // machine set to a European locale.
        //
        return std::format( "{}", value);
    }
} // namespace hal::io
