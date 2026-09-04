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
// -- What this bench actually has, as of 2026-09-02 -------------------------
//
// This table was cut down to the hardware that exists. What left: a second
// 6.5-digit DMM (Dmm2, a Keysight L4411A) and the four channels of an N6701A
// modular DC mainframe (DcP1..DcP4). Neither box is on the bench, neither
// driver had grown a transport, and a rig table naming instruments nobody has
// is a table that cannot be checked against anything. Their driver packages
// were deleted with them -- `instruments/keysight_l4411a` and
// `instruments/keysight_n6701a` -- along with `instruments/keysight_dso8064a`,
// the Infiniium this rig was written against before the scope that actually
// turned up.
//
// What is left -- with the waveform generator that arrived on 2026-09-04 --
// is seven rows and five boxes:
//
//     Dmm1   the 5.5-digit Keysight EDU34450A. The bench's only meter now,
//            which is a real narrowing rather than a tidy-up: this table used
//            to carry two DMMs of different resolutions on purpose (see the
//            paragraph on that below, kept because the argument still holds
//            for whoever adds a second meter back).
//
//     Osc1   the two-channel Keysight DSOX1202G.
//
//     DcP5   the 6 V / 5 A output of a Keysight EDU36311A triple supply.
//     DcP6   its first 30 V / 1 A output.
//     DcP7   its second 30 V / 1 A output.
//
//     AcP1   a Keysight Ac6834B three-phase AC source.
//
//     Wfg1   a Keysight (Agilent) 33522B waveform generator -- both of its
//            channels, see below.
//
//     Ser1   an RS232 port that talks to the DUT's debug console.
//
// Two of those are kept deliberately without hardware behind them, and it is
// worth saying which and why rather than leaving a later reader to wonder why
// the cull stopped where it did. AcP1 stays because the Transient group is
// built on it -- acDropoutScript verifies that the DUT's 5 V rail rides a loss
// of the primary AC input, and transient_bracket.cpp is that group's
// SETUP/TEARDOWN -- and deleting the source would delete the test. Ser1 stays
// because the DUT's console is reached through it and the Console group needs
// it. Both answer from their own simulation hooks, as every driver here did
// before hal/io/ existed.
//
// The switching hardware is not in this table at all and never was: see
// rig/devices.inc, which also records this rack's migration onto a Keysight
// 34980A. That mainframe has a driver (instruments/keysight_34980a) and no row
// anywhere, because it is a switching device rather than an instrument -- that
// package's own README has the argument.
//
// -- DcP5..DcP7 are one box, and a differently shaped one -------------------
//
// DcP5, DcP6 and DcP7 are the three outputs of one Keysight EDU36311A
// triple-output bench supply (hal::keysight_edu36311a::EDU36311A), 90 W in one
// chassis as 6 V / 5 A, 30 V / 1 A and 30 V / 1 A. So this table has a "one
// box, several endpoints" group in it, the shape DcP1..DcP4 used to have
// behind their mainframe -- and the two were worth comparing, which is why the
// comparison is kept here now that only one of them is left.
//
// Both repeat one address per row and put the endpoint somewhere else. An
// EDU36311A mainframe put it in a constructor argument, because that chassis has
// four *slots* and any module can be in any of them -- which slot a channel is
// in says nothing about what it can source. The EDU36311A puts it in the type,
// because its three outputs are built into the chassis: output 1 IS the
// 6 V / 5 A one. Handing that one a channel number and a rating separately
// would let this table write down a combination the instrument does not have,
// and nothing would object. Hence DirectOutput1/RelayOutput2/RelayOutput3 in
// the type column, each naming one physical output.
//
// Which of the two isolation aliases each row takes is arithmetic on this rig,
// and it is the one thing about these three rows a reader might otherwise
// think arbitrary. The isolation relays live on Spst1, a Racal 1260-18, whose
// relays are rated 2 A (see rig/devices.inc). Outputs 2 and 3 are 1 A outputs
// and fit inside that with room to spare, so DcP6 and DcP7 are RelayOutput*
// and each has a real fixed channel in rig/wiring.inc. Output 1 is a 5 A
// output and no relay in this rack will carry it, so DcP5 is DirectOutput1 --
// cabled straight through, nothing to Connect or Disconnect.
//
// That distinction is not cosmetic and one of this suite's tests depends on
// it: bulkCapacitanceScript takes the battery rail down and then *disconnects*
// it, so that the supply's own output impedance is off the node before the
// meter charges it. Only a RelayOutput can do that -- Disconnect( DcP5.dc())
// does not compile (see hal::keysight_edu36311a::SwitchableIsolation) -- which
// is why the rail that test measures is on DcP7 rather than DcP5.
//
// DcP5 is a spare, and the honest kind: nothing on this DUT wants 6 V, so it
// appears in no SOURCE_WIRING row and drives nothing. That is the case DcP4
// used to be the example of, and hal::SourceWiring::findAll() returns empty for
// it rather than throwing.
//
// -- What this supply cannot deliver, which the DUT wanted ------------------
//
// TODO(bench): the two rails this bench can still drive are provisioned well
// below what the DUT's own points ask for, and the arithmetic is not close.
// dut::BackupSupply and dut::BatterySupply were fed at 28 V / 7 A and
// 24 V / 4 A while an N6701A was on the bench. An EDU36311A's 30 V outputs are
// **1 A** each and the whole chassis is 90 W, so suite/scripts/rig_power_on.cpp
// now brings them up current-limited at 1 A -- the output's maximum, and the
// most it can honestly be asked for (a limit past the badge is refused before
// anything reaches the wire, see hal::keysight_edu36311a::RatingExceeded).
//
// What that means on a real bench: if the DUT draws more than 1 A on either
// rail, the supply goes into constant current, the rail sags below its
// setpoint, and rigPowerOn's own readback check fails the run before the first
// test -- which is the right failure and a clear one. It is not something this
// code can fix. Powering that DUT as specified needs a supply this bench does
// not have.
//
// -- Wfg1 is two outputs and one row, which is the other answer ------------
//
// A 33522B has two independent front-panel channels, so it is a "one box,
// several endpoints" instrument exactly as DcP5..DcP7 are -- and it gets ONE
// row where they get three. The two shapes are both right, and which one an
// instrument wants is decided by the same question each time: is an endpoint
// separately wired and separately rated, or is it not?
//
// The EDU36311A's three outputs are. They differ in what they can source
// (6 V / 5 A against two of 30 V / 1 A), each feeds a different DUT rail down
// its own lead, and each would have its own isolation relay if this rack had
// any. Three things a rig has to write down separately are three rows.
//
// The 33522B's two channels are not. They are identical, they are hard-cabled
// here, and they are the sort of thing a script uses together -- a stimulus
// and its inverse, a clock and the signal it gates. So the endpoint is chosen
// at the call site instead:
//
//     Apply( Wfg1.channel<2>().square().frequency( 10_kHz).amplitude( 3.3_V));
//
// which is the scope's arrangement (Osc1.channel<1>()) rather than the
// supply's, and for the same reason: one instrument, one address, one id, and
// a channel number that is a compile-time argument rather than part of the
// type. channel<3>() does not compile.
//
// One consequence worth knowing before writing a script against it: safing is
// per *box*, so hal::safeRig() takes both channels down together. That is the
// right granularity for a failed run and the wrong one for a script that wants
// one channel left running -- which is what Remove( Wfg1.channel<1>()...) is
// for, and it is per channel.
//
// Wfg1 has no rows in rig/wiring.inc and no SOURCE_WIRING row, and neither is
// an oversight. There are no relays in this rack at all (Thomas, 2026-09-04),
// so its outputs are cabled straight through and there is nothing for
// Connect/Disconnect to close -- its driver declares no connectDriver at all,
// so Connect( Wfg1.channel<1>().sine()) is a compile error rather than a call
// that switches nothing. And no point on this DUT is a function-generator
// stimulus point yet, so there is no pin for a SOURCE_WIRING row to name; that
// is the honest-spare position DcP5 is already in.
//
// -- The third column, and what it now means -------------------------------
//
// The third column is how the *PC* reaches each instrument -- the control
// side, as against the signal side wiring.inc describes (see hal/driver/address.hpp
// for the distinction, and hal/topology/active_instruments.hpp for why it is a fixed
// column rather than one more constructor argument). Which bus kinds a given
// row may use is fixed by its driver, not by this file: an EDU34450A is a LAN
// and USB box with no GPIB connector, so Gpib( ...) on the Dmm1 row is a
// compile error rather than a run that dies on its first reading.
//
// Dmm1 and Dmm2 used to be two *different* models, and the argument for that
// is kept here even though Dmm2 has gone, because it is the thing a reader
// adding a second meter is most likely to get wrong. They were two
// L4411A instances -- two wiring facts sharing one C++
// type -- and Dmm1 became a 5.5-digit hal::keysight_edu34450a::EDU34450A
// against the L4411A's 6.5. A script measuring a rail cannot tell two meters
// apart, and a criterion tightened past 5.5 digits can; anything reaching for
// .nplc() can too, since this family has no NPLC command at all. So a second
// meter is a second row with its own type, not a second instance of this one.
//
// -- Some rows say Simulated, and that is a decision ------------------------
//
// This paragraph used to argue that no row here should be hal::Simulated{},
// because it "would read as a decision rather than a gap". That was right while
// every driver in the tree answered from its own simulation hooks and no driver
// read its address at all: an invented hostname was then a note to a reviewer
// and nothing more.
//
// hal::keysight_edu34450a::EDU34450A changed that. It was the first driver here
// that opens a real session -- SCPI over a socket to the address in this column
// (see hal/io/transport.hpp) -- so for that row this column stopped being a
// note and became an instruction. "Lan( "bench-dmm1")" would mean *connect to
// bench-dmm1*, and every reading taken through Dmm1 on a bench where no such
// host exists would fail with a name-resolution error, correctly and
// immediately.
//
// So Dmm1 is hal::Simulated{} -- the first row here to say it -- which has
// become the accurate statement rather than the evasive one: there is no meter
// at a hostname anybody has written down, this driver can tell, and the honest
// thing for the table to say is that its readings come from the driver's own
// hooks.
//
// The rule, in one line: a row may name a real bus address when there is a
// real instrument at it, and must say Simulated{} until then.
//
// Which rows that binds has changed with the cull, and the change is worth
// noticing. Every row whose driver *ignores* its address is now one of the two
// kept deliberately without hardware -- AcP1 and Ser1 -- and those two keep
// invented addresses on purpose: they are this table's worked examples of a
// GPIB box and a PC serial port, which are otherwise only described in
// hal/driver/address.hpp. Every row whose driver can talk says Simulated{}.
// There is no longer a row in the middle, which is the state this rule was
// written to reach.
//
// DcP5..DcP7 arrived under that rule rather than being converted to it: their
// driver opens a session from the day it landed, so all three say Simulated{}
// and each carries the TODO that says what replaces it. What is not yet known
// about them is which of two things that will be -- an EDU36311A has both a
// LAN interface and a USB device port, so the row becomes Lan( "<hostname>")
// or Usb( "<serial>") depending on how the box is cabled, and both compile.
//
// All three carry the same address when they get one, and that repetition is
// the point: they are three outputs behind one chassis interface. If a second
// shared box ever appears here, the thing to add is a named constant above this
// table per chassis, not a slot field on the address.
//
// -- And Osc1 says it too, for the same reason -----------------------------
//
// hal::keysight_dsox1202g::DSOX1202G opens a session (see
// instruments/keysight_dsox1202g/src/keysight_dsox1202g.cpp for everything it
// sends), so this column is an instruction for that row too, and the rule above
// applies unchanged.
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
// hal::Simulated and nothing else.
//
// What is deliberately NOT lost by any of this: an attached run still reaches
// these drivers, and that is what the acceptance suite checks (see
// acceptance/test_acceptance.cpp's AcceptanceBench group). hal::Simulated
// removes the socket, not the driver -- Measure still calls rawMeasure, the
// journal still records the reading, and the run still fails its criteria
// because a simulated meter reads zero volts. What it removes is a CI machine
// trying to open a TCP connection to a hostname that describes a rack nobody
// has built.
//
// -- Ser1, and the one bus kind this table still exercises by hand ---------
//
// Ser1 is the one instrument here that is neither a meter nor a source: an
// RS232 port switched onto the DUT through the same fabric everything else
// uses (see hal::racal1260::Racal1260, and dut::Console for the interface it
// talks to). It takes no constructor argument beyond its id and address -- one
// instance is one port, the same way one EDU34450A is one meter.
//
// Its row is Serial on purpose, and for a reason that is not a claim about the
// bench: it is this table's one worked example of a PC-attached serial device,
// kept deliberately so that the bus kinds a rig is likely to meet are
// exercised by something here rather than only described in
// hal/driver/address.hpp.
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
    INSTRUMENT( keysight_edu34450a::EDU34450A,     Dmm1, Simulated{})        // TODO(bench): Lan( "<host>") or Usb( "<serial>") once a meter answers -- this driver connects
    INSTRUMENT( keysight_dsox1202g::DSOX1202G,     Osc1, Simulated{})        // TODO(bench): Usb( "<serial>") off the scope's own *IDN? -- this driver connects
    INSTRUMENT( keysight_edu36311a::DirectOutput1, DcP5, Simulated{})        // 6 V / 5 A, hard-wired: no relay in this rack carries 5 A. A spare -- nothing on this DUT wants 6 V
    INSTRUMENT( keysight_edu36311a::RelayOutput2,  DcP6, Simulated{})        // 30 V / 1 A -> dut::BackupSupply,  isolation relay on Spst1/6
    INSTRUMENT( keysight_edu36311a::RelayOutput3,  DcP7, Simulated{})        // 30 V / 1 A -> dut::BatterySupply, isolation relay on Spst1/7
    INSTRUMENT( keysight_ac6834b::Ac6834B,         AcP1, Gpib( 0, 5))        // simulated: kept for the Transient group, see this file's own comment
    INSTRUMENT( keysight_33522b::Wfg33522B,        Wfg1, Simulated{})        // TODO(bench): Lan( "<host>"), Usb( "<serial>") or Gpib( ...) -- this driver connects
    INSTRUMENT( racal1260::Racal1260,              Ser1, Serial( "/dev/ttyUSB0"))
END_INSTRUMENTS
