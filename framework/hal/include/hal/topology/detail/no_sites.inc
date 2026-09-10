// ReSharper disable once CppMissingIncludeGuard

//
// The fallback site table, used when a deployment supplies no
// THORIUM_SITE_TABLE -- which is every deployment that is one bench rather
// than one of a fleet of identical ones.
//
// Empty for the reason no_pools.inc beside it is: a rig with no site table
// has every row's address in its own instrument.inc column, so nothing ever
// looks a site up. What is worth noting is what this does NOT mean -- it is
// not "the site is unknown". A deployment with no site table has no sites,
// and hal::bindAddresses() rejects a --site given to it rather than silently
// resolving nothing, because a bench PC configured for a fleet it is not part
// of is a mistake worth hearing about at startup.
//
SITES
END_SITES
