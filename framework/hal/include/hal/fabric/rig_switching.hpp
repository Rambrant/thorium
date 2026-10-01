#pragma once

#include "hal/fabric/switch_fabric.hpp"

//
// The rig's SwitchDriver: what makes hal::fabric move real relays.
//
// For an element -- a card and a channel -- it finds the card's row in
// devices.inc, that row's box, and the instrument on the same box that
// switches cards (hal::CardSwitchingInstrument), and tells it the card's
// address and the channel. So a 34932A declared as
//
//     SWITCH_DEVICE( Swu1, Keysight34932A, Matrix1, Usb( "MY53154781"), Card( 1))
//
// is closed by the instrument row on box Swu1 that switches cards -- the
// 34980A Chassis -- as ROUT:CLOS (@1<channel>), down the box's one session.
//
// Two cases it deliberately leaves as bookkeeping:
//
//   no switching instrument on the box   the bench's Racal rack today: its
//                                        cards are declared, and nothing drives
//                                        them yet. Exactly what every card was
//                                        before the fabric had a driver.
//
//   releasing an RF bank                 an RF multiplexer bank is a 1-of-N
//                                        selector with no open state, and a
//                                        34941A refuses ROUT:OPEN outright. So
//                                        the fabric releasing an RF element
//                                        sends nothing: the bank stays on the
//                                        channel it was put on, and a rig wires
//                                        its idle channel somewhere harmless.
//
// Defined in hal_rig, because finding the instrument means reflecting over the
// rig's instrument globals -- the same reason safing and preflight live there.
//
namespace hal
{
    [[nodiscard]]
    auto rigSwitchDriver() -> SwitchDriver &;
} // namespace hal
