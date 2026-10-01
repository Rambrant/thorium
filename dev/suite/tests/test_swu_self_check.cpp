#include "dev/suite/scripts.hpp"

//
// The SwitchUnit scripts, against a fake of this desk's rack.
//
// Not detached, unlike the other instruments' script tests, because detaching
// would not help: these scripts call the chassis driver directly
// (Swu1.close(), not Connect()), and only the verbs consult the bench.
//
// Not the driver's own simulation either, which knows the eight slots and
// nothing about what is in them -- so it accepts any well-formed channel, and
// would pass a refusal script that should fail and fail an RF script that
// should pass. Instead Swu1 is handed a FakeRack: a transport that answers the
// commands the driver sends the way this desk's rack would, using hal's own
// models of the two modules (hal::detail::keysight34932AHasChannel and
// keysight34941AHasChannel) for which channels exist. So these tests check the
// scripts against the same channel spaces the fabric will route with -- and a
// wrong number in either model fails here as well as on the box.
//
#include "suite/tests/verdict.hpp"

#include "hal/fabric/switch_device.hpp"
#include "hal/io/transport.hpp"
#include "hal/keysight_34980a.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"
#include "core/session/bench.hpp"
#include "core/quantities/quantity.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using core::quantities::Resistance;
using namespace core::literals;

namespace
{
    //
    // Slots 1-4 34932A, slot 5 34941A, the rest empty -- as SYST:CTYP? would
    // name them. A test changes Modules to describe a different rack.
    //
    class FakeRack final : public hal::io::ITransport
    {
        public:
            std::map<int, std::string> Modules{
                { 1, "34932A" }, { 2, "34932A" }, { 3, "34932A" }, { 4, "34932A" }, { 5, "34941A" } };

            //
            // A rack that accepts every well-formed channel, the way the
            // driver's own simulation does -- the failure the refusal script
            // exists to catch.
            //
            bool AcceptsAnything{ false };

            //
            // The internal DMM: fitted and enabled, as on this desk's unit.
            //
            bool DmmFitted{ true };
            bool DmmEnabled{ true };

            auto send( const std::string_view command) -> void override
            {
                const std::string text( command);

                if( text == "*IDN?")
                {
                    reply( "Agilent Technologies,34980A,MY53154781,2.43-2.42-1.19");
                }
                else if( text == "SYST:ERR?")
                {
                    reply( mPending.empty() ? "+0,\"No error\"" : mPending);
                    mPending.clear();
                }
                else if( text.starts_with( "SYST:CTYP? "))
                {
                    const int slot = std::stoi( text.substr( 11));
                    const auto found = Modules.find( slot);

                    reply( found == Modules.end() ? "Agilent Technologies,0,0,0"
                                                  : "Agilent Technologies," + found->second + ",MY0000000" + std::to_string( slot) + ",2.0");
                }
                else if( text.starts_with( "ROUT:CLOS? "))
                {
                    reply( mClosed.contains( channelsOf( text).front()) ? "1" : "0");
                }
                else if( text.starts_with( "ROUT:CLOS:EXCL "))
                {
                    const auto channels = channelsOf( text);

                    if( !allExist( channels)) return;

                    for( const auto & channel : channels)
                    {
                        std::erase_if( mClosed, [ & channel]( const auto & closed) { return closed.first == channel.first; });
                    }

                    for( const auto & channel : channels) closeOne( channel);
                }
                else if( text.starts_with( "ROUT:CLOS "))
                {
                    const auto channels = channelsOf( text);

                    if( !allExist( channels)) return;

                    for( const auto & channel : channels) closeOne( channel);
                }
                else if( text.starts_with( "ROUT:OPEN:ALL "))
                {
                    const auto which = text.substr( 14);

                    std::erase_if( mClosed, [ & which, this]( const auto & closed)
                    {
                        return !isRf( closed.first) && ( which == "ALL" || std::stoi( which) == closed.first);
                    });
                }
                else if( text.starts_with( "ROUT:OPEN "))
                {
                    const auto channels = channelsOf( text);

                    //
                    // What a 34941A does with ROUT:OPEN: refuses it, because a
                    // 1-of-4 bank has no open state.
                    //
                    if( std::ranges::any_of( channels, [ this]( const auto & channel) { return isRf( channel.first); }))
                    {
                        mPending = "-221,\"Settings conflict; RF module channels cannot be opened\"";
                        return;
                    }

                    for( const auto & channel : channels) mClosed.erase( channel);
                }
                else if( text.starts_with( "ROUT:MOD:WAIT? "))
                {
                    reply( "1");
                }
                else if( text == "INST:DMM:INST?")
                {
                    reply( DmmFitted ? "1" : "0");
                }
                else if( text == "INST:DMM?")
                {
                    reply( DmmEnabled ? "1" : "0");
                }
                else if( text.starts_with( "DIAG:REL:CYCL? "))
                {
                    reply( std::to_string( mCycles[ channelsOf( text).front()]));
                }
                else if( !text.empty() && text.back() == '?')
                {
                    reply( "0");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    throw hal::io::TransportTimeout( "nothing queued on the fake rack");
                }

                auto next = mReplies.front();

                mReplies.erase( mReplies.begin());

                return next;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake 34980A rack";
            }

