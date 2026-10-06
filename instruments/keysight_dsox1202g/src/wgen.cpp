#include "hal/keysight_dsox1202g.hpp"

#include <string>
#include <string_view>

//
// Everything the scope's waveform generator puts on the wire -- the :WGEN
// subsystem, from the 1000 X-Series programmer's guide. See the header's
// comment on WGEN for the order and for what is not yet confirmed.
//

namespace hal::keysight_dsox1202g
{
    namespace
    {
        [[nodiscard]]
        auto setting( const std::string_view command, const double value) -> std::string
        {
            return std::string( command) + " " + io::ScpiSession::number( value);
        }
    } // namespace

    auto WGEN::session() -> io::ScpiSession &
    {
        return mConnection.session( "keysight_dsox1202g.wgen", []( io::ScpiSession & opened)
        {
            opened.clearErrors();

            static_cast<void>( detail::verifyScope( opened, true));
        });
    }

    auto WGEN::identity() -> std::string
    {
        return session().identify();
    }

    namespace detail
    {
        auto program( io::ScpiSession & session, const GeneratorProgram & waveform) -> void
        {
            //
            // Each step through checked(), one error-queue read apiece: the
            // order only holds if each is known to have landed before the next
            // is sent.
            //
            if( !waveform.Load.empty())
            {
                session.checked( ":WGEN:OUTPut:LOAD " + std::string( waveform.Load));
            }

            session.checked( ":WGEN:FUNCtion " + std::string( waveform.Function));

            if( waveform.Hertz)
            {
                session.checked( setting( ":WGEN:FREQuency", *waveform.Hertz));
            }

            if( waveform.ShapeValue && !waveform.ShapeCommand.empty())
            {
                session.checked( setting( waveform.ShapeCommand, *waveform.ShapeValue));
            }

            if( waveform.Volts)
            {
                session.checked( setting( ":WGEN:VOLTage", *waveform.Volts));
            }

            if( waveform.OffsetVolts)
            {
                session.checked( setting( ":WGEN:VOLTage:OFFSet", *waveform.OffsetVolts));
            }

            session.checked( ":WGEN:OUTPut ON");
            session.waitForComplete();
        }

        auto disableOutput( io::ScpiSession & session) -> void
        {
            session.checked( ":WGEN:OUTPut OFF");
            session.waitForComplete();
        }

        auto outputIsOn( io::ScpiSession & session) -> bool
        {
            return session.queryNumber( ":WGEN:OUTPut?") != 0.0;
        }

        //
        // Off first, then :WGEN:RST -- the factory state, 1 kHz sine, 500 mVpp,
        // no offset, into 1 MOhm, with the output off. One command that cannot
        // be refused for the state the generator is in, which is why it is used
        // instead of collapsing the amplitude to a minimum: the minimum depends
        // on the load, and a refused write here would sit in the error queue the
        // next face to use the box reads.
        //
        auto sendSafe( io::ScpiSession & session) -> void
        {
            try
            {
                session.write( ":WGEN:OUTPut OFF");
                session.waitForComplete();
                session.write( ":WGEN:RST");
            }
            catch( const io::TransportError &)
            {
            }
        }
    } // namespace detail
} // namespace hal::keysight_dsox1202g
