#pragma once

#include "core/topology/adapter.hpp"
#include "core/quantities/quantity_kind.hpp"
#include "hal/topology/vpc_location.hpp"

//
// ADAPTER / POINT / END_ADAPTER: declarative, Excel-readable adapter wiring
// tables, mirroring CRITERIA/CRIT/END_CRITERIA in core/criteria/criterion.hpp. Each
// POINT becomes a genuine static constexpr member of the group struct --
// exactly like CRIT -- so a misspelled point name at a Measure() call site
// is a real "no such member" compile error. That comes for free from ordinary
// C++ once the point's location is baked into its *type*
// (core::AdapterPointTag<Loc>) rather than stored as runtime data -- no
// reflection needed.
//
// A POINT names a pin and says where it is. It does NOT say what is measured
// there -- see core::AdapterPointTag's own comment for why binding a quantity
// to a pin turned out to forbid legitimate measurements (inrush current at a
// supply pin, ripple frequency at a rail) without protecting against anything.
//
// The one thing a point does declare about itself is whether anything can be
// routed to it at all -- POINT for the ordinary routable case, SOURCE_POINT
// below for a pin a source instrument is cabled straight onto.
//
// Point identifiers can't reuse a spec label verbatim when it starts with a
// digit (e.g. "5VOutput"): C++ identifiers can't start with a digit. Use a
// legal rearrangement instead (Output5V) -- see dut/adapter.inc.
//
//   ADAPTER( "Device X on standard adapter")
//       POINT( Output5V, A, 1, 3, "5Vdc supply port")
//   END_ADAPTER
//
//   Measure( Dmm1.voltage(), at( dut::Output5V));
//   Measure( Dmm1.current(), at( dut::Output5V));   // same pin, fine
//
// The group is always named `dut`, not named by the table the way CRITERIA
// names its groups. A build targets exactly one DUT on exactly one adapter,
// so there was never a second name for a script to pick between -- the old
// per-table name (DeviceX) was a second spelling of a fact the build already
// fixes, and one every call site had to repeat. The DUT's identity for
// traceability is a separate fact and stays separate: THORIUM_DUT_NAME (see
// framework/core/CMakeLists.txt) is what the logs report, and it is a display
// string, not a C++ identifier, so it is free to be "Device X rev B" without
// breaking a single Measure() call. What this table still names for itself is
// its Description, below.
//
// `dut` is a struct at global scope rather than a real namespace because that
// is what makes a misspelled point a "no such member" error (see
// core::AdapterPointTag) -- but it reads as the namespace it stands in for,
// which is why the codebase's comments have always written dut:: for it.
//
#define ADAPTER( desc)                                                 \
    struct dut                                                         \
    {                                                                  \
        static constexpr std::string_view Description = desc;

#define POINT( id, rack, connector, pin, desc)                                                                                \
        static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin }> id{ #id, desc };

//
// The same declaration for a pin a source instrument's output is cabled
// straight onto -- a rail this bench drives into the DUT, rather than a pin
// the DUT merely presents. Same arguments as POINT; the difference is
// entirely in what the point *is*.
//
//   ADAPTER( "Device X on standard adapter")
//       POINT(        Output5V,     A, 1, 3, "5Vdc supply port")
//       SOURCE_POINT( BackupSupply, A, 1, 5, "28Vdc backup supply")
//   END_ADAPTER
//
// Both remain measurable. A cabled rail is still worth reading at the DUT
// pin -- that is what catches cable drop and bad contacts, which the supply's
// own readback cannot see -- so SOURCE_POINT restricts nothing at the call
// site; see core::PointKind's own comment for why an earlier version of this
// did, and why that was wrong. What it does is record the fact, where a
// reader of the DUT's pinout will see it.
//
// Note what it still does not say: which instrument lands there. That is a
// fact about one bench's cabling, not about the device, and it lives in that
// rig's own wiring.inc (WIRE_SOURCE, see hal/topology/wiring.hpp). The two halves are
// cross-checked against each other -- every SOURCE_POINT must have a
// WIRE_SOURCE entry, and every WIRE_SOURCE pin must be declared SOURCE_POINT
// rather than POINT -- in dut/tests/test_wiring_coverage.cpp, which is the
// one place both files are visible at once. So the adapter cannot quietly
// describe a driven rail as an ordinary pin, which is the mistake worth
// catching: a reader deciding what is safe to do at a pin is reading this
// file, not the rig's.
//
#define SOURCE_POINT( id, rack, connector, pin, desc)                                                     \
        static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin },  \
                                                 ::core::PointKind::Source> id{ #id, desc };

//
// BUNDLE / LINE / SOURCE_LINE / END_BUNDLE: a group of points that are one
// physical interface -- an RS232 console, an Ethernet pair set, a
// three-phase input -- rather than several unrelated pins that happen to sit
// near each other on the connector.
//
//   ADAPTER( "Device X on standard adapter")
//       POINT( Output5V, A, 1, 3, "5Vdc supply port")
//
//       BUNDLE( Console, "RS232 debug console")
//           LINE( Tx,  A, 2, 1, "transmit")
//           LINE( Rx,  A, 2, 2, "receive")
//           LINE( Gnd, A, 2, 5, "signal ground")
//       END_BUNDLE
//   END_ADAPTER
//
//   Measure( Dmm1.voltage(), at( dut::Console::Tx));   // one line
//   Connect( Ser1.rs232(),   at( dut::Console));       // the whole interface
//
// Both spellings work, and neither is a special case: a bundle is a nested
// struct (so dut::Console::Tx is ordinary member lookup) *and* a value of
// core::AdapterBundle (so at( dut::Console) has something to pass). See that
// type's own comment for how one name manages to be both, and what it costs.
//
// Routing to the bundle closes every LINE in it together, which is what makes
// it worth naming: an RS232 console is not usable one wire at a time, so the
// unit a script connects has to be the interface, not the pin. See
// hal::bundleLocations in hal/topology/bundle.hpp for the walk that recovers the lines,
// and hal::Racal1260's connectDriver for the one that uses it.
//
// A nested struct-as-namespace, reached with :: exactly the way `dut` itself
// is (see this file's own comment above on why `dut` is a struct rather than
// a real namespace) -- so dut::Console::Tz is the same "no such member"
// compile error a misspelled top-level point already gives, and END_BUNDLE
// needs no repeat of the name, matching END_GROUP/END_CRITERIA/END_ADAPTER.
//
// LINE and SOURCE_LINE are POINT and SOURCE_POINT under different names,
// producing the identical AdapterPointTag: a line in a bundle is an ordinary
// point that is also measurable, coverage-checked and pin-unique-checked on
// its own (see core::AdapterBundleTag's own comment on why grouping does not
// create a second kind of thing). Renamed only so a reader can see at the
// call site that they are inside a bundle rather than at the top level.
//
// Not called BUS, deliberately: this codebase already had a hal::Bus, and it
// meant something else entirely -- a digital register bus a script read
// values *from*, removed in the VPC/instrument redesign (see
// suite/scripts/fuse_register_script.cpp's own comment). Reusing the word
// for a bundle of wires would make that removed model look like it came
// back.
//
// The three declarations are in this order for a reason -- see
// core::AdapterBundle's own comment, which is where the whole arrangement is
// explained. In short: the value has to be declared before the struct it names
// is defined, because once the value exists it hides the class name for
// ordinary lookup, and `struct id : ...` is an elaborated-type-specifier that
// looks past it. Reversing the two, or folding the value into END_BUNDLE,
// breaks either at( dut::Console) or dut::Console::Tx.
//
#define BUNDLE( id, desc)                                              \
        struct id;                                                     \
        static constexpr ::core::AdapterBundle<id> id{ #id, desc };    \
        struct id : ::core::AdapterBundleTag                           \
        {                                                              \
            static constexpr ::std::string_view Name        = #id;     \
            static constexpr ::std::string_view Description = desc;

#define LINE( id, rack, connector, pin, desc)                                                                                     \
            static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin }> id{ #id, desc };

#define SOURCE_LINE( id, rack, connector, pin, desc)                                                              \
            static constexpr ::core::AdapterPointTag<::hal::VpcLocation{ ::hal::VpcRack::rack, connector, pin },  \
                                                     ::core::PointKind::Source> id{ #id, desc };

#define END_BUNDLE };

#define END_ADAPTER };
