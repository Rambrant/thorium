//
// hal::keysight_34980a::Chassis's own tests.
//
// Linked against this driver alone -- hal and core and nothing else -- which is
// what keeps this directory independently packageable (see
// instruments/README.md).
//
// And the one test file here that names no hal::InstrumentId at all, because a
// chassis has none: it is a switching device, not an instrument (see the
// header's preamble). Which makes these the only driver tests in the tree that
// a deployment declaring one meter and no switching can still build -- the
// coupling the top-level CMakeLists.txt calls a real defect simply does not
// arise.
//
#include "hal/keysight_34980a.hpp"

//
// For the two assertions that this class is NOT an instrument -- see the
// namespace below. hal/keysight_34980a.hpp deliberately does not include this,
// because a chassis needs nothing from it; the test does, precisely in order to
// say so.
//
#include "hal/driver/instrument.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using hal::keysight_34980a::AnalogBus;
using hal::keysight_34980a::ChannelAddress;
using hal::keysight_34980a::Chassis;
using hal::keysight_34980a::ModuleIdentity;
using hal::keysight_34980a::NoSuchSlot;
using hal::keysight_34980a::analogBus;
using hal::keysight_34980a::channelList;
using hal::keysight_34980a::kSlots;

//
// This mainframe's back panel, as the constructor constraint actually sees it
// -- checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp uses
// for hal::SafeableInstrument, and framework/hal/tests/driver/test_address.cpp
// for the hal::ReachableOver mechanism itself).
//
// GPIB, LAN and USB are all standard on a 34980A, so this is the one row in
// framework/hal/README.md's table that rules out only hal::Serial. Asserted
// anyway: "this box has no serial port" is a fact worth failing to compile on,
// and the assertion is what stops the constraint being quietly widened to
// anything.
//
namespace
{
    static_assert(   std::constructible_from< Chassis, hal::Gpib> );
    static_assert(   std::constructible_from< Chassis, hal::Lan> );
    static_assert(   std::constructible_from< Chassis, hal::Usb> );
    static_assert(   std::constructible_from< Chassis, hal::Simulated> );
    static_assert( ! std::constructible_from< Chassis, hal::Serial> );

    //
    // An address is not optional: a chassis the PC cannot reach is not a
    // chassis a rig has.
    //
    static_assert( ! std::constructible_from< Chassis> );

    //
    // And no id of any kind is accepted, which is the assertion that pins down
    // the header's central claim -- a mainframe is neither an instrument nor a
    // card, so nothing identifies it but its address. If a later change gives
    // this class an id, it should be because the fabric grew somewhere to put
    // one, and this line should fail and be deleted deliberately.
    //
    static_assert( ! std::constructible_from< Chassis, hal::InstrumentId, hal::Lan> );

    //
    // Not safeable, and not an instrument. Both deliberate, both argued in the
    // header's preamble, and both worth an assertion because "add
    // InstrumentTag so safeRig() picks it up" is exactly the plausible-looking
    // change that would fold a switching device into the instrument list and
    // hand every Measure overload a chassis.
    //
    static_assert( ! std::derived_from< Chassis, hal::InstrumentTag> );
    static_assert( ! hal::SafeableInstrument< Chassis> );

    static_assert( kSlots == 8);

    //
    // The compile-time channel spelling, which is a constant expression -- so
    // the positive cases are assertions rather than tests, the same way
    // rig/tests/test_switch_device.cpp asserts hal::crosspoint's arithmetic.
    //
    static_assert( hal::keysight_34980a::channel<1, 3>()   == ChannelAddress{ 1, 3 });
    static_assert( hal::keysight_34980a::channel<8, 999>() == ChannelAddress{ 8, 999 });
    static_assert( hal::keysight_34980a::channel<3, 911>() == ChannelAddress{ 3, 911 });

    //
    // The negative cases are NOT asserted here, and cannot be: what rejects
    // channel<9, 3>() is a static_assert in its body, which is a diagnostic
    // rather than a constraint -- so a concept wrapped around it reports
    // "satisfied" either way, and writing the call fails the build. Exactly the
    // position hal::hop is in.
    //
    // So the rejection is tested one level down, on the predicates the
    // assertions are written in terms of, where both directions are ordinary
    // expressions. Same division rig/tests/test_switch_device.cpp uses for
    // hal::hasChannel against hal::hop.
    //
    static_assert(  hal::keysight_34980a::isSlot( 1));
    static_assert(  hal::keysight_34980a::isSlot( 8));
    static_assert( !hal::keysight_34980a::isSlot( 0));
    static_assert( !hal::keysight_34980a::isSlot( 9));

