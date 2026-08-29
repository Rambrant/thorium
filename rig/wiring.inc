// ReSharper disable once CppMissingIncludeGuard

//
// This rig's two static *path* facts (see hal/topology/wiring.hpp's own comment):
// which channel each instrument is hard-wired to, and which channel each VPC
// connector pin is hard-wired to. Composed together at measurement time --
// see core/verbs/measure.hpp -- rather than stored per (instrument, pin)
// combination.
//
// Two of the four blocks below, that is. The other two record connections the
// fabric is not part of at all -- SOURCE_WIRING, where each supply's output is
// cabled, and TAP_WIRING, where a measuring instrument's leads are bolted
// straight onto a pin. Each has its own comment further down; this rig's
// TAP_WIRING is empty, and that block says why.
//
// HOP( device, channel) names a card declared in rig/devices.inc, not a
// string: every device below has to be one this rig says it has, what kind of
// card it is comes from that declaration rather than being repeated here (see
// hal/fabric/switch_device.hpp for what the string-and-kind-per-hop form used to
// allow through), and the channel has to be one the card actually has -- a
// 1260-18 has 152 relays, a 1260-17 has 80, and writing 300 on either is now
// a compile error naming the card.
//
// CROSSPOINT( device, group, row, column) and BANK( device, bank, channel)
// are the same hop written as the card numbers it: a 1260-45 channel is
// <group><row><column> (2312 is group 2, row 3, column 12) and an E1472A
// channel is <bank><channel>. Used wherever a card has such a scheme, because
// the parts say what the digits mean -- and because the packed form of group
// 0 row 3 column 00 is 0300, which C++ reads as octal.
//
// -- The topology these numbers describe -----------------------------------
//
// A route is a composition: the instrument's own fixed path plus the pin's
// fixed path, closed together (see core::MeasureEngine). That only works on a
// rig built around *buses* -- somewhere the two halves meet -- and this rig
// has one LF bus and one HF bus:
//
//   the LF measurement bus is column 00 of Matrix1's group 0. Dmm1, Dmm2 and
//   Osc1's channel 3 sit on rows 0, 1 and 2; Mux1's common sits on row 3. So
//   an instrument's path is its own crosspoint onto that column, a pin's path
//   is its mux channel plus the crosspoint that puts the mux common on the
//   same column, and a reading closes both ends of one bus. The shared
//   crosspoint appearing in every routed pin's path is not duplication: it is
//   one relay that several routes legitimately need at once, which is exactly
//   what hal::SwitchFabric's use counting is for.
//
//   the HF bus is the common of RfMux1's bank 0, cabled to Osc1's channel 1.
//   A tree-switched 1 x 4 needs no instrument-side hop at all -- closing the
//   pin's channel is the whole path from pin to scope input.
//
// The second of those is where this rig's wiring model shows its edge, and it
// is worth stating rather than discovering: wiring is keyed by InstrumentId,
// and an instrument is one key, so Osc1 gets one fixed path and it is the LF
// one below. A reading taken through the HF bus therefore closes Osc1's LF
// crosspoint too -- superfluous, harmless (it puts an unused scope input on an
// idle bus), and not a route to anywhere it should not go. Per-channel
// instrument wiring is what would remove it, and nothing needs that yet.
//
INSTRUMENT_WIRING
    //
    // The three measuring instruments, one crosspoint each onto the LF
    // measurement bus -- group 0, column 00, one row apiece.
    //
    WIRE_INSTRUMENT( Dmm1, CROSSPOINT( Matrix1, 0, 0, 0))
    WIRE_INSTRUMENT( Dmm2, CROSSPOINT( Matrix1, 0, 1, 0))
    WIRE_INSTRUMENT( Osc1, CROSSPOINT( Matrix1, 0, 2, 0))

    // DcP1/DcP2 have no entry here at all -- they're hal::N6701ADirect,
    // wired straight through with no isolation relay, nothing for
    // Connect/Disconnect to close or open (see hal::N6701A's own comment
    // on DirectWiring). Only DcP3/DcP4 (hal::N6701ARelay) have a real
    // fixed channel.
    //
    // On Spst1 rather than the matrix, and that is the point of having a
    // relay card at all: this is the isolation relay in a supply's output
    // lead, carrying whatever the rail carries. A 1260-18 relay is rated 2 A;
    // the matrix is a signal path and its load has no business there.
    WIRE_INSTRUMENT( DcP3, HOP( Spst1, 4))
    WIRE_INSTRUMENT( DcP4, HOP( Spst1, 5))

    // AcP1 is fixed-wired on four channels, not one -- phases A/B/C plus
    // the neutral/ground return, all closed and opened together (see
    // hal::Ac6834B's own comment on why the return is included, and
    // hal::InstrumentWiring::findAll() for how a script-facing Connect/
    // Disconnect gets all four back from one InstrumentId). Each is its
    // own single-hop entry here -- four independent Paths under one
    // InstrumentId, not one four-element Path -- see findAll()'s own
    // comment for why that distinction matters.
    //
    // Four relays on the power card, for the same reason DcP3/DcP4 are:
    // three-phase mains is the last thing that should reach a matrix.
    WIRE_INSTRUMENT( AcP1, HOP( Spst1, 0)) // phase A
    WIRE_INSTRUMENT( AcP1, HOP( Spst1, 1)) // phase B
    WIRE_INSTRUMENT( AcP1, HOP( Spst1, 2)) // phase C
    WIRE_INSTRUMENT( AcP1, HOP( Spst1, 3)) // neutral/ground return

    // Ser1 is fixed-wired on three channels -- transmit, receive and the
    // signal-ground return -- closed and opened together exactly the way
    // AcP1's four are, and for the same reason (see hal::InstrumentWiring::
    // findAll). Unlike AcP1, though, these are only half of Ser1's route: it
    // is a *routed* instrument, so a Connect composes these with whichever DUT
    // interface the script named (see hal::Racal1260's routeTo, and the
    // CONNECTOR_WIRING rows for dut::Console below).
    //
    // Three independent relays, one per conductor, and independence is the
    // whole requirement: a console is three wires that must stay three wires.
    // The muxes cannot do this at all -- every channel of a 1260-35 shares one
    // common, so closing Tx, Rx and Gnd on one would short the three together
    // rather than route them -- which is what the earlier invented wiring
    // here actually described.
    //
    // These are the bench-side isolation relays; the DUT-side changeovers are
    // the Spdt1 rows under dut::Console below, and a console route closes one
    // of each per conductor.
    WIRE_INSTRUMENT( Ser1, HOP( Spst1, 16)) // transmit
    WIRE_INSTRUMENT( Ser1, HOP( Spst1, 17)) // receive
    WIRE_INSTRUMENT( Ser1, HOP( Spst1, 18)) // signal-ground return
