#pragma once

#include <meta>
#include <optional>
#include <string_view>
#include <vector>

#include "hal/driver/address.hpp"
#include "hal/driver/instrument.hpp"

//
// The two optional rig tables that can supply a row's address when its own
// third column does not: this deployment's sites, and its address pools.
//
// It sits in topology/ for the reason active_instruments.hpp and wiring.hpp
// do: this directory is where the mechanism behind each of the deployment's
// declarative tables lives. See hal/topology/address_plan.hpp for what the
// two tables mean and when each is the right one -- that argument is not
// repeated here.
//
// -- Both tables are optional, unlike every other one in a rig directory ----
//
// rig/ has neither and needs neither. That is deliberate, and it is the
// opposite of the convention devices.inc and wiring.inc follow: those are
// written out empty because "this bench has no cards" is a statement about the
// bench, and because their readers are *defined* by the expansion, so a
// missing file would be a link error rather than an answer of "none".
//
// "This deployment has no pools" is not a statement about a bench. It is the
// absence of a feature most deployments will never use, so requiring every rig
// directory to carry an empty copy would be ceremony rather than a fact worth
// writing down.
//
// The mechanism is the one core::criteria already uses for the same shape of
// optional table (see core/src/criteria/criteria_variants.cpp and
// core/criteria/detail/no_criteria_variants.inc): the fallback is named here,
// next to the reader that needs it, and CMake defines the macro only for a
// deployment that has a table. Doing it here rather than with an if(EXISTS) in
// the build means a translation unit compiled outside this build -- a driver's
// own test, an out-of-tree rig repo -- still reads a valid empty table instead
// of failing on an undefined macro.
//
#ifndef THORIUM_POOL_TABLE
    #define THORIUM_POOL_TABLE "hal/topology/detail/no_pools.inc"
#endif

#ifndef THORIUM_SITE_TABLE
    #define THORIUM_SITE_TABLE "hal/topology/detail/no_sites.inc"
#endif

namespace hal
{
    //
    // One candidate an instrument may be bound to. Several rows per
    // instrument, in the order the table writes them, which is the order they
    // are tried in -- so a lab can put the usual meter first and the shared
    // one last.
    //
    struct PoolEntry
    {
        InstrumentId Instrument{};
        Address      Value{};
    };

    //
    // One instrument's address on one bench of a fleet. Keyed by a site name
    // rather than an index, because the name is what a bench PC is configured
    // with and what the traceability header records.
    //
    struct SiteEntry
    {
        std::string_view Site{};
        InstrumentId     Instrument{};
        Address          Value{};
    };

    //
    // Which instruments have a pool at all -- the compile-time half, and the
    // only half there can be.
    //
    // Only the ids, deliberately, and this is the same split hal::detail::
    // InstrumentWiringKey already makes for the same reason: hal::Address
    // holds a std::string_view, whose data members are private on this
    // standard library, so it is not a structural type and a
    // std::vector<PoolEntry> cannot be promoted into a genuine compile-time
    // array by std::define_static_array(). An InstrumentId is an enum and
    // needs none of that care.
    //
    // "Which rows have a pool" is all the compile-time half has to answer --
    // see hal::bindAddresses()'s veto -- so the addresses themselves stay in
    // the ordinary runtime table below, exactly as a Path stays out of
    // ConnectorWiringKey and only ever lives in hal::connectorWiring.
    //
    consteval auto hasPool( InstrumentId instrument) -> bool;

    //
    // This row's candidates, in table order, or empty for a row with no pool.
    //
    [[nodiscard]]
    auto poolFor( InstrumentId instrument) -> std::vector<Address>;

    //
    // What the named site says this instrument's address is, or nothing if
    // that site does not name this row. A site that names only the two rows
    // which actually differ between benches is the normal case and not a gap:
    // every other row falls through to its own column, which is where an
    // address identical on every bench belongs.
    //
    [[nodiscard]]
    auto siteAddressFor( std::string_view site, InstrumentId instrument) -> std::optional<Address>;

    //
    // Every site this deployment declares, in table order and without
    // duplicates -- for the diagnostic when somebody asks for one that is not
    // there, and to answer "is there a site table at all".
    //
    [[nodiscard]]
    auto siteNames() -> std::vector<std::string_view>;

