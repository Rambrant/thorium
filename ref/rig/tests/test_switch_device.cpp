//
// Moved here from framework/hal/tests/ -- the sixth file to make that trip, after
// the five that went when the drivers moved out to instruments/ (see
// framework/hal/CMakeLists.txt's own list). The reason is the same one, arrived at
// from the other end: every assertion below names one of this bench's five
// cards, so what the file checks is rig/devices.inc as much as it is the
// mechanism that reads it.
//
// It sat in the generic library's test directory for as long as this repository
// held one rig, and the link-line check that is supposed to catch exactly that
// could not see it: hal_tests links plain hal so that a test reaching an
// instrument global or an Apply fails to link, and a SwitchDeviceId enumerator
// is neither. A second deployment found it immediately -- a bench with no
// switching hardware has no Matrix1 to ask about, and hal_tests stopped
// compiling.
//
// What is genuinely generic here is the *model* table -- that a 1260-45 is a
// matrix, that its channels are <group><row><column> -- and those facts are
// reachable through hal::SwitchDeviceModel without a declared device at all.
// Splitting the file along that line is a real improvement and a separate
// change; every assertion below currently goes through an id, which is why the
// whole file moved rather than half of it.
//
#include "hal/fabric/switch_device.hpp"

// hal::hop/crosspoint/bank live one header over, with hal::SwitchElementId --
// they are the checked way of building one, so this file reaches for them
// where it checks what a card's channel numbers are.
#include "hal/fabric/switch_fabric.hpp"

#include <gtest/gtest.h>

#include <type_traits>

//
// hal::SwitchDeviceId and the facts each declared device carries. Like
// hal::InstrumentId's own tests, these name this rig's enumerators (Matrix1,
// Mux1, Spst1, Spdt1, RfMux1) because generic hal is handed the rig's table --
// see rig/devices.inc, and hal/tests/driver/test_instrument.cpp for the same
// arrangement one enum over.
//
namespace
{
    //
    // Every lookup is usable at compile time, which is the property that
    // makes them free at the call sites that matter: hal::hop() checks a
    // channel while building a wiring table, hal::to_string( SwitchElementId)
    // asks for a kind on a path it is already rendering, and a future driver
    // asking for its own address does it once at startup.
    //
    static_assert( hal::kindOf( hal::SwitchDeviceId::Matrix1) == hal::SwitchDeviceKind::Matrix);
    static_assert( hal::kindOf( hal::SwitchDeviceId::Mux1)    == hal::SwitchDeviceKind::Mux);
    static_assert( hal::kindOf( hal::SwitchDeviceId::Spst1)   == hal::SwitchDeviceKind::Spst);
    static_assert( hal::kindOf( hal::SwitchDeviceId::Spdt1)   == hal::SwitchDeviceKind::Spdt);
    static_assert( hal::kindOf( hal::SwitchDeviceId::RfMux1)  == hal::SwitchDeviceKind::RfMux);

    //
    // The four Racal cards are modules in one chassis behind one smart
    // controller, so they share its GPIB address outright and differ in which
    // card of that chassis they are -- not in a GPIB secondary, which is a
    // different thing the bus itself understands (see hal::CardAddress). The
    // E1472A is the one card reached on its own address, and so the one with
    // a secondary and no card number.
    //
    // Asserted as *relationships* rather than by repeating the literals, since
    // the literals are provisional and this is the part that isn't.
    //
    static_assert( hal::addressOf( hal::SwitchDeviceId::Matrix1) ==
                   hal::addressOf( hal::SwitchDeviceId::Mux1));
    static_assert( hal::cardOf( hal::SwitchDeviceId::Matrix1) !=
                   hal::cardOf( hal::SwitchDeviceId::Mux1));

    static_assert( hal::addressOf( hal::SwitchDeviceId::RfMux1) !=
                   hal::addressOf( hal::SwitchDeviceId::Matrix1));
    static_assert( hal::cardOf( hal::SwitchDeviceId::RfMux1) == hal::NoCard);
} // namespace

TEST( HalSwitchDevice, ToStringIsTheDeclaredEnumeratorSpelling)
{
    EXPECT_EQ( to_string( hal::SwitchDeviceId::Matrix1), "Matrix1");
    EXPECT_EQ( to_string( hal::SwitchDeviceId::Mux1),    "Mux1");
    EXPECT_EQ( to_string( hal::SwitchDeviceId::RfMux1),  "RfMux1");
}

TEST( HalSwitchDevice, KindComesFromTheCardNotFromTheNameAndNotFromTheRig)
{
    // Nothing parses "Mux1" to decide it is a mux, and -- unlike before there
    // was a model column -- nothing in rig/devices.inc says so either. The
    // kind is a property of the card: an Agilent E1472A is an RF selector on
    // every rig that owns one, so a rig cannot name one and call it a matrix.
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Mux1),   hal::SwitchDeviceKind::Mux);
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::RfMux1), hal::SwitchDeviceKind::RfMux);

    EXPECT_EQ( hal::modelOf( hal::SwitchDeviceId::Mux1),   hal::SwitchDeviceModel::Racal1260_35);
    EXPECT_EQ( hal::modelOf( hal::SwitchDeviceId::RfMux1), hal::SwitchDeviceModel::AgilentE1472A);
}

TEST( HalSwitchDevice, ACardKnowsWhatItIsCalledInAPartsList)
{
    // Two spellings on purpose, and they answer different questions:
    // to_string gives the identifier this codebase chose, partOf gives what
    // the datasheet calls the card -- which is what belongs in a journal, and
    // what a reader chasing a channel number back to a manual needs.
    EXPECT_EQ( to_string( hal::SwitchDeviceModel::Racal1260_45), "Racal1260_45");
    EXPECT_EQ( hal::partOf( hal::SwitchDeviceId::Matrix1),       "Racal Instruments 1260-45A");
    EXPECT_EQ( hal::partOf( hal::SwitchDeviceId::RfMux1),        "Agilent E1472A");
}

