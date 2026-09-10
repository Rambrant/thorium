#pragma once

#include <concepts>
#include <string>
#include <vector>

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/topology/address_plan.hpp"

//
// The two passes that happen between "the rig's instrument globals exist" and
// "the first script runs", and the one line per instrument they produce.
//
// This sits in verbs/ beside safing.hpp because it is the same shape: a
// whole-rig operation that reflects over the InstrumentTag-derived globals
// rather than reading rig/instrument.inc one more time (see
// hal/src/verbs/safing.cpp's own comment on that reflection, which this
// reuses). It belongs to hal_rig for the same reason all four of those do.
//
// -- Two passes and not one, and the split is load-bearing -----------------
//
// bindAddresses() is pure. It reads the plan, the site table and the pool
// table, decides each row's address and writes it into the driver. It opens
// no socket, so it runs for every invocation including --replay, --inject and
// --skeleton, and it cannot fail for any reason other than a plan that does
// not make sense.
//
// contactInstruments() is the half that touches hardware: it acquires the
// pooled rows, asks every reachable instrument *IDN?, and fills in each
// binding's Identity. It runs only for an attached run. That boundary is the
// same one hal::safeRig() draws and it is drawn for the same reason -- a
// --replay at a desk must not reach whatever bench the runner happens to be
// pointed at.
//
// -- Why binding happens at startup rather than at first use ---------------
//
// Sessions are opened lazily, and have to be: the rig's instruments are
// globals constructed before main(), so a constructor that opened a socket
// would make every binary that links the rig try to reach the bench at
// static-initialisation time (see any driver's session()).
//
// With one fixed address per row that laziness is invisible. With a pool it
// is not. Seven rows would each acquire at a different moment during the run;
// a closeSession() partway through would silently re-acquire a *different*
// box; and "which instrument is this run using" would have no single answer.
// So the decision is made once, in one place, at one moment, before the first
// script -- and is then sticky for the run. It is also the only way the
// banner can exist at all: a line printed before the run means the binding
// happened before the run.
//

namespace hal
{
    //
    // Every instrument must be rebindable, and the loop static_asserts this
    // rather than filtering on it -- exactly as hal::SafeableInstrument is
    // used (see hal/driver/instrument.hpp). A driver missing useAddress()
    // would be a driver the resolver silently skipped, and a silently
    // unresolved row is the failure this whole mechanism exists to prevent.
    //
    // useAddress() itself is a handful of lines per driver and the sibling of
    // the useTransport() every connecting driver already has: replace the
    // stored address, drop any open session. It validates nothing, and does
    // not need to -- bindAddresses() has already checked the address against
    // this same driver's Buses, so what arrives is always something its own
    // constructor would have accepted.
    //
    // Buses is the other half of that, and is required here rather than
    // assumed. A driver that declared no back panel would have no list to
    // check an --address against, and hal::BackPanelInfo's default refuses
    // everything -- so the symptom would be every override on that one row
    // being rejected against an empty list of alternatives, which is a puzzle
    // rather than a diagnostic. Requiring it makes that a one-line compile
    // error naming the driver instead.
    //
    template<typename InstrumentT>
    concept AddressableInstrument = requires( InstrumentT & instrument, const Address & address)
    {
        { instrument.id()            } -> std::same_as<InstrumentId>;
        { instrument.address()       } -> std::convertible_to<const Address &>;
        { InstrumentT::Buses::info() } -> std::same_as<BackPanelInfo>;
        instrument.useAddress( address);
    };

    //
    // Whether this driver can be asked what it is. Branched on, NOT
    // static_asserted, and the asymmetry with the concept above is the
    // interesting part of this file.
    //
    // hal::keysight_ac6834b::Ac6834B and hal::racal1260::Racal1260 have no
    // identity() and no closeSession(), because they open no session at all:
    // they answer from their own simulation hooks, and rig/instrument.inc
    // keeps both deliberately (AcP1 for the Transient group, Ser1 for the
    // Console group). That is a decision the table already records, not a
    // driver that forgot something, so the preflight reports it as a fact
    // rather than refusing to build. The banner prints "no session" for those
    // rows and the run continues.
    //
    template<typename InstrumentT>
    concept ContactableInstrument = requires( InstrumentT & instrument)
    {
        { instrument.identity() } -> std::convertible_to<std::string>;
        instrument.closeSession();
    };