            [[nodiscard]]
            auto closedCount() const -> std::size_t
            {
                return mClosed.size();
            }

        private:
            using Channel = std::pair<int, int>;   // slot, number

            auto reply( std::string text) -> void
            {
                mReplies.push_back( std::move( text));
            }

            //
            // "ROUT:CLOS (@1101,1416)" -> { {1,101}, {1,416} }
            //
            static auto channelsOf( const std::string & text) -> std::vector<Channel>
            {
                std::vector<Channel> channels;

                auto at = text.find( "(@");

                if( at == std::string::npos) return channels;

                auto list = text.substr( at + 2, text.find( ')', at) - at - 2);

                std::size_t start = 0;

                while( start < list.size())
                {
                    const auto comma = list.find( ',', start);
                    const auto item  = list.substr( start, comma == std::string::npos ? std::string::npos : comma - start);
                    const int  code  = std::stoi( item);

                    channels.emplace_back( code / 1000, code % 1000);

                    if( comma == std::string::npos) break;

                    start = comma + 1;
                }

                return channels;
            }

            [[nodiscard]]
            auto isRf( const int slot) const -> bool
            {
                const auto found = Modules.find( slot);

                return found != Modules.end() && found->second == "34941A";
            }

            [[nodiscard]]
            auto exists( const Channel & channel) const -> bool
            {
                if( AcceptsAnything) return true;

                const auto found = Modules.find( channel.first);

                if( found == Modules.end()) return false;

                const auto number = static_cast<std::uint16_t>( channel.second);

                if( found->second == "34932A") return hal::detail::keysight34932AHasChannel( number);
                if( found->second == "34941A") return hal::detail::keysight34941AHasChannel( number);

                return false;
            }

            //
            // A whole command refused if any channel in it does not exist, the
            // way the instrument validates a list before moving anything.
            //
            auto allExist( const std::vector<Channel> & channels) -> bool
            {
                if( std::ranges::all_of( channels, [ this]( const auto & channel) { return exists( channel); }))
                {
                    return true;
                }

                mPending = "-221,\"Settings conflict; channel not valid for module\"";

                return false;
            }

            //
            // Close one channel, counting a cycle if it was open -- and on an
            // RF bank, opening whichever channel of that bank was closed.
            //
            auto closeOne( const Channel & channel) -> void
            {
                if( isRf( channel.first))
                {
                    const int bank = channel.second / 100;

                    std::erase_if( mClosed, [ & channel, bank]( const auto & closed)
                    {
                        return closed.first == channel.first && closed.second / 100 == bank && closed != channel;
                    });
                }

                if( mClosed.insert( channel).second)
                {
                    ++mCycles[ channel];
                }
            }

            std::set<Channel>              mClosed;
            std::map<Channel, long>        mCycles;
            std::string                    mPending;
            std::vector<std::string>       mReplies;
    };

    struct SwitchUnitFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                auto fake = std::make_unique<FakeRack>();

                Rack = fake.get();

                Swu1.useTransport( std::move( fake));
            }

            void TearDown() override
            {
                Swu1.closeSession();
            }

            FakeRack * Rack{};
    };
} // namespace

//
// -- Inventory -------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, TheInventoryPassesOnThisDesksRack)
{
    EXPECT_TRUE( verdictOf( swuInventory));
}

TEST_F( SwitchUnitFixture, TheInventoryFailsWhenAMatrixSlotHoldsSomethingElse)
{
    Rack->Modules[ 3] = "34921A";

    EXPECT_FALSE( verdictOf( swuInventory));
}

TEST_F( SwitchUnitFixture, TheInventoryFailsWhenTheRfSlotIsEmpty)
{
    Rack->Modules.erase( 5);

    EXPECT_FALSE( verdictOf( swuInventory));
}

//
// -- Matrices ----------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, EveryMatrixPassesItsCycle)
{
    EXPECT_TRUE( verdictOf( swuMatrixSlot1));
    EXPECT_TRUE( verdictOf( swuMatrixSlot2));
    EXPECT_TRUE( verdictOf( swuMatrixSlot3));
    EXPECT_TRUE( verdictOf( swuMatrixSlot4));
}

//
// Each matrix script leaves the rack as it found it -- nothing closed -- so
// the next test starts from a known rack.
//
TEST_F( SwitchUnitFixture, AMatrixScriptLeavesNothingClosed)
{
    static_cast<void>( verdictOf( swuMatrixSlot2));

    EXPECT_EQ( Rack->closedCount(), 0u);
}

//
// -- Refusal -----------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, MissingChannelsAreRefused)
{
    EXPECT_TRUE( verdictOf( swuRefusesMissingChannels));
    EXPECT_EQ( Rack->closedCount(), 0u);
}