    static_assert(  hal::keysight_34980a::isAddressableChannel( 0));
    static_assert(  hal::keysight_34980a::isAddressableChannel( 911));
    static_assert(  hal::keysight_34980a::isAddressableChannel( 999));
    static_assert( !hal::keysight_34980a::isAddressableChannel( 1000));
    static_assert( !hal::keysight_34980a::isAddressableChannel( -1));
} // namespace

// ===========================================================================
// Channel lists
// ===========================================================================
//
// The punctuation is the instrument's rule rather than a formatting choice, and
// it is the thing most likely to be broken by a careless edit while still
// compiling -- so it is asserted directly rather than only through the commands
// that use it.
//

TEST( Keysight34980AChannels, AChannelIsWrittenSlotThenThreeDigits)
{
    EXPECT_EQ( channelList( ChannelAddress{ 1, 3 }),    "(@1003)");
    EXPECT_EQ( channelList( ChannelAddress{ 7, 203 }),  "(@7203)");
    EXPECT_EQ( channelList( ChannelAddress{ 8, 999 }),  "(@8999)");
}

//
// The padding is what makes the slot/channel boundary unambiguous: without it,
// slot 1 channel 3 and slot 13 channel <nothing> are the same four characters.
//
TEST( Keysight34980AChannels, TheChannelIsPaddedToThreeDigitsSoTheSlotStaysOneDigit)
{
    EXPECT_EQ( channelList( ChannelAddress{ 1, 0 }),  "(@1000)");
    EXPECT_EQ( channelList( ChannelAddress{ 1, 3 }),  "(@1003)");
    EXPECT_EQ( channelList( ChannelAddress{ 1, 30 }), "(@1030)");
}

TEST( Keysight34980AChannels, SeveralChannelsAreOneCommaSeparatedList)
{
    const std::vector<ChannelAddress> channels{ { 1, 5 }, { 1, 13 }, { 2, 15 } };

    EXPECT_EQ( channelList( channels), "(@1005,1013,2015)");
}

//
// The order is preserved because it is the order the instrument operates in --
// on a break-before-make module (the 34925A FET multiplexer) the last channel
// closed in each bank is the one left closed, so a list is not a set.
//
TEST( Keysight34980AChannels, TheOrderOfAListIsPreserved)
{
    EXPECT_EQ( channelList( std::vector<ChannelAddress>{ { 1, 13 }, { 1, 5 } }), "(@1013,1005)");
}

//
// The Analog Bus relays are ordinary channels as far as addressing goes, and
// their number is 9<bank><bus> -- which is the part this driver got wrong at
// first, by hard-coding bank 1. A module has one relay per bus per bank.
//
TEST( Keysight34980AChannels, AnalogBusRelaysAreNineHundredPlusBankAndBus)
{
    EXPECT_EQ( channelList( analogBus( 1, 1, AnalogBus::One)),   "(@1911)");
    EXPECT_EQ( channelList( analogBus( 3, 1, AnalogBus::Two)),   "(@3912)");
    EXPECT_EQ( channelList( analogBus( 8, 1, AnalogBus::Four)),  "(@8914)");
}

//
// And bank 2's are 921-924, which is not a detail: on a matrix module in
// two-wire mode -- a 34932A, which is what this rig is migrating onto -- only
// Matrix 2 reaches the Analog Buses, so 921-924 are the only ones that exist
// and 911 is not a channel of that module at all.
//
TEST( Keysight34980AChannels, MatrixModulesUseBankTwosAnalogBusRelays)
{
    EXPECT_EQ( channelList( analogBus( 1, 2, AnalogBus::One)),  "(@1921)");
    EXPECT_EQ( channelList( analogBus( 1, 2, AnalogBus::Two)),  "(@1922)");
    EXPECT_EQ( channelList( analogBus( 4, 2, AnalogBus::Four)), "(@4924)");
}

// ===========================================================================
// Slots
// ===========================================================================

TEST( Keysight34980ASlots, ASlotTheChassisDoesNotHaveIsRefusedAtRuntime)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_THROW( chassis.close( ChannelAddress{ 0, 3 }), NoSuchSlot);
    EXPECT_THROW( chassis.close( ChannelAddress{ 9, 3 }), NoSuchSlot);
    EXPECT_THROW( chassis.openAll( 9),                    NoSuchSlot);
    EXPECT_THROW( static_cast<void>( chassis.moduleIn( 0)), NoSuchSlot);

    EXPECT_NO_THROW( chassis.close( ChannelAddress{ 1, 3 }));
    EXPECT_NO_THROW( chassis.close( ChannelAddress{ 8, 3 }));
}

