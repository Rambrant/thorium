// ReSharper disable once CppMissingIncludeGuard

//
// This rig's fixed, concrete switching-device list: every matrix card, LF mux,
// relay card and RF selector its signal chain is built from, each named by the
// card it actually is, by the address the PC commands it at, and -- for the
// cards that share one -- by which card in that chassis it is. The sibling of
// rig/instrument.inc, and read the same way -- twice, once to generate
// hal::SwitchDeviceId's enumerators and once to carry the model/address/card of
// each (see hal/fabric/switch_device.hpp's own comment on why the second read cannot
// be replaced by reflection the way most of this codebase's re-#include spots
// were).
//
// These are deliberately not instruments, even though they are addressable
// hardware the PC talks to just as an instrument is. A switching device
// measures nothing and sources nothing: no script ever names one, only
// wiring.inc does, and hal::InstrumentId is what a *reading* is identified by.
// See hal/fabric/switch_device.hpp for that argument in full, and hal/README.md for
// the naming this rig settled on -- driver for the code, instrument for the
// hardware a script names, switch device for the plumbing it doesn't.
//
// The first column is the card, not the kind. What kind of thing a 1260-45 is
// (a matrix) and which channel numbers exist on it (0000-3315, written
// <group><row><column>) are datasheet facts this rig does not get a vote on,
// so they come from the model rather than being restated here -- which is what
// makes HOP( Spdt1, 300) a compile error on an 80-channel relay card. What
// this file states is what only a bench can know: which cards are in the rack,
// where they are, and what this rig calls them.
//
// -- The rack --------------------------------------------------------------
//
// Four Racal Instruments 1260-series switching cards in one VXI chassis,
// behind one Option 01T smart controller in the leftmost slot of the set. That
// controller is the only thing on the bus: the PC opens its single GPIB
// address and writes `CLOSE 3.0115`, where the leading 3 is the card's own
// address, set on SW1 on that card's PCB. So all four carry the same
// hal::Gpib and differ in Card( n) -- the same "one box, several endpoints"
// shape DcP1..DcP4 have with their mainframe, where the slot is likewise its
// own column and not a field on the address (see hal::CardAddress, and
// hal/driver/address.hpp on why a 1260 card number must not be written as a 488
// secondary).
//
// The Agilent E1472A is the one card that is not one of those: an ordinary
// register-based VXI servant, reached through the mainframe's command module,
// which does map its logical address onto a real GPIB secondary. Hence a
// secondary in its address and NoCard beside it.
//
// -- What each card is for -------------------------------------------------
//
// Matrix1  1260-45A, quad 4 x 16. The instrument crossbar, and the meeting
//          point of the whole LF signal chain: rows are endpoints, a column
//          is a bus. Group 0 carries this rig's measurement bus on column 00,
//          with Dmm1/Dmm2/Osc1 on rows 0-2 and Mux1's common on row 3, so a
//          reading is one crosspoint at each end of that column. Groups 1-3
//          and columns 01-15 are spare -- room for a second bus, which needs
//          a second mux common before it means anything.
//
// Mux1     1260-35, ordered as a 1 x 96 two-wire scanner. The DUT fan-in:
//          one channel per routed VPC pin, all sharing the one common that
//          lands on Matrix1's row 3. One common is exactly why this rig has
//          one measurement bus -- closing two of its channels at once does
//          not give two paths, it shorts two DUT pins together.
//
// Spst1    1260-18, 152 Form A relays at 2 A. The power path: the isolation
//          relay in each switched supply lead. Deliberately not the matrix --
//          it is load current that has to be kept off the signal relays (see
//          dut/adapter.inc, which makes the same point from the other side).
//          Channels 0-15 are that power block; 16 up are signal-level leads
//          that need isolating without needing a changeover (Ser1's three).
//
//          The 2 A is a rating this rig has now run into rather than merely
//          recorded, and it is what decides one row of rig/instrument.inc:
//          the EDU36311A's 6 V output is a 5 A output, so DcP5's lead cannot
//          pass through this card at all and is cabled straight through
//          instead. Its two 1 A siblings, DcP6 and DcP7, sit on channels 6
//          and 7. A switched 5 A rail on this rig would need a contactor
//          card this rack does not have.
//
// Spdt1    1260-17, 80 Form C relays. The console changeover: each of the
//          DUT's three console pins sits on the common of one relay, with the
//          bench's serial line on the normally-open contact and the parked
//          network on the normally-closed one, so an unconnected console is
//          three defined pins rather than three floating ones. Form C is the
//          whole reason this card is here rather than more of Spst1.
//
// RfMux1   E1472A, six 1 x 4 banks in 50 ohm. The HF path to the scope: bank
//          0's common is cabled to Osc1's channel 1, channel 00 is the 50 ohm
//          park the card powers up into, and the DUT's clock probe is on
//          channel 01. Banks 1-5 are spare.
//
// -- Where this rack is going: a 34980A ------------------------------------
//
// The five rows below are staying, and they are also on their way out. The
// decision taken 2026-09-02 is that this rig's switching becomes a single
// Keysight 34980A Multifunction Switch/Measure Unit -- which is what that box
// is for: it is the modern one-chassis replacement for exactly this
// arrangement, a VXI rack of LF cards plus a separate RF selector.
//
// The module set was named on 2026-09-02 and is not a guess:
//
//     slots 1-4   Keysight 34932A   dual 4 x 16 armature matrix,
//                                   +/-300 V, 1 A, 30 MHz  (four of them)
//     slot  5     Keysight 34941A   quad 1 x 4 RF multiplexer,
//                                   50 ohm, 3 GHz, SMA
//
// Both are declared in generic hal (hal::SwitchDeviceModel::Keysight34932A and
// ::Keysight34941A) with their real channel spaces, which is the half of the
// migration that needed no wiring decisions. Their numbering is worth knowing
// before reading any of it: a 34932A channel is <row><column> with the row one
// digit and the column two (315 is row 3 column 15) and it is ONE-based, unlike
// every Racal card in this table; the two matrices on one module share the row
// axis, Matrix 1 being rows 1-4 and Matrix 2 rows 5-8. A 34941A channel is
// <bank><channel>, bank 1-4 and channel 01-04, so 103 is bank 1 channel 03 --
// the same shape as the E1472A's and a decade apart in the packing.
//
// -- What that module set does and does not replace -------------------------
//
// It is not a five-for-five swap, and the differences are the whole reason the
// rows below have not moved yet. Two of this rack's five roles have a better
// home on the new hardware, one has an equal one, and two have none at all:
//
//     Matrix1  the instrument crossbar. A 34932A carries it with room to
//              spare and more bandwidth than the 1260-45A had (30 MHz against
//              25).
//
//     Mux1     the DUT fan-in. This is the one that gets *better* rather than
//              merely equivalent: a 1260-35's 96 channels all share one
//              common, which is exactly why this rig has one measurement bus
//              and why closing two mux channels shorts two DUT pins together
//              (see rig/wiring.inc's own comment on both). Matrix columns are
//              a real crossbar, so that constraint simply goes away.
//
//     RfMux1   the HF path. A 34941A is four banks against the E1472A's six
//              and 3 GHz against 1.3, both fine for one clock probe.
//
//     Spst1    the isolation relay in each supply lead -- and this one has
//              NOWHERE to go. A 1260-18 relay is rated 2 A; every channel of
//              a 34932A is rated 1 A. So the new rack cannot switch a supply
//              lead at all above 1 A, which includes AcP1's three phases (a
//              rig power-on applies 3 A) and leaves DcP6/DcP7 sitting exactly
//              on the limit with no margin. DcP5 was already unswitchable at
//              5 A and stays so, by a wider margin.
//
//     Spdt1    the console changeover, and it has no Form C to go to. What
//              that relay buys is a *parked* pin: the DUT's console line sits
//              on a relay common with the bench's serial line on the
//              normally-open contact and a parking network on the
//              normally-closed one, so an unconnected console is three
//              defined pins rather than three floating ones. A matrix
//              crosspoint is make-only.
//
// Neither gap is a reason to keep the Racal rack, and neither is settled here
// because neither is a code decision. What closes them, for the record:
//
//     the power path      a 34938A (20-channel 5 A Form A) would carry every
//                         rail this rig has, DcP5 included. A 34937A is the
//                         32-channel general-purpose alternative. Either is a
//                         module to buy, and without one the switched rails
//                         become hard-wired -- which is a change to
//                         rig/instrument.inc's type column, not to wiring.
//
//     the console park    two crosspoints per line rather than one relay: the
//                         DUT pin on a row, the bench serial line on one
//                         column and the parking network on another, closing
//                         exactly one of the two. A matrix can do this, and
//                         ROUT:CLOS:EXCL makes the changeover atomic per
//                         module (see instruments/keysight_34980a), so the
//                         park survives -- at the cost of dedicating a module
//                         to the console and of teaching hal::SwitchFabric an
//                         exclusive close, which it has no notion of today.
//
// -- Why the rows below have not moved --------------------------------------
//
// Because the two gaps above are wiring decisions and a row here is half of a
// wiring decision. Changing a row's model changes which channel numbers
// rig/wiring.inc may legally write (hal::hasChannel is a compile-time check),
// so devices.inc and wiring.inc migrate together or not at all -- and the
// wiring cannot be re-planned until it is known whether the rails and the
// console keep their relays.
//
// What has landed is everything that needed no such decision: the two module
// models above, and the mainframe itself as instruments/keysight_34980a/. That
// is deliberately a driver rather than a row here, because on a 34980A the
// *chassis* is the thing that talks -- "ROUT:CLOS (@3010)" is a mainframe
// command -- and a module only decides which channel numbers mean anything.
// Which is the same division this rack already has, one protocol older: four
// 1260 cards behind one Option 01T controller, with the card number riding
// inside the command.
//
// One structural question that driver's arrival makes concrete, and this table
// is where it will be answered: there is no row for a chassis, and no
// hal::SwitchDeviceId for one. Five module rows in one 34980A would be five
// rows repeating one address -- which is exactly the case rig/instrument.inc
// has already predicted an answer for, about its own second shared box: "a
// named constant above this table per chassis, not a slot field on the
// address." That is the line to write here when the first module row does.
//
// -- Provisional ------------------------------------------------------------
//
// TODO(bench): the addresses, the card numbers and every channel in wiring.inc
// are invented -- there is no physical bench yet. What is *not* invented any
// more is the hardware: these five models are real cards with real channel
// spaces, taken from their manuals, and hal::hasChannel holds this rig to
// them. Re-check the numbers against a real rack; the shapes will survive it.
//
// Note this also settles a question rig/instrument.inc raises about Ser1: if
// that RS232 port turns out to be a module in this same chassis rather than a
// port on the PC, its address becomes this chassis's Gpib( 0, 7) and it stops
// being this repo's hal::Serial example.
//
// One of each model, and no more, because that is what this rig's wiring uses.
// A second card of a model is another row with its own id and its own Card( n)
// -- Matrix1 rather than the old Matrix2 because there is one matrix and it is
// the first, not because the numbering means anything.
//
SWITCH_DEVICES
    SWITCH_DEVICE( Racal1260_45,  Matrix1, Gpib( 0, 7),    Card( 1))
    SWITCH_DEVICE( Racal1260_35,  Mux1,    Gpib( 0, 7),    Card( 2))
    SWITCH_DEVICE( Racal1260_18,  Spst1,   Gpib( 0, 7),    Card( 3))
    SWITCH_DEVICE( Racal1260_17,  Spdt1,   Gpib( 0, 7),    Card( 4))
    SWITCH_DEVICE( AgilentE1472A, RfMux1,  Gpib( 0, 9, 3), NoCard)
END_SWITCH_DEVICES
