#include "hal/driver/box_connection.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/meta.hpp"
#include "hal/topology/boxes.hpp"

//
// hal::BoxConnection, without a driver: two connections on one id are two
// faces of that id's box, and these tests watch what they share.
//
// Every address is a .invalid hostname, and each test's own: a fake transport
// is always installed before anything would be opened, and a name nothing
// resolves turns a test that got that wrong into a transport error rather
// than a reach for the network. Distinct per test because the registry is
// process-wide.
//
namespace
{
    [[nodiscard]]
    auto anyId() -> hal::InstrumentId
    {
        return core::meta::values<hal::InstrumentId>[ 0];
    }

    //
    // Answers every query, records every command, and counts nothing else --
    // what is under test is who talks through it, not what it says.
    //
    class Wire final : public hal::io::ITransport
    {
        public:
            explicit Wire( std::shared_ptr<std::vector<std::string>> sent) : mSent( std::move( sent)) {}

            auto send( const std::string_view command) -> void override
            {
                mSent->emplace_back( command);

                if( !command.empty() && command.back() == '?')
                {
                    mReplies.emplace_back( "+0,\"No error\"");
                }
            }

            auto receive() -> std::string override
            {
                if( mReplies.empty())
                {
                    throw hal::io::TransportTimeout( "nothing queued");
                }

                auto reply = mReplies.front();

                mReplies.erase( mReplies.begin());

                return reply;
            }

            [[nodiscard]]
            auto description() const -> std::string override
            {
                return "fake wire";
            }

        private:
            std::shared_ptr<std::vector<std::string>> mSent;
            std::vector<std::string>                  mReplies;
    };

    //
    // Two faces of one box at one address, the first with a fake installed.
    //
    struct TwoFaces
    {
        explicit TwoFaces( const std::string_view host) :
            First( anyId(), hal::Lan( host)),
            Second( anyId(), hal::Lan( host))
        {
            First.useTransport( std::make_unique<Wire>( Sent));
        }

        std::shared_ptr<std::vector<std::string>> Sent = std::make_shared<std::vector<std::string>>();
        hal::BoxConnection                        First;
        hal::BoxConnection                        Second;
    };

    //
    // A prepare step that says it ran.
    //
    struct Counted
    {
        int * Count;

        auto operator()( hal::io::ScpiSession & session) const -> void
        {
            ++*Count;

            session.write( "PREPARED");
        }
    };
} // namespace

TEST( BoxConnection, AFaceIsOnTheBoxItsIdNames)
{
    const hal::BoxConnection face{ anyId(), hal::Simulated{} };

    EXPECT_EQ( face.box(), hal::boxOf( anyId()));
}

//
// -- Shared per box and address ----------------------------------------------
//

TEST( BoxConnection, FacesOfOneBoxAtOneAddressShareOneSession)
{
    TwoFaces box{ "share.invalid" };

    EXPECT_TRUE( box.Second.hasSession());
    EXPECT_FALSE( box.Second.isSimulated());

    int prepared = 0;

    auto & first  = box.First.session( "family", Counted{ &prepared });
    auto & second = box.Second.session( "family", Counted{ &prepared });

    EXPECT_EQ( &first, &second);
}

TEST( BoxConnection, OneFamilyPreparesOnceHoweverManyFacesSpeak)
{
    TwoFaces box{ "prepare-once.invalid" };

    int prepared = 0;

    static_cast<void>( box.First.session( "family", Counted{ &prepared }));
    static_cast<void>( box.Second.session( "family", Counted{ &prepared }));
    static_cast<void>( box.First.session( "family", Counted{ &prepared }));

    EXPECT_EQ( prepared, 1);
}

//
// A mainframe's switching and its meter: two drivers on one session, each with
// its own once-per-session exchange.
//
TEST( BoxConnection, EachFamilyPreparesTheSharedSessionForItself)
{
    TwoFaces box{ "two-families.invalid" };

    int switching = 0;
    int meter     = 0;

    static_cast<void>( box.First.session( "switching", Counted{ &switching }));
    static_cast<void>( box.Second.session( "meter", Counted{ &meter }));
    static_cast<void>( box.Second.session( "meter", Counted{ &meter }));

    EXPECT_EQ( switching, 1);
    EXPECT_EQ( meter, 1);
}