//
// Refused on a *simulated* chassis too, which is the whole point of the check
// living in the driver: every rig in this repository's CI is simulated, so a
// check that only fired against hardware would be a check that never fires.
// Same argument hal::keysight_edu36311a::RatingExceeded makes about a supply's
// rating.
//
TEST( Keysight34980ASlots, TheRefusalNamesTheSlotAndTheRange)
{
    Chassis chassis{ hal::Simulated{} };

    try
    {
        chassis.close( ChannelAddress{ 9, 3 });

        FAIL() << "expected a NoSuchSlot";
    }
    catch( const NoSuchSlot & refused)
    {
        const std::string message = refused.what();

        EXPECT_NE( message.find( "9"), std::string::npos) << message;
        EXPECT_NE( message.find( "8"), std::string::npos) << message;
    }
}

//
// A list with one bad slot in it sends nothing at all, rather than closing the
// good half and then throwing. On a switching device a half-executed route is
// worse than none: it is a path nobody wrote down.
//
TEST( Keysight34980ASlots, AListWithOneBadSlotClosesNoneOfIt)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_THROW( chassis.close( std::vector<ChannelAddress>{ { 1, 3 }, { 9, 3 }, { 2, 4 } }), NoSuchSlot);

    EXPECT_TRUE( chassis.simulatedClosedChannels().empty());
}

// ===========================================================================
// Switching, simulated
// ===========================================================================

TEST( Keysight34980A, CloseAndOpenTrackStateOnASimulatedChassis)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 3 }));

    chassis.close( ChannelAddress{ 1, 3 });

    EXPECT_TRUE( chassis.isClosed( ChannelAddress{ 1, 3 }));

    chassis.open( ChannelAddress{ 1, 3 });

    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 3 }));
}

//
// A relay is open or closed, not a counter -- so closing twice and opening once
// leaves it open. Which is the difference between this class and
// hal::SwitchFabric, and it is deliberate in both places: the fabric
// use-counts because two callers can legitimately need one relay closed at
// once, and that arbitration belongs one layer up from the hardware. See the
// header's comment on simulatedClosedChannels().
//
TEST( Keysight34980A, ARelayIsNotAUseCount)
{
    Chassis chassis{ hal::Simulated{} };

    chassis.close( ChannelAddress{ 1, 3 });
    chassis.close( ChannelAddress{ 1, 3 });
    chassis.open(  ChannelAddress{ 1, 3 });

    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 3 }));
}

TEST( Keysight34980A, OpenAllClearsOneSlotOrTheWholeChassis)
{
    Chassis chassis{ hal::Simulated{} };

    chassis.close( std::vector<ChannelAddress>{ { 1, 3 }, { 1, 4 }, { 2, 5 } });

    chassis.openAll( 1);

    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 3 }));
    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 4 }));
    EXPECT_TRUE(  chassis.isClosed( ChannelAddress{ 2, 5 }));

    chassis.openAll();

    EXPECT_TRUE( chassis.simulatedClosedChannels().empty());
}

//
// "Exclusive" is per *module*, not per bank and not per channel -- the sharp
// edge the header warns about. A simulation that only added the named channels
// would be indistinguishable from close(), which is precisely what a test of an
// RF bank is checking.
//
TEST( Keysight34980A, CloseExclusivelyDropsEveryOtherChannelInThatSlotOnly)
{
    Chassis chassis{ hal::Simulated{} };

    chassis.close( std::vector<ChannelAddress>{ { 1, 3 }, { 1, 4 }, { 2, 5 } });

    chassis.closeExclusively( ChannelAddress{ 1, 11 });

    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 3 }));
    EXPECT_FALSE( chassis.isClosed( ChannelAddress{ 1, 4 }));
    EXPECT_TRUE(  chassis.isClosed( ChannelAddress{ 1, 11 }));

    // Slot 2 is a different module and is untouched.
    EXPECT_TRUE( chassis.isClosed( ChannelAddress{ 2, 5 }));
}

TEST( Keysight34980A, AnEmptyChannelListIsANoOpRatherThanAMalformedCommand)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_NO_THROW( chassis.close( std::vector<ChannelAddress>{}));
    EXPECT_NO_THROW( chassis.open( std::vector<ChannelAddress>{}));

    EXPECT_TRUE( chassis.simulatedClosedChannels().empty());
}