    namespace detail
    {
        [[nodiscard]]
        consteval auto instrumentsOf( const std::vector<PoolEntry> & entries) -> std::vector<InstrumentId>
        {
            std::vector<InstrumentId> instruments;

            for( const auto & entry : entries)
            {
                instruments.push_back( entry.Instrument);
            }

            return instruments;
        }
    } // namespace detail
} // namespace hal

//
// ADDRESS_POOLS / POOL / END_ADDRESS_POOLS, and SITES / SITE / END_SITES:
// the two declarative tables, in the shape every other table in this codebase
// uses -- see rig/wiring.inc for the sibling this is modelled on, and
// dev/rig/pools.inc for a filled-in one.
//
// One row per candidate with the id repeated, rather than a list nested inside
// one entry: that is what keeps the table greppable for "which rows can end up
// on dev-dmm-2", and it is the shape WIRE_INSTRUMENT already has for an
// instrument with several wires.
//
// The address is written unqualified (Lan( "dev-dmm-1"), not
// hal::Lan( ...)) and qualified in the expansion, exactly as the third column
// of instrument.inc is, so the table stays free of namespace noise.
// Parentheses rather than braces for the same preprocessor reason
// active_instruments.hpp gives: braces are not grouping to the preprocessor,
// so Lan{ "host", 5025 } would arrive as two macro arguments.
//
#define ADDRESS_POOLS                                                           \
    namespace hal { namespace detail {                                          \
    constexpr auto buildPoolEntries() -> std::vector<PoolEntry>                 \
    {                                                                            \
        std::vector<PoolEntry> entries;

#define POOL( instrument, address) \
        entries.push_back( PoolEntry{ InstrumentId::instrument, hal::address });

#define END_ADDRESS_POOLS                                                       \
        return entries;                                                         \
    }                                                                            \
    inline constexpr auto pooledInstruments =                                    \
        std::define_static_array( instrumentsOf( buildPoolEntries()));           \
    inline const std::vector<PoolEntry> poolEntries = buildPoolEntries();        \
    } /* namespace detail */                                                     \
    consteval auto hasPool( const InstrumentId instrument) -> bool               \
    {                                                                            \
        for( const auto pooled : detail::pooledInstruments)                      \
        {                                                                        \
            if( pooled == instrument)                                            \
            {                                                                    \
                return true;                                                     \
            }                                                                    \
        }                                                                        \
        return false;                                                            \
    }                                                                            \
    }

#define SITES                                                                   \
    namespace hal { namespace detail {                                          \
    inline auto buildSiteEntries() -> std::vector<SiteEntry>                    \
    {                                                                            \
        std::vector<SiteEntry> entries;

#define SITE( site, instrument, address) \
        entries.push_back( SiteEntry{ #site, InstrumentId::instrument, hal::address });

#define END_SITES                                                               \
        return entries;                                                         \
    }                                                                            \
    inline const std::vector<SiteEntry> siteEntries = buildSiteEntries();        \
    } /* namespace detail */                                                     \
    }

#include THORIUM_POOL_TABLE
#include THORIUM_SITE_TABLE

//
// The three lookups, defined after the tables above have expanded because the
// wrappers read the entry vectors those expansions declare.
//
// Each is two functions: a pure one taking the entries, and a wrapper naming
// this deployment's. The split is what makes the semantics testable at all --
// which row a site falls back from, what order candidates come out in,
// whether a repeated site name is listed twice -- without a test having to be
// a whole deployment with tables of its own. The wrappers live in
// src/topology/address_tables.cpp rather than here so that there is exactly
// one definition of each in a program: an inline wrapper would be compiled
// once per translation unit, and a translation unit that defined
// THORIUM_POOL_TABLE differently from the library's would be an ODR violation
// the linker resolves by silently picking one.
//
namespace hal
{
    namespace detail
    {
        [[nodiscard]]
        auto poolIn( const std::vector<PoolEntry> & entries, InstrumentId instrument) -> std::vector<Address>;

        [[nodiscard]]
        auto siteAddressIn(
            const std::vector<SiteEntry> & entries,
            std::string_view               site,
            InstrumentId                   instrument) -> std::optional<Address>;

        [[nodiscard]]
        auto siteNamesIn( const std::vector<SiteEntry> & entries) -> std::vector<std::string_view>;
    } // namespace detail
} // namespace hal
