// ReSharper disable once CppMissingIncludeGuard

//
// The fallback pool table, used when a deployment supplies no
// THORIUM_POOL_TABLE -- which is every deployment that does not draw a row
// from a shelf of interchangeable instruments, including this repo's own
// bench.
//
// Empty, and unlike core/criteria/detail/no_criteria_variants.inc there is no
// placeholder in it. That difference is the point: an absent variant list
// still has to answer *something* when a traceability header asks which
// variant a run used, where an absent pool list answers a question nobody
// asks. A row with no pool never reaches the pool lookup at all --
// hal::bindAddresses() resolves it from the site table or from its own column
// and marks it hal::AddressSource::Table -- so "no pools" needs no
// representation beyond this file expanding to nothing.
//
// It exists only so that the #include in hal/topology/address_tables.hpp has
// a file to name; see that header on why this table is one of the two
// optional ones, and dev/rig/pools.inc for a real one.
//
ADDRESS_POOLS
END_ADDRESS_POOLS