TEST( Keysight34980A, ASimulatedChassisReportsEverySlotEmptyUntilToldOtherwise)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_TRUE( chassis.moduleIn( 4).Empty);

    chassis.setSimulatedModule( 4, ModuleIdentity{ "Agilent Technologies", "34932A", "MY44001234", "1.21", false });

    EXPECT_FALSE( chassis.moduleIn( 4).Empty);
    EXPECT_EQ( chassis.moduleIn( 4).Model, "34932A");

    EXPECT_TRUE( chassis.moduleIn( 5).Empty);

    const auto rack = chassis.modules();

    ASSERT_EQ( rack.size(), static_cast<std::size_t>( kSlots));
    EXPECT_EQ( rack[ 3].Model, "34932A");
    EXPECT_TRUE( rack[ 0].Empty);
}

//
// A simulated chassis with no DMM fitted cannot enable one, which is the
// refusal the real instrument makes too. Modelled rather than ignored so that
// the future DMM face's installed-check behaves the same way against both.
//
TEST( Keysight34980A, ASimulatedChassisHasNoInternalDmmUnlessGivenOne)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_FALSE( chassis.internalDmmInstalled());
    EXPECT_FALSE( chassis.internalDmmEnabled());

    chassis.setInternalDmm( true);

    EXPECT_FALSE( chassis.internalDmmEnabled()) << "enabled a DMM that is not fitted";

    chassis.setSimulatedInternalDmm( true, true);

    EXPECT_TRUE( chassis.internalDmmInstalled());
    EXPECT_TRUE( chassis.internalDmmEnabled());

    chassis.setInternalDmm( false);

    EXPECT_TRUE(  chassis.internalDmmInstalled());
    EXPECT_FALSE( chassis.internalDmmEnabled());
}

//
// Waiting on a simulated chassis opens no session, which matters for a detached
// run: a simulated relay moved the instant it was asked to, and reaching for a
// socket purely to wait for hardware that is not there would be the one thing
// simulation exists to avoid.
//
TEST( Keysight34980A, WaitingOnASimulatedChassisTouchesNothing)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_NO_THROW( chassis.waitForSwitching());
    EXPECT_NO_THROW( chassis.waitForSwitching( 3));
    EXPECT_TRUE( chassis.isSimulated());
}

//
// ===========================================================================
// The wire: what this mainframe is actually told
// ===========================================================================
//
// Everything above tests the driver's simulated half. This half tests the
// other one: the exact SCPI it would put on a socket, asserted without a
// socket.
//
// Which is the whole reason hal::io::ITransport is an interface rather than a
// socket class (see hal/io/transport.hpp). A driver written against a concrete
// connection can only be checked against hardware, which means its command
// strings are verified by a human reading them once and then never again --
// including after the edit that broke them.
//
// It matters in a particular way on a switching device. A wrong command to a
// meter produces a reading that is wrong; a wrong command to a matrix produces
// a reading of a *different node*, which is plausible, in tolerance often
// enough, and attributed to the pin the script asked for. There is nothing
// downstream that can notice.
//
// What this cannot test, and no test here could: that the *instrument* accepts
// these strings, and that the channels named exist on whatever is plugged in.
// The first is what the programmer's reference was read for (see
// src/keysight_34980a.cpp for the document); the second is what the module
// models will check, and they are deliberately not built yet -- so for now the
// mainframe's own error queue is what catches it.
//
namespace
{
    //
    // A mainframe made of canned replies: it records every command it is given
    // and answers the queries this driver asks, so a test can assert the
    // conversation and steer any part of it.
    //
    // Answering by rule rather than by a fixed script, deliberately. A test
    // that had to write out the whole exchange in order would have to state the
    // error-queue reads and the identity query that it does not care about, and
    // would break whenever an unrelated part of the sequence changed -- the
    // failure mode that gets a test deleted rather than fixed.
    //
    class FakeChassis final : public hal::io::ITransport
    {
        public:
            auto send( const std::string_view command) -> void override
            {
                mSent->emplace_back( command);

                if( command == "*IDN?")
                {
                    mReplies.emplace_back( Identity);
                }
                else if( command == "SYST:ERR?")
                {
                    //
                    // "+0,\"No error\"" is the empty queue, which is what an
                    // instrument that accepted everything answers -- see
                    // hal::io::ScpiSession::nextError().
                    //
                    if( Errors.empty())
                    {
                        mReplies.emplace_back( "+0,\"No error\"");
                    }
                    else
                    {
                        mReplies.emplace_back( Errors.front());
                        Errors.erase( Errors.begin());
                    }
                }
                else if( command.starts_with( "SYST:CTYP?"))
                {
                    mReplies.emplace_back( ModuleReply);
                }
                else if( command.starts_with( "ROUT:CLOS?"))
                {
                    mReplies.emplace_back( ClosedReply);
                }
                else if( command.starts_with( "ROUT:MOD:WAIT?"))
                {
                    mReplies.emplace_back( "1");
                }
                else if( command == "INST:DMM:INST?")
                {
                    mReplies.emplace_back( DmmInstalledReply);
                }
                else if( command == "INST:DMM?")
                {
                    mReplies.emplace_back( DmmEnabledReply);
                }
                else if( !command.empty() && command.back() == '?')
                {
                    mReplies.emplace_back( "0");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    //
                    // Silence, which is exactly how a real instrument refuses a
                    // query -- so a driver bug that queries something this fake
                    // does not answer surfaces as the timeout it would surface
                    // as on the bench, rather than as an empty string that
                    // parses as zero.
                    //
                    throw hal::io::TransportTimeout( "nothing queued on the fake chassis");
                }

                std::string reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake 34980A";
            }