    //
    // Pass one. Decides every row's address and writes it into the driver.
    // Pure -- no I/O, no bench, and no failure that is not a bad plan.
    //
    // Called once per process, and the shape of it assumes that. The baseline
    // it resolves from is each driver's *current* address, which before the
    // first call is the one rig/instrument.inc constructed it with -- there
    // is nowhere else the table's value is kept, and adding a field to every
    // driver to remember it would be storing a fact for the benefit of a
    // second call nothing makes. So a second call with a different plan
    // layers on the first rather than replacing it, and anything that wants
    // to undo one has to have kept the addresses itself (see
    // rig/tests/test_preflight.cpp, which does exactly that and says why).
    //
    // Priority, highest first, and each level may only replace the payload of
    // the level below it:
    //
    //     1. an --address override for this row
    //     2. this deployment's site table, for the selected site
    //     3. this row's candidate pool -- left unresolved here, marked
    //        AddressSource::Pool, and acquired in pass two
    //     4. the row's own third column
    //
    // Throws AddressKindMismatch for an override, site row or pool candidate
    // naming a bus the instrument has no connector for, and
    // AddressSyntaxError for a --site this deployment does not declare.
    // main() reports both before any log is opened.
    //
    [[nodiscard]]
    auto bindAddresses( const AddressPlan & plan) -> std::vector<Binding>;

    //
    // Pass two. Attached runs only; a no-op otherwise.
    //
    // For a row with a fixed address: open the session and record what *IDN?
    // answered. The drivers already check their own *model* on a first session
    // (see hal::keysight_33522b's prepare()); what this adds is that the check
    // happens now, on every instrument at once, before the first measurement,
    // rather than whenever some script first happens to touch that box.
    //
    // For a row with a pool: try each candidate in table order until one
    // connects, and claim it for the run.
    //
    // Two rules make pool acquisition honest, and both are enforced here
    // rather than left to the hardware:
    //
    //   No box is claimed twice. A serial already taken by an earlier row is
    //   skipped even though it would connect, so two rows drawing on one pool
    //   can never land on the same instrument. Most LXI boxes accept a single
    //   socket session and would refuse the second attempt anyway -- but
    //   relying on that would make the diagnostic "connection refused" three
    //   layers down instead of "pool exhausted: 2 rows, 1 free meter".
    //
    //   A wired row may not have a pool. Checked in pass one, not here: if the
    //   deployment declares any wiring for that instrument -- a routed wire, a
    //   source landing, or a bare WIRE_TAP row, which needs no switching
    //   hardware at all (see dev/rig/wiring.inc) -- then its leads are
    //   somewhere specific, it is not interchangeable with its siblings, and a
    //   pool would be a coin flip on which node gets measured.
    //
    //   Read that second one as a veto and not as a licence, which is the one
    //   thing about it that is easy to get backwards. It refuses the obviously
    //   wrong pool. It cannot approve a right one, because the fact it tests
    //   -- "something distinguishes this row" -- is only ever *declared*, and
    //   a clip lead nobody wrote down is invisible to it. Two rows on this
    //   repo's bench pass the veto today and should still never have pools:
    //   Wfg1 and DcP5 are cabled straight through with no wiring rows at all.
    //   A pool row therefore stays a claim its author makes, and the banner
    //   below is what actually catches a wrong one.
    //
    // Throws io::TransportError for a fixed address that cannot be reached and
    // for an exhausted pool. Both are rig faults, and both are worth failing
    // the run before it starts rather than three tests in.
    //
    auto contactInstruments( std::vector<Binding> & bindings) -> void;

    //
    // The banner: one line per instrument, column-aligned, in the order the
    // reflection finds them.
    //
    // It goes into the traceability header (see core::RunInfo::Instruments)
    // rather than only to the console, so that a log read a week later still
    // says which physical instruments produced it -- which is the whole point
    // on a fleet of identical benches, and on a desk where the meter changes
    // daily.
    //
    //     Dmm1  keysight_edu34450a::EDU34450A  Lan dev-dmm-3:5025  --address    Keysight Technologies,EDU34450A,MY60012345,01.00-01.00
    //     Osc1  keysight_dsox1202g::DSOX1202G  Usb CN59176621      pool 2 of 3  KEYSIGHT TECHNOLOGIES,DSO-X 1202G,CN59176621,01.20
    //     AcP1  keysight_ac6834b::Ac6834B      Gpib 0::5           table        no session -- answers from its own hooks
    //
    // The address column is hal::to_string( address), which already exists and
    // already spells every kind. The provenance column is the one a reader
    // most needs and the one no other line of a report carries: Lan(
    // "dev-dmm-3") looks identical whether it was compiled in, selected for
    // this site, taken off a shelf or typed on the command line.
    //
    [[nodiscard]]
    auto bannerLines( const std::vector<Binding> & bindings) -> std::vector<std::string>;
} // namespace hal
