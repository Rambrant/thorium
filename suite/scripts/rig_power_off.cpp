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
// deliberately cannot express: a sequence. hal/src/safing.cpp says as much in
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
    // not pretend otherwise: which DcP instance is the battery rail, which are
    // backups, and which feeds discrete-input signals is a DUT fact this
    // deployment has not recorded -- dut/adapter.inc makes exactly the same
    // hedge about which point each one lands on ("worth confirming against the
    // schematic before asserting it"). When that mapping is confirmed, the
    // order below is where it gets written down.
    //
    Remove( DcP1.dc());
    Remove( DcP2.dc());
    Remove( DcP3.dc());
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
    // Three of the five appear here, and that is enforced rather than
    // remembered: DcP1/DcP2 are hal::N6701ADirect -- wired straight through
    // with no isolation relay -- so Disconnect( DcP1.dc()) is a compile error
    // rather than a call that quietly does nothing (see hal::SwitchableIsolation
    // in hal/n6701a.hpp).
    //
    Disconnect( DcP3.dc());
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