TEST( HalSwitchDevice, EveryDeviceHasAnAddressThePcCanReachItAt)
{
    // The gap hal/driver/address.hpp named when it was written for instruments only:
    // closing a relay on real hardware is a write to a card, and a card needs
    // addressing exactly as an instrument does. Nothing reads these yet -- see
    // hal::addressOf's own comment -- so what is checked is that the rig can
    // state them and that they arrive intact.
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::Matrix1)), "Gpib 0::7");
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::Mux1)),    "Gpib 0::7");
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::RfMux1)),  "Gpib 0::9::3");

    EXPECT_EQ( hal::cardOf( hal::SwitchDeviceId::Matrix1), hal::Card( 1));
    EXPECT_EQ( hal::cardOf( hal::SwitchDeviceId::Mux1),    hal::Card( 2));
    EXPECT_EQ( hal::cardOf( hal::SwitchDeviceId::RfMux1),  hal::NoCard);
}

//
// ---------------------------------------------------------------------------
// Which channels a card has
// ---------------------------------------------------------------------------
// The fact a device was only a name and a kind for, and could therefore not
// state at all. Every card here numbers its channels the way its own manual
// does and no two schemes agree, so "is 300 a channel" is a question only the
// card can answer -- and a rig that answers it wrong does not get a route that
// fails, it gets a fabric element for a relay that does not exist: closed,
// opened, and routing nothing.
//
TEST( HalSwitchDevice, AChannelIsOneTheCardActuallyHas)
{
    // Flat cards: 152 relays on the 1260-18, 80 on the 1260-17, and one past
    // the end of each is not a channel.
    EXPECT_TRUE(  hal::hasChannel( hal::SwitchDeviceId::Spst1, 151));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::Spst1, 152));
    EXPECT_TRUE(  hal::hasChannel( hal::SwitchDeviceId::Spdt1, 79));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::Spdt1, 80));
}

TEST( HalSwitchDevice, ACompositeChannelNumberIsCheckedDigitByDigitNotJustAsARange)
{
    // A 1260-45 channel is <group><row><column>, and being inside 0000-3315
    // is not enough to be one of them: 316 is column 16 of a card whose
    // columns stop at 15, and 3400 is row 4 of a card with four rows.
    EXPECT_TRUE(  hal::hasChannel( hal::SwitchDeviceId::Matrix1, 3315));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::Matrix1, 316));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::Matrix1, 3400));

    // Same shape one card over: an E1472A is six banks of four, so 04 is not
    // "bank 0 channel 4", it is nothing.
    EXPECT_TRUE(  hal::hasChannel( hal::SwitchDeviceId::RfMux1, 53));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::RfMux1, 4));
    EXPECT_FALSE( hal::hasChannel( hal::SwitchDeviceId::RfMux1, 60));
}

TEST( HalSwitchDevice, ACardSaysHowItNumbersItsChannels)
{
    // Prose, and only ever read by a human -- it is what the failed check in
    // hal::hop() prints, so that a rejected channel says what the card does
    // have rather than only that this wasn't it.
    EXPECT_EQ( hal::channelsOf( hal::SwitchDeviceId::Spdt1), "0-79");
    EXPECT_NE( hal::channelsOf( hal::SwitchDeviceId::Matrix1).find( "<group><row><column>"),
               std::string_view::npos);
}

//
// The structured spellings, checked against the packed numbers they stand for
// -- see hal::crosspoint/hal::bank, and rig/wiring.inc, which is written in
// them. Compile-time, because that is where they are used and where their
// guard fires.
//
namespace
{
    static_assert( hal::crosspoint<hal::SwitchDeviceId::Matrix1, 2, 3, 12>() ==
                   hal::SwitchElementId{ hal::SwitchDeviceId::Matrix1, 2312 });

    static_assert( hal::bank<hal::SwitchDeviceId::RfMux1, 5, 3>() ==
                   hal::SwitchElementId{ hal::SwitchDeviceId::RfMux1, 53 });

    //
    // The one CROSSPOINT( Matrix1, 0, 3, 0) that every routed pin in
    // rig/wiring.inc goes through, written out here so the octal trap it
    // exists to avoid is visible: the packed form is 0300, and 0300 is 192.
    //
    static_assert( hal::crosspoint<hal::SwitchDeviceId::Matrix1, 0, 3, 0>().channel == 300);
    static_assert( 0300 != 300);
} // namespace

//
// This rig declares five devices, one of each model it owns. Asserted because
// it is the premise the wiring rests on -- and because the count is what
// catches a card added to rig/devices.inc and then never wired, or wired and
// never declared.
//
TEST( HalSwitchDevice, TheRigDeclaresExactlyTheDevicesItsWiringUses)
{
    EXPECT_EQ( core::meta::values<hal::SwitchDeviceId>.size(), 5u);

    // No two rows name the same card *and* the same place in the rack. Two
    // 1260-45s in one chassis is an ordinary thing to own; two rows claiming
    // to be card 1 of it is a copy-pasted line.
    for( const auto device : core::meta::values<hal::SwitchDeviceId>)
    {
        for( const auto other : core::meta::values<hal::SwitchDeviceId>)
        {
            if( device == other)
            {
                continue;
            }

            EXPECT_FALSE( hal::addressOf( device) == hal::addressOf( other) &&
                          hal::cardOf( device)    == hal::cardOf( other));
        }
    }
}
