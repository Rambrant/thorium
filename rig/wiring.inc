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
//   the LF measurement bus is column 00 of Matrix1's group 0. Dmm1 and Osc1's
//   channel 2 sit on rows 0 and 2 (row 1 was Dmm2's, and is free); Mux1's
//   common sits on row 3. So
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
// TODO(bench): that HF path now needs a part it did not need before. The
// E1472A is a 50 ohm card and its bank commons are 50 ohm cable, and the scope
// on the end of it is no longer the Infiniium whose inputs could be switched
// to 50 ohm: a DSOX1202G's inputs are 1 MOhm and nothing else
// (:CHANnel<n>:IMPedance takes only ONEMeg -- see hal::keysight_dsox1202g::Coupling).
// So the HF bus is unterminated at the instrument end, and a config clock read
// through it will show reflections rather than the edge it went looking for.
// The fix is a 50 ohm feedthrough terminator at Osc1's channel 1 BNC, which is
// a part to buy rather than a line to write -- and it is a wiring fact, so it
// is recorded here rather than in the driver.
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
    // The two measuring instruments, one crosspoint each onto the LF
    // measurement bus -- group 0, column 00, one row apiece.
    //
    // Row 1 is deliberately left empty rather than closed up. It was Dmm2, the
    // second 6.5-digit meter this bench no longer has (see rig/instrument.inc
    // on the cull), and renumbering Osc1 down onto its crosspoint would be
    // rewriting a cable that nobody moved. A free row on the measurement bus is
    // where a second meter goes back.
    //
    WIRE_INSTRUMENT( Dmm1, CROSSPOINT( Matrix1, 0, 0, 0))
    WIRE_INSTRUMENT( Osc1, CROSSPOINT( Matrix1, 0, 2, 0))

    // DcP6/DcP7 are the EDU36311A's two 30 V / 1 A outputs, each with an
    // isolation relay in its output lead. On Spst1 rather than the matrix, and
    // that is the point of having a relay card at all: this carries whatever
    // the rail carries. A 1260-18 relay is rated 2 A; the matrix is a signal
    // path and its load has no business there.
    //
    // DcP5 -- the same box's 6 V / 5 A output -- has no entry, and that is
    // arithmetic rather than history: a 1260-18 relay is rated 2 A, so a 5 A
    // rail cannot pass through this card at all. It is cabled straight through
    // and its row in rig/instrument.inc says DirectOutput1, which is what
    // makes Connect( DcP5.dc()) a compile error rather than a relay carrying
    // two and a half times its rating (see
    // hal::keysight_edu36311a::SwitchableIsolation).
    //
    // Channels 4 and 5 are free where DcP3 and DcP4 used to be -- the two
    // relay-isolated channels of the N6701A mainframe this bench no longer has
    // (see rig/instrument.inc). Left as holes for the same reason Matrix1's
    // row 1 is: a relay nobody moved should not be renumbered.
    //
    // Both rows below are inside the 0-15 power block this card's comment in
    // rig/devices.inc sets aside, which now has six of its sixteen channels
    // spoken for -- AcP1's four and these two.
    WIRE_INSTRUMENT( DcP6, HOP( Spst1, 6))
    WIRE_INSTRUMENT( DcP7, HOP( Spst1, 7))

    // AcP1 is fixed-wired on four channels, not one -- phases A/B/C plus
    // the neutral/ground return, all closed and opened together (see
    // hal::keysight_ac6834b::Ac6834B's own comment on why the return is included, and
    // hal::InstrumentWiring::findAll() for how a script-facing Connect/
    // Disconnect gets all four back from one InstrumentId). Each is its
    // own single-hop entry here -- four independent Paths under one
    // InstrumentId, not one four-element Path -- see findAll()'s own
    // comment for why that distinction matters.
    //
    // Four relays on the power card, for the same reason DcP6/DcP7 are:
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
    // interface the script named (see hal::racal1260::Racal1260's routeTo, and the
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
    // (Which now wants a feedthrough terminator on it -- see this file's own
    // TODO(bench) above on what a 1 MOhm-only scope does to a 50 ohm path.)
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
// corroborates with the voltage it applies: DcP6 is brought up at 28 V against
// a point described as "28Vdc backup supply", DcP7 at 24 V against "24Vdc
// secondary backup supply", and AcP1 as a three-phase source against the four
// AcInput lines. Re-check when a real rig and DUT exist.
//
// The *currents* are a separate and sharper problem, and it is not a wiring
// one: these two rails were provisioned at 7 A and 4 A when an N6701A fed
// them, and an EDU36311A's 30 V outputs are 1 A each. See rig/instrument.inc's
// own TODO on what that means for a DUT that draws more.
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
// lead on the way there. DcP6 and DcP7 have both -- a relay and a landing pin.
//
// DcP5 appears nowhere below, deliberately: this rig declares it (see
// rig/instrument.inc) but nothing drives it and no adapter point corresponds
// to it -- a 6 V / 5 A output on a DUT whose lowest input rail is 24 V.
// hal::SourceWiring::findAll() returns empty for it rather than throwing,
// which is exactly that case. It is the spare DcP4 used to be.
//
// dut::BackupSupply_2 is the other half of that story, told from the pin's
// side rather than the supply's. This bench has two outputs that can reach a
// 24-28 V rail and the DUT has three such points, so one of them is now unfed
// -- and the framework will not let a SOURCE_POINT be unfed (dut_tests asserts
// hal::isSourceWired for every one of them), so it has become an ordinary
// POINT in dut/adapter.inc. It keeps its mux tap and stays measurable; what it
// no longer has is anything driving it. See that file's own comment.
//
SOURCE_WIRING
    WIRE_SOURCE( DcP6, A, 1, 5)   // dut::BackupSupply,   28Vdc (30 V / 1 A output)
    WIRE_SOURCE( DcP7, A, 1, 8)   // dut::BatterySupply,  24Vdc (30 V / 1 A output)
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
// Which is not the same as saying nothing is cabled. Three instruments on this
// rig are hard-wired to pins and every one of them is a source -- DcP6, DcP7
// and AcP1, recorded in SOURCE_WIRING above, cabled because a rail's load
// current has no business on signal relays. A tap row would say the opposite
// thing about a measuring instrument: that its input reaches a pin without
// passing through Matrix1 or Mux1 at all. This rig has a fabric precisely so
// that its two meters do not need that -- Dmm1 and Osc1 each have one
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
