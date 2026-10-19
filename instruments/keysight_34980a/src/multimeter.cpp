//
// hal::keysight_34980a::Multimeter's real I/O: the MEASure queries on the Analog Bus.
// See keysight_34980a_multimeter.hpp, and switch_unit.cpp for the document this is
// written against.
//
#include "hal/keysight_34980a.hpp"

#include <string>
#include <string_view>

#include "hal/io/transport.hpp"

namespace hal::keysight_34980a
{
    // ---------------------------------------------------------------------
    // Multimeter
    // ---------------------------------------------------------------------

    namespace
    {
        //
        // The MEASure query for each function, and its name for a message.
        // No channel list: the bare form measures the Analog Buses -- see the
        // header's comment on Multimeter, including what is not yet confirmed
        // about it.
        //
        struct DmmCommand
        {
            std::string_view Query;
            std::string_view Name;
            bool             TakesRange;
        };

        constexpr auto commandFor( const Multimeter::Function function) -> DmmCommand
        {
            switch( function)
            {
                case Multimeter::Function::DcVoltage:          return { "MEAS:VOLT:DC?", "DC voltage",       true  };
                case Multimeter::Function::AcVoltage:          return { "MEAS:VOLT:AC?", "AC voltage",       true  };
                case Multimeter::Function::Resistance:         return { "MEAS:RES?",     "resistance",       true  };
                case Multimeter::Function::FourWireResistance: return { "MEAS:FRES?",    "4-wire resistance", true  };

                //
                // No range: FREQ's range argument on this meter is the
                // *voltage* range of the signal being counted, not a frequency,
                // and a Frequency port's range() is a frequency. Sending one as
                // the other would be a unit error the instrument cannot see.
                //
                case Multimeter::Function::Frequency:          return { "MEAS:FREQ?",    "frequency",        false };
            }

            return { "", "", false };
        }

        //
        // "1" or "0", checked -- INST:DMM:INST? and INST:DMM? both answer so.
        //
        [[nodiscard]]
        auto answersYes( io::ScpiSession & session, const std::string_view question) -> bool
        {
            return session.queryNumber( question) != 0.0;
        }
    } // namespace

    auto Multimeter::prepare( io::ScpiSession & session) -> void
    {
        session.clearErrors();

        static_cast<void>( detail::verifyMainframe( session));

        //
        // The two facts a meter on this box depends on, asked before the
        // first reading rather than discovered by one: a box with no meter
        // fitted answers every MEASure with an error that says less than this.
        //
        if( !answersYes( session, "INST:DMM:INST?"))
        {
            throw io::ScpiFault( session.description(), "INST:DMM:INST?",
                io::ScpiError{ 0,
                    "this 34980A has no internal DMM fitted -- a row naming keysight_34980a::Multimeter"
                    " needs the optional DMM assembly" });
        }

        //
        // Disabled is not refused here by enabling it: INST:DMM ON makes the
        // mainframe issue a factory reset, and this face shares the box with
        // the switching (see SwitchUnit::setInternalDmm). So it says so and stops.
        //
        if( !answersYes( session, "INST:DMM?"))
        {
            throw io::ScpiFault( session.description(), "INST:DMM?",
                io::ScpiError{ 0,
                    "this 34980A's internal DMM is disabled, so it is not on the Analog Buses. Enable"
                    " it from the front panel or a bring-up session (INST:DMM ON) -- this driver"
                    " will not, because enabling it issues a factory reset" });
        }
    }

    auto Multimeter::session() -> io::ScpiSession &
    {
        return mConnection.session( "keysight_34980a.dmm", []( io::ScpiSession & opened)
        {
            prepare( opened);
        });
    }

    auto Multimeter::identity() -> std::string
    {
        return session().identify();
    }

    auto Multimeter::read( const Function function, const std::optional<double> range) -> double
    {
        auto &     scpi    = session();
        const auto command = commandFor( function);

        std::string query{ command.Query };

        if( range && command.TakesRange)
        {
            query += " " + io::ScpiSession::number( *range);
        }

        //
        // A query the instrument refuses gets no reply, so the refusal shows
        // up first as a timeout -- and a timeout alone says nothing about why.
        // So on one, ask the error queue: an entry there is the instrument's
        // own reason, reported against the query that caused it; an empty
        // queue is a real timeout, rethrown as it was.
        //
        double reading = 0.0;

        try
        {
            reading = scpi.queryNumbers( query).front();
        }
        catch( const io::TransportTimeout &)
        {
            if( const auto refusal = scpi.nextError())
            {
                throw io::ScpiFault( scpi.description(), query, *refusal);
            }

            throw;
        }

        //
        // And one that did answer may still have queued an error -- a range
        // the meter clamped, say -- which is the reading being of something
        // other than what was asked for.
        //
        if( const auto error = scpi.nextError())
        {
            throw io::ScpiFault( scpi.description(), query, *error);
        }

        if( io::ScpiSession::isOverload( reading))
        {
            const std::string where = range && command.TakesRange
                ? "the " + io::ScpiSession::number( *range) + " range"
                : "autoranging";

            throw core::UnmeasurableReading(
                std::string( command.Name) + " overload on the Analog Bus -- the input is beyond " + where);
        }

        return reading;
    }
} // namespace hal::keysight_34980a