            [[nodiscard]]
            auto sent() const -> const std::vector<std::string> &
            {
                return *mSent;
            }

            //
            // The record, as a handle that outlives this transport.
            //
            // Which is not gold-plating: the driver *owns* the transport (see
            // Chassis::useTransport), so closeSession() destroys it -- and a
            // test asking "did closing the session send anything" would
            // otherwise be reading freed memory to find out. This is the only
            // way to assert anything about what a driver does on its way out.
            //
            [[nodiscard]]
            auto record() const -> std::shared_ptr<const std::vector<std::string>>
            {
                return mSent;
            }

            std::string              Identity{ "Agilent Technologies,34980A,MY44001234,2.43-2.42-1.19" };
            std::string              ModuleReply{ "\"Agilent Technologies,34932A,MY44002345,1.21\"" };
            std::string              ClosedReply{ "1" };
            std::string              DmmInstalledReply{ "1" };
            std::string              DmmEnabledReply{ "1" };
            std::vector<std::string> Errors;

        private:
            std::shared_ptr<std::vector<std::string>> mSent{ std::make_shared<std::vector<std::string>>() };
            std::vector<std::string>                  mReplies;
    };

    //
    // A chassis with a fake transport already installed, and the fake still
    // reachable. A struct rather than two locals because the driver takes
    // ownership of the transport (see Chassis::useTransport) and the test still
    // needs to read what was sent through it.
    //
    struct Bench
    {
        Chassis       Unit{ hal::Simulated{} };
        FakeChassis * Wire{};

        //
        // The same log the fake writes to, held here so it survives the fake --
        // see FakeChassis::record(). Wire dangles the moment the driver drops
        // its session; this does not.
        //
        std::shared_ptr<const std::vector<std::string>> Log;
    };

    [[nodiscard]]
    auto attached() -> std::unique_ptr<Bench>
    {
        auto bench = std::make_unique<Bench>();
        auto fake  = std::make_unique<FakeChassis>();

        bench->Wire = fake.get();
        bench->Log  = fake->record();

        bench->Unit.useTransport( std::move( fake));

        return bench;
    }

    //
    // Everything the driver sent, with the session-opening exchange dropped --
    // the error-queue drain and the identity query, which happen once and which
    // most of these tests are not about. Asserted in full by the one test that
    // is about them.
    //
    [[nodiscard]]
    auto afterOpening( const FakeChassis & wire) -> std::vector<std::string>
    {
        auto commands = wire.sent();

        commands.erase( commands.begin(), commands.begin() + 2);

        return commands;
    }
} // namespace

TEST( Keysight34980AWire, AnInjectedTransportMakesTheDriverStopSimulating)
{
    Chassis chassis{ hal::Simulated{} };

    EXPECT_TRUE( chassis.isSimulated());

    chassis.useTransport( std::make_unique<FakeChassis>());

    EXPECT_FALSE( chassis.isSimulated());
}

TEST( Keysight34980AWire, OpeningASessionDrainsTheErrorQueueThenAsksWhatTheChassisIs)
{
    const auto bench = attached();

    static_cast<void>( bench->Unit.session());

    ASSERT_GE( bench->Wire->sent().size(), 2u);

    EXPECT_EQ( bench->Wire->sent()[ 0], "SYST:ERR?");
    EXPECT_EQ( bench->Wire->sent()[ 1], "*IDN?");
}

TEST( Keysight34980AWire, CloseAndOpenAreCheckedRouteCommands)
{
    const auto bench = attached();

    bench->Unit.close( ChannelAddress{ 1, 3 });
    bench->Unit.open(  ChannelAddress{ 1, 3 });

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{
                   "ROUT:CLOS (@1003)",
                   "SYST:ERR?",
                   "ROUT:OPEN (@1003)",
                   "SYST:ERR?" }));
}

