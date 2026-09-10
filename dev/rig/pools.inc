// ReSharper disable once CppMissingIncludeGuard

//
// The dev desk's shelf of meters: whichever one is free is Dmm1.
//
// The fifth rig file and one of the two optional ones -- see
// hal/topology/address_tables.hpp on why "this deployment has no pools" is not
// a fact every rig directory has to write out, where "this bench has no cards"
// (devices.inc) is. Nothing selects this file: the top-level CMakeLists.txt
// picks it up because it is here, and deleting it puts this deployment back on
// the empty fallback table with no other edit.
//
// This is the only pool table in the tree, and the bench next door must never
// grow one. That asymmetry is the whole content of this file, so it is worth
// stating before the rows.
//
// -- Why a pool is the right table here, and a site table is not ------------
//
// hal/topology/address_plan.hpp draws the line, in one sentence: pool when the
// value is unknowable at build time, site when it is merely unknown to you.
//
// A desk sharing three identical EDU34450As is the first. Which one answers is
// not a fact about this deployment that somebody could go and look up -- it is
// a fact about who else is using the shelf this afternoon, and it can differ
// between two runs an hour apart. There is nothing to write in a column and no
// bench identity to select, because the three meters are not three benches:
// they are one desk with three interchangeable boxes on it.
//
// A fleet of four identical rigs would be the second, and the table for that
// is sites.inc, which this deployment does not have either.
//
// -- Why this is legal here and a compile error on the bench ----------------
//
// A pool says its candidates are interchangeable. A wiring row says an
// instrument's leads go somewhere specific. Both cannot be true, so
// hal/src/verbs/preflight.cpp holds a static_assert over this deployment's own
// wiring -- see isDistinguished() there, and hal/verbs/preflight.hpp on reading
// it as a veto rather than a licence.
//
// Dmm1 passes it here because dev/rig/wiring.inc is empty in all four blocks:
// nothing in this deployment routes it, sources through it or taps it, so
// there is genuinely nothing that tells one of these meters from another. Move
// these rows to rig/pools.inc and the build stops with the offending id named
// -- the bench's Dmm1 reaches the DUT through the matrix, so a pool there
// would be a coin flip on which node gets measured.
//
// The veto is a claim about tables rather than about hardware, so it can only
// refuse a wrong pool and never approve a right one. This file is still an
// assertion its author makes: that these three boxes really are the same
// model with the same leads on nothing.
//
// -- What this changes about the third column of instrument.inc -------------
//
// It stops being consulted on an attached run, and that is the trap worth
// knowing about before editing it. A pool outranks the row's own address (see
// hal::bindAddresses()'s order: an override, then a site row, then a pool,
// then the column), and a pooled row is deliberately left unbound at bind time
// -- writing the table's address into the driver would be writing something
// nobody chose.
//
// So Lan( "dev-dmm") over in instrument.inc is now what a *detached* run
// carries and never opens, plus the thing that fixes the row's kind at compile
// time. Changing it will not point an attached run anywhere.
//
// What does still win is the operator, which is the order that matters:
//
//     run_scripts --address Dmm1=lan:dev-dmm-7
//
// or THORIUM_ADDRESS_Dmm1 in the shell. A meter that is on none of these three
// needs no edit to this file, and the run's header says the flag chose it.
//
// -- The rows themselves ----------------------------------------------------
//
// Table order is preference order -- acquireFromPool() in
// hal/src/verbs/preflight.cpp tries them in the order written and takes the
// first that answers *IDN? -- so the meter usually free goes first, and the one
// somebody else usually has goes last. A candidate
// that is busy, powered off or absent raises io::TransportError and is simply
// the wrong one; only running out of candidates is this row's failure, and it
// arrives at startup as "no free instrument for Dmm1: all 3 ..." rather than
// at the first Measure.
//
// Every candidate's bus is checked against the driver's own back panel before
// any of them is tried, so a mistyped row fails at startup rather than on
// whichever meter the shelf happens to leave free (see requireAccepted() in
// hal/src/verbs/preflight.cpp, and hal::BackPanel).
//
// All three are Lan for no deeper reason than that all three have a network
// cable. A Usb( "MY60012345") candidate beside them would be accepted -- an
// EDU34450A has both connectors, and the back-panel check is about the
// instrument rather than about this table's other rows -- and the pool would
// then span two buses, which is legal and means only what it says: either box
// will do.
//
// These hostnames are placeholders in the same way the row's was, and name one
// developer's desk rather than a rack a reviewer can check. What is better
// than the single placeholder they replace is how they fail: three chances
// instead of one, any of which is enough, and no dirty working tree for a desk
// whose meter happens to be the second one.
//
// One row per candidate with the id repeated, rather than a list nested in one
// entry -- the shape WIRE_INSTRUMENT already uses, and what keeps the table
// greppable for "which rows can end up on dev-dmm-2". With one instrument here
// that reads like ceremony; it is what makes a second pooled row, and the
// "already claimed by an earlier row" rule that comes with it, an ordinary
// edit rather than a reshape.
//
ADDRESS_POOLS
    POOL( Dmm1, Lan( "dev-dmm-1"))      // the desk's own meter, free unless somebody borrowed it
    POOL( Dmm1, Lan( "dev-dmm-2"))
    POOL( Dmm1, Lan( "dev-dmm-3"))      // the shared one on the end of the bench, usually taken
END_ADDRESS_POOLS
