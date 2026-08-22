#include "../prelude.hpp"

#include <chrono>
#include <string>

//
// Ask the DUT's debug console for its status register and check what came back.
//
// The point of this script, beyond the check itself, is the shape of a dialogue
// with the device: connect the interface, configure the port, send, read, check,
// disconnect. Every one of those is a separate verb because every one is a
// separate thing that can go wrong, and a log that shows them in order is what
// tells you whether a silent DUT was misconfigured, unconnected or simply dead.
//
auto consoleScript() -> void
{
    using namespace std::chrono_literals;

    //
    // Connect first, and the whole interface at once -- transmit, receive and
    // the ground return are one thing (see dut::Console). The route stays
    // closed for the rest of the script: unlike a Measure, which connects and
    // disconnects within the call, a dialogue would be broken by dropping the
    // path between a command and its answer.
    //
    Connect( Ser1.rs232(), at( dut::Console));

    //
    // Setup, not Apply. Configuring a UART changes what a later Write means and
    // changes nothing at the DUT's pins -- see core::SetupEngine on why that is
    // a verb of its own.
    //
    Setup( Ser1.rs232()
               .baudRate( 9600)
               .wordLength( 8)
               .parity( hal::Parity::None)
               .stopBits( hal::StopBits::One));

    Write( Ser1.rs232(), "RD 30\r");

    //
    // The terminator says what the read stops at; it comes back in the payload
    // rather than being stripped, so what follows can decide whether a
    // well-formed reply matters. Here it does: the acknowledgement is checked
    // against the bytes before the terminator, which is a different assertion
    // from "the reply happened to start with ACK".
    //
    const auto reply = Read( Ser1.rs232().terminator( "\r").timeout( 500ms));

    Verify( FS_Console_1::FS_Console_Ack, reply.before( "\r"));

    //
    // The status register is the byte after the acknowledgement. Guarded rather
    // than indexed blindly: a DUT that answered with something shorter than the
    // protocol says is exactly the case core::Bytes::at would throw on, and a
    // script that let that escape would report a crash where it should report a
    // failed check.
    //
    if( reply.size() > 4)
    {
        const auto status = reply.at( 4);

        Verify( FS_Console_1::FS_Console_Ready, status);
        Verify( FS_Console_1::FS_Console_Fault, status);
    }
    else
    {
        //
        // No status byte to check. Both criteria are recorded rather than
        // skipped, because a report in which a check simply does not appear
        // reads as a run that did not need it -- and recorded as unchecked
        // rather than as failed, because neither criterion is a claim this run
        // has any evidence about. Fail names them, so a consumer tracking
        // either one across runs sees that this run could not answer it.
        //
        // The reason carries the size the DUT actually sent, which is the one
        // fact here that is not already in the criteria table.
        //
        const auto reason = "console reply is " + std::to_string( reply.size())
                          + " bytes, too short to hold a status byte";

        Fail( FS_Console_1::FS_Console_Ready, reason);
        Fail( FS_Console_1::FS_Console_Fault, reason);
    }

    Disconnect( Ser1.rs232(), at( dut::Console));
}