//
// One command for several channels, not one per channel -- which on this box is
// not merely fewer round trips: a multi-channel close is executed
// break-before-make within a bank on the FET multiplexer, and the high-density
// matrix switches measurably faster given a list.
//
TEST( Keysight34980AWire, SeveralChannelsGoInOneCommandWithOneErrorCheck)
{
    const auto bench = attached();

    bench->Unit.close( std::vector<ChannelAddress>{ { 1, 5 }, { 1, 13 }, { 2, 15 } });

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "ROUT:CLOS (@1005,1013,2015)", "SYST:ERR?" }));
}

//
// No ranges, ever. A range silently skips every Analog Bus relay it spans and
// errors outright if one is an endpoint, so "(@1005:1010)" is a spelling whose
// meaning depends on what is plugged in. An explicit list means what it says on
// any rack.
//
TEST( Keysight34980AWire, NothingThisDriverSendsUsesAChannelRange)
{
    const auto bench = attached();

    bench->Unit.close( std::vector<ChannelAddress>{ { 1, 5 }, { 1, 6 }, { 1, 7 }, { 1, 8 } });
    bench->Unit.open( std::vector<ChannelAddress>{ { 1, 5 }, { 1, 6 } });
    bench->Unit.openAll();

    for( const auto & command : bench->Wire->sent())
    {
        EXPECT_EQ( command.find( ':', command.find( "(@")), std::string::npos)
            << "a channel range reached the wire: " << command;
    }
}

TEST( Keysight34980AWire, CloseExclusivelyIsItsOwnCommand)
{
    const auto bench = attached();

    bench->Unit.closeExclusively( ChannelAddress{ 5, 11 });

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "ROUT:CLOS:EXCL (@5011)", "SYST:ERR?" }));
}

TEST( Keysight34980AWire, IsClosedIsAQueryWithASpaceBeforeItsChannelList)
{
    const auto bench = attached();

    bench->Wire->ClosedReply = "1";

    EXPECT_TRUE( bench->Unit.isClosed( ChannelAddress{ 7, 203 }));

    bench->Wire->ClosedReply = "0";

    EXPECT_FALSE( bench->Unit.isClosed( ChannelAddress{ 7, 203 }));

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "ROUT:CLOS? (@7203)", "ROUT:CLOS? (@7203)" }));
}

//
// isClosed() asks the hardware rather than remembering, which on a switching
// device is worth more than on a supply: relay state survives a power cycle on
// a latching module, a front-panel operator can change it, and a scan opens
// whole banks on its own initiative.
//
TEST( Keysight34980AWire, IsClosedReportsTheInstrumentNotThisDriversMemory)
{
    const auto bench = attached();

    bench->Wire->ClosedReply = "1";

    // Nothing has been closed through this driver at all.
    EXPECT_TRUE( bench->Unit.isClosed( ChannelAddress{ 1, 3 }));
}

TEST( Keysight34980AWire, OpenAllTakesASlotOrAll)
{
    const auto bench = attached();

    bench->Unit.openAll( 3);
    bench->Unit.openAll();

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{
                   "ROUT:OPEN:ALL 3",
                   "SYST:ERR?",
                   "ROUT:OPEN:ALL ALL",
                   "SYST:ERR?" }));
}

//
// The query form of the wait, not the command form: both wait, and the query
// returns "1" when the wait is over, so a caller finds out that the mainframe
// answered rather than only that the bytes were sent. Same reasoning as *OPC?
// over *WAI -- and note this waits for *switching* only, where *WAI would also
// wait for a scan.
//
TEST( Keysight34980AWire, WaitForSwitchingIsARouteModuleWaitQuery)
{
    const auto bench = attached();

    bench->Unit.waitForSwitching( 2);
    bench->Unit.waitForSwitching();

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "ROUT:MOD:WAIT? 2", "ROUT:MOD:WAIT? ALL" }));
}

TEST( Keysight34980AWire, ModuleInParsesTheIdentityReplyAndStripsItsQuotes)
{
    const auto bench = attached();

    bench->Wire->ModuleReply = "\"Agilent Technologies,34921A,MY44005678,1.05\"";

    const auto module = bench->Unit.moduleIn( 2);

    EXPECT_FALSE( module.Empty);
    EXPECT_EQ( module.Vendor,   "Agilent Technologies");
    EXPECT_EQ( module.Model,    "34921A");
    EXPECT_EQ( module.Serial,   "MY44005678");
    EXPECT_EQ( module.Firmware, "1.05");

    EXPECT_EQ( afterOpening( *bench->Wire), ( std::vector<std::string>{ "SYST:CTYP? 2" }));
}