//
// The failure the refusal script exists for: a rack -- or a driver -- that
// lets a bad channel through without an error is a relay that did not move
// reported as one that did.
//
TEST_F( SwitchUnitFixture, AChannelThatIsSilentlyAcceptedFails)
{
    Rack->AcceptsAnything = true;

    EXPECT_FALSE( verdictOf( swuRefusesMissingChannels));
    EXPECT_EQ( Rack->closedCount(), 0u) << "a refusal script that got through must undo what it closed";
}

//
// -- RF multiplexer ----------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, EveryRfBankSelectsOneOfFourAndRefusesToOpen)
{
    EXPECT_TRUE( verdictOf( swuRfMultiplexer));
}

//
// Left on channel 01 of every bank -- the desk's idle, since an RF bank cannot
// be opened and stays where it is put.
//
TEST_F( SwitchUnitFixture, TheRfScriptLeavesEveryBankOnChannelOne)
{
    static_cast<void>( verdictOf( swuRfMultiplexer));

    for( unsigned bank = 1; bank <= 4; ++bank)
    {
        EXPECT_TRUE(  Swu1.isClosed( { 5, static_cast<int>( bank * 100 + 1) })) << "bank " << bank;
        EXPECT_FALSE( Swu1.isClosed( { 5, static_cast<int>( bank * 100 + 4) })) << "bank " << bank;
    }
}

//
// A slot 5 holding a matrix instead behaves like relays -- closing 104 does
// not open 101, and ROUT:OPEN works -- so both RF-specific checks fail.
//
TEST_F( SwitchUnitFixture, AnRfScriptOnAMatrixFails)
{
    Rack->Modules[ 5] = "34932A";

    EXPECT_FALSE( verdictOf( swuRfMultiplexer));
}

//
// -- Relay life ----------------------------------------------------------------------
//

TEST_F( SwitchUnitFixture, ADrivenCrosspointMovesItsLifeCount)
{
    EXPECT_TRUE( verdictOf( swuRelayCycles));
}

//
// -- The internal DMM -------------------------------------------------------------
//
// Dmm2's readings are injected by key -- "Dmm2.Voltage", "Dmm2.Resistance" --
// as every meter's are, so these never reach a MEASure; the rack fake answers
// Swu1's relay moves and its fitted/enabled questions around them.
//

namespace
{
    struct SwitchUnitDmmFixture : SwitchUnitFixture
    {
        protected:

            void TearDown() override
            {
                Measure.useLive();

                core::bench().attach();

                SwitchUnitFixture::TearDown();
            }
    };
} // namespace

TEST_F( SwitchUnitDmmFixture, TheMeterIsFittedAndEnabled)
{
    EXPECT_TRUE( verdictOf( swuDmmFitted));
}

TEST_F( SwitchUnitDmmFixture, AMeterThatIsDisabledFails)
{
    Rack->DmmEnabled = false;

    EXPECT_FALSE( verdictOf( swuDmmFitted));
}

TEST_F( SwitchUnitDmmFixture, AnOpenBusReadsAsOpen)
{
    Measure.inject( "Dmm2.Voltage",    0.004_V);
    Measure.inject( "Dmm2.Resistance", Resistance{ std::numeric_limits<double>::infinity() });

    EXPECT_TRUE( verdictOf( swuDmmOpenBus));
}

//
// Something on the bus that nothing put there -- a kilohm where there should
// be an open circuit -- fails, which is the short the path script exists to
// catch.
//
TEST_F( SwitchUnitDmmFixture, ABusWithSomethingOnItFails)
{
    Measure.inject( "Dmm2.Voltage",    0.004_V);
    Measure.inject( "Dmm2.Resistance", 1.0_kOhm);

    EXPECT_FALSE( verdictOf( swuDmmPathOntoNothing));
    EXPECT_EQ( Rack->closedCount(), 0u) << "the path script leaves the slot open even when it fails";
}

TEST_F( SwitchUnitDmmFixture, AClosedPathOntoNothingStaysOpen)
{
    Measure.inject( "Dmm2.Voltage",    0.002_V);
    Measure.inject( "Dmm2.Resistance", Resistance{ std::numeric_limits<double>::infinity() });

    EXPECT_TRUE( verdictOf( swuDmmPathOntoNothing));
}

//
// The wired chain, detached so the Apply to DcP7 reaches no supply: open,
// 5 V through the path, open again -- three readings of one key, in order.
//
TEST_F( SwitchUnitDmmFixture, TheSupplyArrivesThroughTheMatrix)
{
    core::bench().detach();

    Measure.inject( "Dmm2.Voltage", { 0.003_V, 5.002_V, 0.001_V });

    EXPECT_TRUE( verdictOf( swuDmmThroughTheMatrix));
    EXPECT_EQ( Rack->closedCount(), 0u);
}

//
// A path that did not close -- the meter still reads the open bus with the
// supply on -- fails on the middle reading.
//
TEST_F( SwitchUnitDmmFixture, ASupplyThatDoesNotArriveFails)
{
    core::bench().detach();

    Measure.inject( "Dmm2.Voltage", { 0.003_V, 0.003_V, 0.001_V });

    EXPECT_FALSE( verdictOf( swuDmmThroughTheMatrix));
}

