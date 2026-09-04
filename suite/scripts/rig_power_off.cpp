#include "../prelude.hpp"

//
// The catalog's RUN_TEARDOWN: this rig's ordered power-down, run once after the
// last selected script -- including on the ways out that aren't the happy one
// (--until-failure stopping early, a script throwing past everything), since
// the runner holds it in a guard destructor. See suite/test_catalog.inc and
// README.md's "Bracket a run with setup and teardown".
//
// Worth being explicit about what this is *not*, because hal::safeRig() exists
// and at a glance does the same job. safeRig() is the crash path: unconditional,
// stateless, reflected over whatever InstrumentTag-derived globals happen to be
// declared, in whatever order that reflection yields -- and it runs immediately
// after this hook regardless of what happened here (framework/runner/src/main.cpp holds a
// TeardownGuard and a hal::RigSafingGuard, in that order). This is the normal
// path, and what earns it a file of its own is the one thing safeRig()
// deliberately cannot express: a sequence. hal/src/verbs/safing.cpp says as much in
// its own comment on why it carries no SAFING_ORDER table -- "if some future
// instrument turns out to need to go strictly first or last relative to its
// peers -- a real constraint this loop cannot express -- that is when an
// explicit order earns its place". Powering a DUT down is that case.
//
auto rigPowerOff() -> bool
{
    //
    // Alternate sources before the primary, and that ordering is the whole
    // point of the hook. A DUT with a live backup rail transfers onto it the
    // instant the primary drops, so a power-down that takes the primary first
    // does not power the DUT down at all -- it moves it onto the backup and
    // hides that it did. Every DC rail goes first for that reason; AcP1, this
    // rig's three-phase primary input (dut::AcInput, a BUNDLE of four lines
    // -- see dut/adapter.inc), goes last.
    //
    // Within the DC group the order carries no claim yet, and this file should
    // not pretend otherwise: whether the battery rail has to go before or
    // after the backup is a DUT fact this deployment has not recorded. When
    // that is confirmed, the order below is where it gets written down.
    //
    // Two DC rails rather than three, since 2026-09-02: this rig's supplies
    // are now the two usable outputs of one EDU36311A where they were four
    // channels of an N6701A mainframe, and dut::BackupSupply_2 has nothing
    // driving it any more (see rig/instrument.inc and dut/adapter.inc). There
    // is nothing to take down for a rail nobody brought up.
    //
    Remove( DcP6.dc());
    Remove( DcP7.dc());
    Remove( AcP1.ac());

    //
    // Outputs off first, relays after. Not tidiness: opening a relay while
    // current is still flowing through it is hot switching, which arcs, welds
    // contacts and destroys the relay -- the same rule hal::safeRig() follows,
    // where it falls out of the structure (every safe() runs, then the fabric
    // opens once). Here it is only the order these statements are written in,
    // which is precisely the argument for having them in one function rather
    // than spread across whichever scripts happened to use each supply.
    //
    // Every source this hook took down appears here, which was not true
    // before: while an N6701A fed the DC rails, DcP1/DcP2 were
    // hal::keysight_edu36311a::DirectOutput1 -- wired straight through with no isolation
    // relay -- so Disconnect( DcP1.dc()) was a compile error rather than a
    // call that quietly did nothing. The EDU36311A outputs that replaced them
    // are 1 A and fit inside a 1260-18 relay, so both are RelayOutput and both
    // have something to open.
    //
    // Which is still enforced rather than remembered, and there is still an
    // output on this bench it would refuse: DcP5, the 6 V / 5 A one, is
    // DirectOutput because no relay in this rack carries 5 A, so
    // Disconnect( DcP5.dc()) does not compile (see
    // hal::keysight_edu36311a::SwitchableIsolation). It drives nothing here, so
    // the question does not arise -- but the guarantee is the same one.
    //
    Disconnect( DcP6.dc());
    Disconnect( DcP7.dc());
    Disconnect( AcP1.ac());

    //
    // No catch-all "open whatever else is still closed" step, deliberately.
    // That is hal::fabric.openAll(), which safeRig() calls moments later; a
    // teardown clearing the fabric wholesale would be asserting something about
    // routes it never made, which is the crash path's job exactly because
    // nobody there knows either.
    //
    // Returns true unconditionally: there is no verdict here to report. Remove
    // and Disconnect either do what they were told or throw (a real instrument
    // refusing a command), and a false would fail an otherwise clean run -- see
    // README.md's "A failing hook fails the run". Confirming the rails actually
    // read dead afterwards is a worthwhile check and a real one to add, but it
    // is a TEST with criteria of its own, not a verdict smuggled out of a
    // teardown.
    //
    return true;
}