END_INSTRUMENT_WIRING

//
// Every routed pin, two hops: its own channel on Mux1, then the crosspoint
// that puts Mux1's common on the LF measurement bus. The second hop is the
// same relay in every row here, deliberately -- there is one mux common and
// one bus, so that is one fact about this rig repeated where it applies,
// not ten facts that could disagree.
//
// Mux1 channels are allocated in pin order rather than to match pin numbers:
// a 1 x 96 scanner's channel numbering says nothing about the DUT, and
// pretending it does would be a coincidence waiting to be broken by the next
// adapter.
//
CONNECTOR_WIRING
    WIRE_CONNECTOR( A, 1, 3, HOP( Mux1, 0), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::Output5V
    WIRE_CONNECTOR( A, 1, 6, HOP( Mux1, 1), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::Output3V3
    WIRE_CONNECTOR( A, 1, 4, HOP( Mux1, 2), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::Vout

    //
    // The three cabled rails' taps. A pin listed in SOURCE_WIRING below is
    // reached by a cable, which says nothing against a high-impedance mux tap
    // onto the same pin -- see that table's own comment, and
    // dut/tests/test_wiring_coverage.cpp, which allows exactly this
    // combination and forbids the other one.
    //
    WIRE_CONNECTOR( A, 1, 5, HOP( Mux1, 3), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::BackupSupply
    WIRE_CONNECTOR( A, 1, 7, HOP( Mux1, 4), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::BackupSupply_2
    WIRE_CONNECTOR( A, 1, 8, HOP( Mux1, 5), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::BatterySupply

    //
    // The AC input's four lines, tapped the same way. Well inside a 1260-35
    // channel's 250 VAC rating -- the tap measures the line, it does not
    // carry it.
    //
    WIRE_CONNECTOR( A, 3, 1, HOP( Mux1, 6), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::AcInput::A
    WIRE_CONNECTOR( A, 3, 3, HOP( Mux1, 7), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::AcInput::B
    WIRE_CONNECTOR( A, 3, 5, HOP( Mux1, 8), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::AcInput::C
    WIRE_CONNECTOR( A, 3, 7, HOP( Mux1, 9), CROSSPOINT( Matrix1, 0, 3, 0))  // dut::AcInput::N

    //
    // The clock probe is the one pin on this adapter that is not an LF pin,
    // and it is the reason the rack has an E1472A: a config clock read through
    // an LF mux is read through the wrong thing, whatever the reading says.
    // Bank 0, channel 01 -- channel 00 of that bank is the 50 ohm park the
    // card powers up into, and bank 0's common is cabled to Osc1's channel 1.
    //
    // One hop, not two: a tree-switched 1 x 4 puts the pin on the scope input
    // by itself, so there is no instrument-side crosspoint in this path (see
    // this file's own comment above on what that costs).
    //
    WIRE_CONNECTOR( A, 1, 2, BANK( RfMux1, 0, 1))                           // dut::ClkProbe

    //
    // dut::Console's three lines, one Form C changeover each: common to the
    // DUT pin, normally-open to the bench's line, normally-closed to the
    // parked network. Closing the relay hands the pin to Ser1; leaving it
    // open leaves the DUT looking at a defined idle rather than at nothing,
    // which is what a changeover buys over the plain make/break the bench
    // side uses.
    //
    // Routed pins, not cabled ones, which is why they appear here and NOT in
    // SOURCE_WIRING below -- a console is switched onto the DUT for the
    // duration of a dialogue and off again afterwards.
    //
    // Note what these pins are not: they are not on the measurement bus.
    // Three conductors that must stay independent cannot share the one mux
    // common (see Ser1's own rows above), so there is no mux tap here and a
    // DMM cannot read a console line on this rig. That is the bench being
    // described accurately rather than a gap -- the earlier wiring gave these
    // pins mux channels and thereby described a rig that shorts its console
    // together the moment it connects it.
    //
    WIRE_CONNECTOR( A, 2, 1, HOP( Spdt1, 0))   // dut::Console::Tx
    WIRE_CONNECTOR( A, 2, 2, HOP( Spdt1, 1))   // dut::Console::Rx
    WIRE_CONNECTOR( A, 2, 5, HOP( Spdt1, 2))   // dut::Console::Gnd
END_CONNECTOR_WIRING

//
// Which VPC pin each fixed-wired source instrument's output is cabled onto
// -- the third static fact, and the one that is not a fabric path at all
// (see hal::SourceWiring's own comment in hal/topology/wiring.hpp). A pin listed here
// is reached by a cable, not by the mux: it gets no WIRE_CONNECTOR entry
// above, and its dut/adapter.inc counterpart is a SOURCE_POINT rather than a
// POINT. dut/tests/test_wiring_coverage.cpp enforces both directions.
//
// TODO(bench): assignments below are taken from what this repo already says,
// not read off a rig -- there is no physical bench yet. Each row matches the
// pairing suite/scripts/rig_power_on.cpp states in its own comments and
// corroborates with the voltage it applies: DcP1 and DcP2 are brought up at
// 28 V against points described as "28Vdc backup supply" and "28Vdc
// secondary backup supply", DcP3 at 24 V against "24Vdc secondary backup
// supply", and AcP1 as a three-phase source against the four AcInput lines.
// Re-check when a real rig and DUT exist.
//
// A landing pin is NOT thereby unreachable: all seven below also keep their
// WIRE_CONNECTOR rows above. A rail is cabled so the *fabric* never carries
// its load current, which says nothing against a high-impedance mux tap onto
// the same pin -- and reading the rail where it arrives at the DUT is a
// stricter check than the supply's own readback, which cannot see cable drop
// or a bad contact (see rig_power_on.cpp, which spells out that distinction).
// dut/tests/test_wiring_coverage.cpp allows the combination for that reason.
//
// Note the division of labour between this table and the Spst1 rows above:
// this says where a supply's output *lands*, those say which relay is in the
// lead on the way there. DcP3 has both -- a relay and a landing pin -- and
// DcP1/DcP2 have only a landing, being cabled straight through.
//
// DcP4 appears nowhere below, deliberately: this rig declares it (see
// rig/instrument.inc) but nothing drives it and no adapter point corresponds
// to it -- a spare module in the mainframe. hal::SourceWiring::findAll()
// returns empty for it rather than throwing, which is exactly that case.
//
SOURCE_WIRING
    WIRE_SOURCE( DcP1, A, 1, 5)   // dut::BackupSupply,   28Vdc
    WIRE_SOURCE( DcP2, A, 1, 7)   // dut::BackupSupply_2, 28Vdc
    WIRE_SOURCE( DcP3, A, 1, 8)   // dut::BatterySupply,  24Vdc
    // AcP1 lands on four pins, one row each -- the connector-side counterpart
    // of its four WIRE_INSTRUMENT rows above, and grouped on the adapter side
    // as the dut::AcInput bundle.
    WIRE_SOURCE( AcP1, A, 3, 1)   // dut::AcInput::A, phase A
    WIRE_SOURCE( AcP1, A, 3, 3)   // dut::AcInput::B, phase B
    WIRE_SOURCE( AcP1, A, 3, 5)   // dut::AcInput::C, phase C
    WIRE_SOURCE( AcP1, A, 3, 7)   // dut::AcInput::N, neutral/ground return
END_SOURCE_WIRING

//
// This rig's fourth table, and it is empty: nothing on this bench is cabled
// straight onto a DUT pin for the purpose of *measuring* it.
//
// Which is not the same as saying nothing is cabled. Five instruments on this
// rig are hard-wired to pins and every one of them is a source -- DcP1..DcP4
// and AcP1, recorded in SOURCE_WIRING above, cabled because a rail's load
// current has no business on signal relays. A tap row would say the opposite
// thing about a measuring instrument: that its input reaches a pin without
// passing through Matrix1 or Mux1 at all. This rig has a fabric precisely so
// that its three meters do not need that -- Dmm1, Dmm2 and Osc1 each have one
// crosspoint onto the measurement bus, and every pin they can reach is a
// WIRE_CONNECTOR row.
//
// So this block is empty for a structural reason and not for lack of
// attention, and the check in rig/tests/test_wiring_uniqueness.cpp is what
// keeps it that way: an instrument named here as well as in
// INSTRUMENT_WIRING is a build error, because core::MeasureEngine treats a
// tapped instrument as never routed and the matrix path would be dead data.
//
// What a row here would be for is the deployment that has no fabric at all --
// a supply, a meter and a scope on a desk with their leads on the DUT. See
// hal::TapWiring, which exists for that rig; nothing about it is a fallback
// for this one.
//
TAP_WIRING
END_TAP_WIRING
