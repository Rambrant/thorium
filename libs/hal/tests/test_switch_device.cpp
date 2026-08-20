#include "hal/switch_device.hpp"

#include <gtest/gtest.h>

#include <type_traits>

//
// hal::SwitchDeviceId and the two facts each declared device carries. Like
// hal::InstrumentId's own tests, these name this rig's enumerators (Matrix2,
// Mux1, Mux2) because generic hal is handed the rig's table -- see
// rig/devices.inc, and hal/tests/test_instrument.cpp for the same arrangement
// one enum over.
//
namespace
{
    //
    // Both lookups are usable at compile time, which is the property that
    // makes them free at the call sites that matter: hal::to_string(
    // SwitchElementId) asks for a kind on a path it is already rendering, and
    // a future driver asking for its own address does it once at startup.
    //
    static_assert( hal::kindOf( hal::SwitchDeviceId::Matrix2) == hal::SwitchDeviceKind::Matrix);
    static_assert( hal::kindOf( hal::SwitchDeviceId::Mux1)    == hal::SwitchDeviceKind::Mux);
    static_assert( hal::kindOf( hal::SwitchDeviceId::Mux2)    == hal::SwitchDeviceKind::Mux);

    //
    // The three cards are modules in one chassis, so they share its GPIB
    // address and differ in secondary address -- the same shape DcP1..DcP4
    // have with their mainframe (see rig/devices.inc's own comment). Asserted
    // as a *relationship* rather than by repeating the three literals, since
    // the literals are provisional and this is the part that isn't.
    //
    static_assert( std::get<hal::Gpib>( hal::addressOf( hal::SwitchDeviceId::Matrix2)).primary ==
                   std::get<hal::Gpib>( hal::addressOf( hal::SwitchDeviceId::Mux1)).primary);
    static_assert( std::get<hal::Gpib>( hal::addressOf( hal::SwitchDeviceId::Matrix2)).secondary !=
                   std::get<hal::Gpib>( hal::addressOf( hal::SwitchDeviceId::Mux1)).secondary);
} // namespace

TEST( HalSwitchDevice, ToStringIsTheDeclaredEnumeratorSpelling)
{
    EXPECT_EQ( to_string( hal::SwitchDeviceId::Matrix2), "Matrix2");
    EXPECT_EQ( to_string( hal::SwitchDeviceId::Mux1),    "Mux1");
    EXPECT_EQ( to_string( hal::SwitchDeviceId::Mux2),    "Mux2");
}

TEST( HalSwitchDevice, KindComesFromTheDeclarationNotFromTheName)
{
    // Nothing parses "Mux1" to decide it is a mux -- kindOf() reads the kind
    // column of the row that declared it, which is why a card named anything
    // at all still reports the kind its rig said it was.
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Matrix2), hal::SwitchDeviceKind::Matrix);
    EXPECT_EQ( hal::kindOf( hal::SwitchDeviceId::Mux1),    hal::SwitchDeviceKind::Mux);
}

TEST( HalSwitchDevice, EveryDeviceHasAnAddressThePcCanReachItAt)
{
    // The gap hal/address.hpp named when it was written for instruments only:
    // closing a relay on real hardware is a write to a card, and a card needs
    // addressing exactly as an instrument does. Nothing reads these yet -- see
    // hal::addressOf's own comment -- so what is checked is that the rig can
    // state them and that they arrive intact.
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::Matrix2)), "Gpib 0::7::1");
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::Mux1)),    "Gpib 0::7::2");
    EXPECT_EQ( to_string( hal::addressOf( hal::SwitchDeviceId::Mux2)),    "Gpib 0::7::3");
}

//
// This rig declares three devices and no RF selector. That is a fact about
// rig/devices.inc rather than about hal, and it is asserted here for one
// reason: it is what makes an RfMux hal::SwitchElementId unrepresentable on
// this rig, which is why hal/tests/test_switch_fabric.cpp had to stop
// demonstrating kind-independence with a phantom RfMux1 device. If an RF
// selector is ever added to the table, that is the test to widen again.
//
TEST( HalSwitchDevice, TheRigDeclaresExactlyTheDevicesItsWiringUses)
{
    EXPECT_EQ( core::meta::values<hal::SwitchDeviceId>.size(), 3u);

    for( const auto device : core::meta::values<hal::SwitchDeviceId>)
    {
        EXPECT_NE( hal::kindOf( device), hal::SwitchDeviceKind::RfMux);
    }
}
