// ReSharper disable once CppMissingIncludeGuard

//
// This rig's fixed, concrete instrument list -- included from two
// different places, each with INSTRUMENT defined for its own purpose:
// hal/topology/active_instruments.hpp declares each id below as an actual
// global, and hal/driver/instrument.hpp keeps only each id to generate
// hal::InstrumentId's enumerators. Both read this exact list, so an
// instrument added here can never desync from its own identity enumerator
// -- there is nothing left to desync from, since the enumerator comes from
// this file too now.
//
// hal/driver/instrument.hpp's own comment explains why THAT read can't be
// replaced by reflection the way the rest of this codebase's macro-
// redefinition-and-re-#include spots were: reflection needs the enum to
// already exist, and generating it is this read's entire job. Contrast
// hal/src/verbs/safing.cpp, which used to read this file a third time
// (INSTRUMENT redefined again, to call .safe() on each) -- it no longer
// needs this file at all, reflecting over hal::InstrumentTag-derived
// globals instead (see that struct's own comment in hal/driver/instrument.hpp),
// because InstrumentId already existed by the time IT ran.
//
// See hal/topology/active_instruments.hpp's own comment for the INSTRUMENTS/
// INSTRUMENT/END_INSTRUMENTS macros this expands through, and rig/wiring.inc
// for the sibling fact (which channel each of these instruments is wired to).
//
// The type column's namespace qualifier does double duty: it says which
// driver a row is to a reader, and it is also what generates that driver's
// #include (cmake/InstrumentDrivers.cmake). Adding an instrument is this one
// row and nothing else.
//
// DcP1..DcP4 are the N6701A mainframe's four module slots, each its own
// hal::keysight_n6701a::N6701A instance -- see that class's own comment in hal/keysight_n6701a.hpp
// for why the channel number is a constructor argument here, and why
// that's a different fact from which InstrumentWiring/wiring.inc entry
// they get.
//
// DcP1/DcP2 are wired straight through with no isolation relay at all;
// DcP3/DcP4 each have one -- see hal::keysight_n6701a::N6701A's own comment on
// DirectWiring/RelayIsolated for what that means for Connect/Disconnect.
// Matches this rig's real bench fact: some DC rails switchable, some not,
// same instrument type either way.
//
// -- DcP5..DcP7 are a second box, and a differently shaped one --------------
//
// Three more DC rails, and they are not three more N6701A modules: DcP5, DcP6
// and DcP7 are the three outputs of one Keysight EDU36311A triple-output bench
// supply (hal::keysight_edu36311a::EDU36311A), 90 W in one chassis as 6 V / 5 A,
// 30 V / 1 A and 30 V / 1 A. So this table now has two "one box, several
// endpoints" groups in it rather than one, and they are worth comparing,
// because they are the same shape written two different ways for a reason.
//
// Both repeat one address per row and put the endpoint somewhere else. The
// N6701A puts it in a constructor argument, because that mainframe has four
// *slots* and any module can be in any of them -- which slot a channel is in
// says nothing about what it can source. The EDU36311A puts it in the type,
// because its three outputs are built into the chassis: output 1 IS the
// 6 V / 5 A one. Handing that one a channel number and a rating separately
// would let this table write down a combination the instrument does not have,
// and nothing would object. Hence DirectOutput1/RelayOutput2/RelayOutput3 in
// the type column, each naming one physical output.
//
// Which of the two isolation aliases each row takes is arithmetic on this rig,
// and it is the one thing about these three rows a reader might otherwise
// think arbitrary. The isolation relays are on Spst1, a Racal 1260-18, whose
// relays are rated 2 A (see rig/devices.inc). Outputs 2 and 3 are 1 A outputs
// and fit inside that with room to spare, so DcP6 and DcP7 are RelayOutput*
// and each has a real fixed channel in rig/wiring.inc. Output 1 is a 5 A
// output and no relay in this rack will carry it, so DcP5 is DirectOutput1 --
// cabled straight through, nothing to Connect or Disconnect, exactly as
// DcP1/DcP2 are, and for a reason that can be checked rather than remembered.
//
// None of the three appears in SOURCE_WIRING, deliberately: this rig now
// declares three outputs that nothing drives and that no adapter point
// corresponds to, which is the case DcP4 was already the example of (see
// rig/wiring.inc's SOURCE_WIRING comment). Giving them landing pins would mean
// inventing DUT facts, and the DUT is dut/adapter.inc's business.
//
//
// Ser1 is the one instrument here that is neither a meter nor a source: an
// RS232 port switched onto the DUT through the same fabric everything else
// uses (see hal::racal1260::Racal1260, and dut::Console for the interface it talks to). It
// takes no constructor argument beyond its id and address -- one instance is
// one port, the same way one hal::keysight_l4411a::L4411A is one meter.
//
// The third column is how the *PC* reaches each instrument -- the control
// side, as against the signal side wiring.inc describes (see hal/driver/address.hpp
// for the distinction, and hal/topology/active_instruments.hpp for why it is a fixed
// column rather than one more constructor argument). Which bus kinds a given
// row may use is fixed by its driver, not by this file: an L4411A is an LXI
// box with no GPIB connector, so Gpib( ...) on one of the Dmm rows is a
// compile error rather than a run that dies on its first reading.
//
// Dmm1 and Dmm2 are two *different* models, and that is the one thing about
// this table a reader is most likely to skim past. They were two
// hal::keysight_l4411a::L4411A instances -- two wiring facts sharing one C++
// type, exactly as DcP1..DcP4 still are -- and Dmm1 is now the meter this
// bench actually has: a 5.5-digit hal::keysight_edu34450a::EDU34450A, against
// the L4411A's 6.5. A script measuring a rail cannot tell them apart, and a
// criterion tightened past 5.5 digits can; see that driver's own comment on
// why one class for both would have had to lie about one of them. Both models
// still refuse GPIB, so the paragraph above holds for either -- but the two
// rows no longer read the same, and the next section is why: Dmm1's driver has
// learned to open a real session, so its address column had to stop describing
// a rack that does not exist.
//
// DcP1..DcP4 all carry the same address, and that repetition is the point:
// they are four modules behind one mainframe interface, so there is one
// address and four slots, not four addresses.
//
// A second shared box has since appeared -- DcP5..DcP7 behind one EDU36311A --
// and the answer this paragraph used to predict for that case still stands and
// still is not needed yet: a named constant above this table per chassis, not a
// slot field on the address. Not needed because those three rows carry
// Simulated{} rather than an address (see below), so there is as yet nothing to
// name. The day one of them gets a hostname, all three get the same one, and
// that is the day to write the constant.
//
// TODO(bench): every address below is invented, exactly as every channel in
// wiring.inc is -- there is no physical bench yet, beyond the one instrument
// that has arrived (see Osc1, two sections down). They are internally
// consistent (one mainframe address for the four DC modules, an LXI hostname
// for the one meter still reached that way) and no more than that. Re-check
// against a real rack.
//
// -- Some rows say Simulated, and that is now a decision ---------------------
//
// This paragraph used to argue that no row here should be hal::Simulated{},
// because it "would read as a decision rather than a gap". That was right while
// every driver in the tree answered from its own simulation hooks and no driver
// read its address at all: an invented hostname was then a note to a reviewer
// and nothing more.
//
// hal::keysight_edu34450a::EDU34450A changed that. It is the first driver here
// that opens a real session -- SCPI over a socket to the address in this column
// (see hal/io/transport.hpp) -- so for that row this column stopped being a
// note and became an instruction. "Lan( "bench-dmm1")" now means *connect to
// bench-dmm1*, and every reading taken through Dmm1 on a bench where no such
// host exists fails with a name-resolution error, correctly and immediately.
//
// So Dmm1 is hal::Simulated{} -- the first row here to say it -- which has
// become the accurate statement rather
// than the evasive one: there is no meter, this driver can tell, and the honest
// thing for the table to say is that its readings come from the driver's own
// hooks. It is still a gap -- the TODO above still holds, and closing it for
// this row means a hostname *and* a box that answers to it -- but it is now a
// gap that has to be stated in the type rather than in a comment, because the
// code has started reading the column.
//
// The rule to apply as the other drivers grow transports, in one line: a row
// may name a real bus address when there is a real instrument at it, and must
// say Simulated{} until then. The rows below that still carry invented
// addresses do so because their drivers still ignore them entirely -- an
// L4411A, an N6701A, an Ac6834B and a Racal1260 answer from simulation hooks
// whatever this column says -- and they keep their addresses because those
// addresses are this table's worked examples of the three bus kinds a rig
// meets (see the paragraphs above and below on exactly that). The day one of
// those drivers opens a session, its row gets the same treatment Dmm1's just
// did.
//
// DcP5..DcP7 arrived under that rule rather than being converted to it: their
// driver opens a session from the day it landed, so all three say Simulated{}
// and each carries the TODO that says what replaces it. What is not yet known
// about them is which of two things that will be -- an EDU36311A has both a
// LAN interface and a USB device port, so the row becomes Lan( "<hostname>")
// or Usb( "<serial>") depending on how the box is cabled, and both compile.
// Unlike Osc1's gap, this one is not one line from closing: there is no supply
// on the bench yet at all.
//
// -- And now Osc1 says it too, for the same reason --------------------------
//
// Osc1 is hal::Simulated{} as well, and it is now the same statement Dmm1's row
// makes rather than a different one. hal::keysight_dsox1202g::DSOX1202G opens a
// session (see instruments/keysight_dsox1202g/src/keysight_dsox1202g.cpp for
// everything it sends), so this column has become an instruction for this row
// too -- and the rule above applies unchanged: a row may name a real bus
// address when there is a real instrument at it, and must say Simulated{} until
// then.
//
// The gap here is narrower than Dmm1's and is worth stating precisely, because
// it is one line from closing. There *is* a scope on the bench. What is missing
// is its USB serial number, which is what a hal::Usb address is written with
// (see hal/driver/address.hpp on why a serial rather than a bus/device number),
// and inventing one would be worse than inventing a hostname for a rack that
// does not exist: a plausible serial is a thing a later reader pastes into a
// VISA resource string and then debugs for an hour.
//
// So: Simulated{}, and the TODO on the row says exactly what replaces it --
// Usb( "<serial>"), read off the instrument's own *IDN? or its Utility > I/O
// screen. Nothing else about this row changes when it does; the driver already
// routes hal::Usb through VISA (hal/io/visa_transport.hpp).
//
// Note what that row can never say again, and that this is now enforced rather
// than reviewed: Lan( ... ). The DSOX1202G has a USB device port and no network
// connector at all -- Keysight's own programmer's guide, "There is no LAN
// interface (only USB is supported)" -- so its constructor accepts hal::Usb and
// hal::Simulated and nothing else, and the Lan( "bench-osc1") this row used to
// carry does not compile against it. The scope this rig had before accepted
// GPIB, LAN and USB, which is exactly why the old row could say something this
// bench cannot do.
//
// What is deliberately NOT lost by this: an attached run still reaches this
// driver, and that is what the acceptance suite checks (see
// acceptance/test_acceptance.cpp's AcceptanceBench group). hal::Simulated
// removes the socket, not the driver -- Measure still calls rawMeasure, the
// journal still records the reading, and the run still fails its criteria
// because a simulated meter reads zero volts. What it removes is a CI machine
// trying to open a TCP connection to a hostname that describes a rack nobody
// has built.
//
// Ser1's row is Serial on purpose, and for a reason that is not a claim about
// the bench: it is this table's one worked example of a PC-attached serial
// device, kept deliberately so that the three bus kinds a rig is likely to
// meet -- a GPIB box, an LXI hostname, a device path -- are all exercised by
// something real here rather than only described in hal/driver/address.hpp.
//
// The underlying bench fact is still open, and the driver says so: it accepts
// Serial *or* Gpib, because a switched RS232 port can be either a port on
// this PC with its conductors cabled into the fabric or a serial module in the
// switching chassis commanded over the chassis bus (see hal::racal1260::Racal1260's own
// constructor comment). If it turns out to be the latter, this row becomes the
// chassis's bus address and that constraint narrows to Gpib -- and this table
// loses its Serial example, which is a reason to write one somewhere else, not
// a reason to keep this row wrong.
//
INSTRUMENTS
    INSTRUMENT( keysight_edu34450a::EDU34450A, Dmm1, Simulated{})            // TODO(bench): Lan( "bench-dmm1") once a meter answers there -- this driver connects
    INSTRUMENT( keysight_l4411a::L4411A,       Dmm2, Lan( "bench-dmm2"))
    INSTRUMENT( keysight_dsox1202g::DSOX1202G, Osc1, Simulated{})            // TODO(bench): Usb( "<serial>") off the scope's own *IDN? -- this driver connects
    INSTRUMENT( keysight_n6701a::Direct,       DcP1, Gpib( 0, 14), 1)
    INSTRUMENT( keysight_n6701a::Direct,       DcP2, Gpib( 0, 14), 2)
    INSTRUMENT( keysight_n6701a::Relay,        DcP3, Gpib( 0, 14), 3)
    INSTRUMENT( keysight_n6701a::Relay,        DcP4, Gpib( 0, 14), 4)
    INSTRUMENT( keysight_edu36311a::DirectOutput1, DcP5, Simulated{})        // TODO(bench): Lan( "<host>") or Usb( "<serial>") once a supply answers -- this driver connects
    INSTRUMENT( keysight_edu36311a::RelayOutput2,  DcP6, Simulated{})        // 30 V / 1 A, isolation relay on Spst1/6
    INSTRUMENT( keysight_edu36311a::RelayOutput3,  DcP7, Simulated{})        // 30 V / 1 A, isolation relay on Spst1/7
    INSTRUMENT( keysight_ac6834b::Ac6834B,     AcP1, Gpib( 0, 5))
    INSTRUMENT( racal1260::Racal1260,          Ser1, Serial( "/dev/ttyUSB0"))
END_INSTRUMENTS
