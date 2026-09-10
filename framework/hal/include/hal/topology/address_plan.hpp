#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"

namespace hal
{
    //
    // Where a row's address actually comes from, when the third column of
    // rig/instrument.inc is not the last word on it.
    //
    // That column is the last word for the bench in this repo, and for most
    // rigs it always will be. It stops being the last word in two situations,
    // and they are different enough to be worth naming separately before
    // anything below makes sense:
    //
    //   A fleet of identical rigs. Four benches, one release, and the only
    //   thing that differs between them is a USB serial or a hostname. The
    //   table cannot hold four values in one column, and four builds would
    //   make "the same software on every rig" a claim to be proved rather
    //   than one the artifact makes.
    //
    //   A desk with a pool of identical instruments. Three EDU34450As on a
    //   shelf, whichever is free is the one you get. Here the address is not
    //   merely unknown at build time, it is unknown at *start* time and has to
    //   be acquired.
    //
    // One seam serves both, because both are the same sentence: the table
    // fixes the *kind* of address a row has, and something outside the table
    // supplies the value.
    //
    // -- What a resolved address is checked against -------------------------
    //
    // The driver's own back panel, and nothing else -- hal::BackPanel in
    // hal/driver/address.hpp, which is the list its constructor is already
    // constrained by, in a form this can ask at run time.
    //
    // A DSOX1202G has a USB device port and no network connector, so
    // Lan( ...) on the Osc1 row is a sentence about hardware that does not
    // exist. In the table that is a compile error. Arriving from --address it
    // is caught here instead, before any session is opened and before the
    // first script -- same list, same answer, one spelling.
    //
    // What is deliberately NOT the rule is the tempting narrower one: that a
    // resolved address must be the same *kind* the table wrote. That reads
    // like the conservative choice and is the wrong one twice over. It would
    // refuse to move a meter between the two connectors it really has, and --
    // far worse on this repo's own bench -- it would refuse every override on
    // a Simulated{} row, which is most of them.
    //
    // Simulated is where the difference bites, so it is worth being explicit
    // about what each rule says. Simulated{} means "there is no instrument
    // here" (see hal/driver/address.hpp), and rig/instrument.inc's rule says
    // a row must say it until one turns up. Under a same-kind rule that state
    // would be a dead end: you could not point the row at the meter you just
    // plugged in without first editing the table for a bench you have not
    // confirmed yet. Under this one, the bring-up is the obvious order --
    //
    //     run_scripts --address Dmm1=usb:MY60012345
    //
    // -- confirm it answers, and *then* write the row, with the serial the
    // banner printed. The check has not been skipped: an EDU34450A has LAN
    // and USB, so that flag is accepted and Osc1=lan:... would still be
    // refused on a scope that has no network connector.
    //
    // One honest constraint remains, and it is about fleets rather than about
    // types. If bench 2 reaches its scope over LAN where bench 1 uses USB,
    // those are not identical rigs -- nothing here refuses that any more, but
    // a site table saying it is a site table describing two different benches.
    //
    // -- Site and pool are orthogonal, and confusing them is the trap -------
    //
    // The two situations above are not degrees of the same thing, and the
    // number of rigs has nothing to do with which one applies:
    //
    //   Site -- the identity is known and matters. Twenty identical benches
    //   are twenty site rows, not a pool of twenty meters: bench 7 has exactly
    //   one meter that can be Dmm1, because that meter's leads are on
    //   CROSSPOINT( Matrix1, 0, 0, 0). The framework must be *told* which
    //   bench it is on, and that is one decision for the whole rig, every row
    //   together.
    //
    //   Pool -- the identity is unknown and does not matter. The framework may
    //   *choose*, per row, independently. Only safe where the rows are
    //   unrelated, which is a property of the deployment's own wiring tables
    //   (see hal::contactInstruments() in hal/verbs/preflight.hpp).
    //
    // The rule that tells them apart, in one line: pool when the value is
    // unknowable at build time, site when it is merely unknown to you. A desk
    // where someone else has two of the three meters is the first; a bench
    // nobody has yet walked over to and read the serial off is the second, and
    // the answer there is to go and read it.
    //

    //
    // Which of the four sources a bound address came from.
    //
    // Carried all the way through to the banner, because "where did this value
    // come from" is the question an operator looking at an unexpected
    // instrument actually has, and it is not answerable from the value itself:
    // Lan( "dev-dmm-3") looks identical whether it was compiled into the
    // table, selected for this site, taken off a shelf, or typed on the
    // command line.
    //
    enum class AddressSource
    {
        Table,      // the row's own third column, unmodified
        Site,       // this deployment's site table, for the selected site
        Pool,       // acquired from this row's candidate pool at startup
        Override    // --address, or $THORIUM_ADDRESS_<Id>
    };

    [[nodiscard]]
    auto to_string( AddressSource source) -> std::string_view;

    //
    // One row's worth of what the resolver decided. Produced by
    // hal::bindAddresses(), completed by hal::contactInstruments(), rendered
    // by hal::bannerLines(), and stamped into the traceability header.
    //
    struct Binding
    {
        InstrumentId     Id{};

        //
        // The driver's own type name -- "keysight_edu34450a::EDU34450A" --
        // taken off the reflection rather than out of a table. The INSTRUMENT
        // row's type column and this are the same fact, and this is the copy
        // that cannot drift.
        //
        std::string_view Type{};