TEST( BoxConnection, AFailedPrepareIsAskedAgainRatherThanRemembered)
{
    TwoFaces box{ "prepare-fails.invalid" };

    int attempts = 0;

    const auto failing = [ & ]( hal::io::ScpiSession &) { ++attempts; throw std::runtime_error( "not the model"); };

    EXPECT_THROW( static_cast<void>( box.First.session( "family", failing)), std::runtime_error);
    EXPECT_THROW( static_cast<void>( box.Second.session( "family", failing)), std::runtime_error);

    EXPECT_EQ( attempts, 2);
}

TEST( BoxConnection, DifferentAddressesAreDifferentSessions)
{
    TwoFaces box{ "first-address.invalid" };

    hal::BoxConnection elsewhere{ anyId(), hal::Lan( "second-address.invalid") };

    EXPECT_FALSE( elsewhere.hasSession());
    EXPECT_NE( hal::detail::existingBoxLinkAt( hal::boxOf( anyId()), hal::Lan( "first-address.invalid")), nullptr);
    EXPECT_EQ( hal::detail::existingBoxLinkAt( hal::boxOf( anyId()), hal::Lan( "second-address.invalid")), nullptr);
}

//
// A Simulated address names no unit, so it shares nothing: a fake handed to
// one simulated face leaves another simulating.
//
TEST( BoxConnection, SimulatedFacesShareNothing)
{
    hal::BoxConnection first{ anyId(), hal::Simulated{} };
    hal::BoxConnection second{ anyId(), hal::Simulated{} };

    first.useTransport( std::make_unique<Wire>( std::make_shared<std::vector<std::string>>()));

    EXPECT_FALSE( first.isSimulated());
    EXPECT_TRUE(  second.isSimulated());
}

//
// -- Closed per box, never opened by safing ------------------------------------
//

TEST( BoxConnection, ClosingFromAFaceThatNeverSpokeClosesTheBox)
{
    TwoFaces box{ "close.invalid" };

    hal::BoxConnection silent{ anyId(), hal::Lan( "close.invalid") };

    silent.close();

    EXPECT_FALSE( box.First.hasSession());
    EXPECT_FALSE( box.Second.hasSession());
}

TEST( BoxConnection, SafingUsesASessionASiblingOpened)
{
    TwoFaces box{ "safe.invalid" };

    hal::BoxConnection silent{ anyId(), hal::Lan( "safe.invalid") };

    auto * const open = silent.openSession();

    ASSERT_NE( open, nullptr);

    open->write( "OFF");

    EXPECT_EQ( box.Sent->back(), "OFF");
}

//
// And never opens one: a box nobody spoke to has nothing for safing to use,
// and asking must not create anything -- a host nothing answers to would
// throw out of the safing pass if it did.
//
TEST( BoxConnection, SafingNeverOpensASession)
{
    hal::BoxConnection face{ anyId(), hal::Lan( "never-opened.invalid") };

    EXPECT_EQ( face.openSession(), nullptr);
    EXPECT_EQ( hal::detail::existingBoxLinkAt( hal::boxOf( anyId()), hal::Lan( "never-opened.invalid")), nullptr);
}

//
// -- Lifetime and moves ---------------------------------------------------------
//

TEST( BoxConnection, ASessionLivesOnlyAsLongAsAFaceHoldsIt)
{
    std::weak_ptr<hal::detail::BoxLink> remembered;

    {
        TwoFaces box{ "lifetime.invalid" };

        remembered = hal::detail::existingBoxLinkAt( hal::boxOf( anyId()), hal::Lan( "lifetime.invalid"));

        EXPECT_FALSE( remembered.expired());
    }

    EXPECT_TRUE( remembered.expired());
}

TEST( BoxConnection, MovingOneFaceLeavesItsSiblingOnTheOldSession)
{
    TwoFaces box{ "old-address.invalid" };

    box.Second.useAddress( hal::Lan( "new-address.invalid"));

    EXPECT_TRUE(  box.First.hasSession());
    EXPECT_FALSE( box.Second.hasSession());
    EXPECT_EQ( box.Second.address(), hal::Address{ hal::Lan( "new-address.invalid") });
}

TEST( BoxConnection, AMoveHandsTheHoldOver)
{
    TwoFaces box{ "move.invalid" };

    hal::BoxConnection moved{ std::move( box.First) };

    EXPECT_TRUE( moved.hasSession());
    EXPECT_TRUE( box.Second.hasSession());
}