//
// The instrument's way of saying "nothing here" is zeros in an identity reply
// rather than an error -- recognised so that a caller comparing Model against a
// part number does not have to know that "0" is a sentinel.
//
TEST( Keysight34980AWire, AnEmptySlotIsRecognisedFromItsZerosRatherThanAnError)
{
    const auto bench = attached();

    bench->Wire->ModuleReply = "\"Agilent Technologies,0,0,0\"";

    const auto module = bench->Unit.moduleIn( 6);

    EXPECT_TRUE( module.Empty);
    EXPECT_TRUE( module.Model.empty());
    EXPECT_TRUE( module.Serial.empty());
}

TEST( Keysight34980AWire, ModulesWalksAllEightSlotsInOrder)
{
    const auto bench = attached();

    const auto rack = bench->Unit.modules();

    EXPECT_EQ( rack.size(), static_cast<std::size_t>( kSlots));

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{
                   "SYST:CTYP? 1", "SYST:CTYP? 2", "SYST:CTYP? 3", "SYST:CTYP? 4",
                   "SYST:CTYP? 5", "SYST:CTYP? 6", "SYST:CTYP? 7", "SYST:CTYP? 8" }));
}

TEST( Keysight34980AWire, TheInternalDmmIsAskedAboutSeparatelyFromBeingEnabled)
{
    const auto bench = attached();

    bench->Wire->DmmInstalledReply = "1";
    bench->Wire->DmmEnabledReply   = "0";

    EXPECT_TRUE(  bench->Unit.internalDmmInstalled());
    EXPECT_FALSE( bench->Unit.internalDmmEnabled());

    bench->Unit.setInternalDmm( true);

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{
                   "INST:DMM:INST?",
                   "INST:DMM?",
                   "INST:DMM ON",
                   "SYST:ERR?" }));
}

//
// Fitted and enabled are different questions, and the difference is electrical:
// disabling the internal DMM removes its connections to the Analog Buses, so a
// disabled DMM is not a meter that refuses to read -- it is a meter that is no
// longer on the backplane the multiplexers reach it through.
//
TEST( Keysight34980AWire, SettingTheInternalDmmOffSendsOff)
{
    const auto bench = attached();

    bench->Unit.setInternalDmm( false);

    EXPECT_EQ( afterOpening( *bench->Wire),
               ( std::vector<std::string>{ "INST:DMM OFF", "SYST:ERR?" }));
}

//
// No *RST from this driver, ever, and on this box the reason is sharper than
// usual: a Factory Reset aborts any measurement and clears the scan list, and
// the internal DMM and eight modules are all downstream of it. The one command
// here that *causes* one is setInternalDmm, which says so in its own comment
// and is a bring-up operation rather than something a script calls.
//
TEST( Keysight34980AWire, NothingThisDriverSendsResetsTheChassis)
{
    const auto bench = attached();

    bench->Unit.close( ChannelAddress{ 1, 3 });
    bench->Unit.open( ChannelAddress{ 1, 3 });
    bench->Unit.openAll();
    static_cast<void>( bench->Unit.moduleIn( 1));

    for( const auto & command : bench->Wire->sent())
    {
        EXPECT_EQ( command.find( "*RST"), std::string::npos) << command;
        EXPECT_EQ( command.find( "*CLS"), std::string::npos) << command;
        EXPECT_EQ( command.find( "SYST:PRES"), std::string::npos) << command;
    }
}

//
// Closing the session opens no relay. A latching matrix keeps every crosspoint
// exactly where it was when the connection drops, which is hardware rather than
// something to paper over -- and it is why openAll() has to be called by
// somebody, which today nobody does.
//
TEST( Keysight34980AWire, ClosingTheSessionSendsNothing)
{
    const auto bench = attached();

    bench->Unit.close( ChannelAddress{ 1, 3 });

    const auto before = bench->Log->size();

    bench->Unit.closeSession();

    //
    // Read through the log rather than through Wire, which is dangling by this
    // line: closeSession() destroyed the session, and the session owned the
    // transport. See FakeChassis::record().
    //
    EXPECT_EQ( bench->Log->size(), before);

    //
    // And the driver really has let go -- the next command would open a new
    // session, which on a hal::Simulated address means it goes back to
    // simulating rather than reaching for a socket that is not there.
    //
    EXPECT_TRUE( bench->Unit.isSimulated());
}

// ===========================================================================
// The wrong box at the right address
// ===========================================================================

TEST( Keysight34980AWire, SomethingThatIsNotA34980AIsRefused)
{
    const auto bench = attached();

    bench->Wire->Identity = "Keysight Technologies,EDU34450A,MY60012345,01.00-01.00";

    EXPECT_THROW( static_cast<void>( bench->Unit.session()), hal::io::ScpiFault);
}