        Address          Value{};
        AddressSource    Source{ AddressSource::Table };

        //
        // The driver's back panel, carried so that the resolver can check an
        // address against it without being a template over every driver in
        // the rig -- see hal::BackPanelInfo. Recorded at bind time, where the
        // reflection still knows the type.
        //
        BackPanelInfo    Panel{};

        //
        // Which site row supplied Value, for AddressSource::Site only. The
        // banner names it, because on a fleet "this is bench 2's address" is
        // the fact and the hostname is only its consequence.
        //
        std::string_view Site{};

        //
        // Whether this driver has a session to open at all -- see
        // hal::ContactableInstrument. Recorded at bind time rather than
        // rediscovered later, because it is a property of the driver's type
        // and the banner is written from these records long after the
        // reflection that knew the type has finished.
        //
        // It is what lets an empty Identity be reported precisely: a row that
        // *could* have been contacted and was not is a detached run, and a row
        // that could not is AcP1 or Ser1 saying so.
        //
        bool             CanContact{ false };

        //
        // The instrument's own *IDN? reply, filled in by contactInstruments()
        // and empty until then.
        //
        // Empty is not a failure, and it has three legitimate causes the
        // banner distinguishes rather than collapsing: a detached run, which
        // must not touch hardware at all; a driver with no session (AcP1,
        // Ser1 -- see rig/instrument.inc on why both are kept); and a run that
        // has not reached the preflight yet.
        //
        std::string      Identity{};

        //
        // Only for AddressSource::Pool -- which candidate of how many was
        // taken, so the banner can say how much of the shelf was busy. A pool
        // that is always exhausted down to its last candidate is a lab fact
        // worth being able to see.
        //
        std::optional<std::pair<int, int>> PoolPosition{};
    };

    //
    // What the resolver was told before it started.
    //
    // The pools and the site rows themselves are not here: those are rig
    // tables, the same way wiring is (see hal/topology/address_tables.hpp).
    // This is only what a caller supplied at the moment of the run.
    //
    struct AddressPlan
    {
        //
        // Highest priority, and deliberately so. The operator standing at the
        // bench with the instrument in front of them beats every table in the
        // tree, and needs to win without editing a file that is under review.
        //
        // That last part is the whole point on the dev desk today: the address
        // in dev/rig/instrument.inc is documented as a thing you edit, which
        // means a permanently dirty working tree and a live chance of
        // committing your own desk's hostname into the deployment.
        //
        std::vector<std::pair<InstrumentId, Address>> Overrides;

        //
        // Which row of the site table is live. Empty means "this deployment
        // has no site table", which is every deployment in this repo today.
        //
        std::string_view Site{};
    };

    //
    // An address this row's driver has no connector for -- the one thing the
    // rule above forbids.
    //
    // Its own type so that main() can report it before a log exists, with a
    // message naming the connectors the instrument does have. This is the
    // runtime face of hal::ReachableOver: the same list the compiler applied
    // to the table's own address, applied to one that arrived too late for
    // the compiler to see.
    //
    class AddressKindMismatch : public std::runtime_error
    {
        public:
            explicit AddressKindMismatch( const std::string & what) : std::runtime_error( what) {}
    };

    //
    // An --address that could not be read at all -- an unknown instrument, a
    // missing '=', a bus kind nobody has heard of, a GPIB address that is not
    // a number.
    //
    // Separate from the mismatch above because the two are different mistakes:
    // this one is a typo, and that one is a claim about hardware. The
    // diagnostics differ accordingly.
    //
    class AddressSyntaxError : public std::runtime_error
    {
        public:
            explicit AddressSyntaxError( const std::string & what) : std::runtime_error( what) {}
    };

    //
    // "Dmm1=lan:dev-dmm-3", "Osc1=usb:CN59176621", "AcP1=gpib:0,5",
    // "Ser1=serial:/dev/ttyUSB1", "Dmm1=sim" -> one override.
    //
    // The kind prefix is mandatory even though the row already fixes it, and
    // that redundancy is deliberate: a bare "Dmm1=dev-dmm-3" would be a value
    // whose meaning depends on a table the person typing it is not looking at,
    // and stating the kind is exactly what lets AddressKindMismatch be
    // diagnosed rather than guessed at.
    //
    // The returned address borrows from `text`, so the caller's storage must
    // outlive the plan. That is free for the real caller -- argv lives as long
    // as the process (see framework/runner/src/main.cpp) -- and is why
    // hal::Lan and friends hold string_view in the first place; see
    // environmentOverrides() below for the one caller that has to do
    // something about it.
    //
    [[nodiscard]]
    auto parseOverride( std::string_view text) -> std::pair<InstrumentId, Address>;

    //
    // Every $THORIUM_ADDRESS_<Id> the environment sets, for the ids this rig
    // has -- so a bench PC can pin an instrument for every run on it without
    // anyone typing a flag.
    //
    // Reads the environment once and copies what it finds into storage that
    // lives as long as the process, because getenv()'s result does not: it
    // points into an environment block that setenv() is free to reallocate.
    // The flags need no such copy, which is the asymmetry the comment on
    // parseOverride() is about.
    //
    [[nodiscard]]
    auto environmentOverrides() -> std::vector<std::pair<InstrumentId, Address>>;

    //
    // $THORIUM_SITE, or empty. Same storage argument as above.
    //
    [[nodiscard]]
    auto environmentSite() -> std::string_view;
} // namespace hal