//
// Both badges accepted, and the vendor field deliberately not checked: this
// mainframe was an Agilent product and is a Keysight one, the same unit either
// way, and its own programmer's reference says "Agilent Technologies" on every
// example. Contrast hal::keysight_edu36311a, which refuses its sibling on
// purpose -- there the two boxes really are different instruments sharing a
// command set.
//
TEST( Keysight34980AWire, EitherBadgeIsAccepted)
{
    {
        const auto bench = attached();

        bench->Wire->Identity = "Agilent Technologies,34980A,MY44001234,2.43-2.42-1.19";

        EXPECT_NO_THROW( static_cast<void>( bench->Unit.session()));
    }

    {
        const auto bench = attached();

        bench->Wire->Identity = "Keysight Technologies,34980A,MY44001234,2.43-2.42-1.19";

        EXPECT_NO_THROW( static_cast<void>( bench->Unit.session()));
    }
}

TEST( Keysight34980AWire, AnIdentityThatIsNotShapedLikeAnIdnReplyIsRefused)
{
    const auto bench = attached();

    bench->Wire->Identity = "hello";

    EXPECT_THROW( static_cast<void>( bench->Unit.session()), hal::io::ScpiFault);
}

TEST( Keysight34980AWire, AFailedIdentityCheckIsRetriedRatherThanRemembered)
{
    const auto bench = attached();

    bench->Wire->Identity = "Agilent Technologies,34970A,MY44001234,1.0";

    EXPECT_THROW( static_cast<void>( bench->Unit.session()), hal::io::ScpiFault);

    bench->Wire->Identity = "Agilent Technologies,34980A,MY44001234,2.43-2.42-1.19";

    EXPECT_NO_THROW( static_cast<void>( bench->Unit.session()));
}

// ===========================================================================
// A refused command
// ===========================================================================
//
// The failure hal::io::ScpiSession::checked() exists for, on the device where
// missing it is worst: a refused ROUT:CLOS is a relay that did not move, and
// every reading afterwards is a reading of a different node -- plausible, in
// tolerance often enough, and attributed to the pin the script asked for.
//
// This is also where the missing half of the channel check lands today. Whether
// the module in slot 3 has a channel 010 is a module fact, no module is
// modelled yet, and the instrument answers -222 for one that does not exist. So
// a wrong channel arrives as this exception rather than as a compile error
// naming the card -- which is exactly the trade hal::hasChannel exists to
// remove, and getting it back is what the module models are for.
//

TEST( Keysight34980AWire, AChannelTheModuleDoesNotHaveArrivesAsAFaultNamingTheCommand)
{
    const auto bench = attached();

    // Open the session first, or the queued error is drained as the previous
    // user's -- see hal::io::ScpiSession::clearErrors.
    static_cast<void>( bench->Unit.session());

    bench->Wire->Errors = { "-222,\"Data out of range\"", "+0,\"No error\"" };

    try
    {
        bench->Unit.close( ChannelAddress{ 3, 500 });

        FAIL() << "expected an ScpiFault";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "ROUT:CLOS (@3500)");
        EXPECT_EQ( fault.error().Code, -222);
    }
}

//
// An RF multiplexer module does not respond to ROUT:OPEN at all -- the command
// generates an error. This driver sends it anyway rather than trying to know
// what is in the slot (see the header on open() for why), so the instrument's
// own refusal is what reaches the caller. closeExclusively() is the operation
// an RF bank actually wants.
//
TEST( Keysight34980AWire, OpeningAnRfMultiplexerChannelIsRefusedByTheInstrument)
{
    const auto bench = attached();

    static_cast<void>( bench->Unit.session());

    bench->Wire->Errors = { "-221,\"Settings conflict\"", "+0,\"No error\"" };

    try
    {
        bench->Unit.open( ChannelAddress{ 5, 11 });

        FAIL() << "expected an ScpiFault";
    }
    catch( const hal::io::ScpiFault & fault)
    {
        EXPECT_EQ( fault.command(), "ROUT:OPEN (@5011)");
    }
}

//
// And the slot check fires before any of that, which is the point of its being
// in the driver: nothing reaches the wire at all.
//
TEST( Keysight34980AWire, ASlotRefusalSendsNothing)
{
    Chassis chassis{ hal::Simulated{} };

    auto  fake = std::make_unique<FakeChassis>();
    auto *wire = fake.get();

    chassis.useTransport( std::move( fake));

    EXPECT_THROW( chassis.close( ChannelAddress{ 9, 3 }), NoSuchSlot);

    EXPECT_TRUE( wire->sent().empty());
}
