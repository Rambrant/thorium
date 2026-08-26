// ReSharper disable once CppMissingIncludeGuard

//
// The fallback variant list, used only when core is built without a criteria
// layer supplying THORIUM_CRITERIA_VARIANT_NAMES -- see
// core/src/criteria_variants.cpp on why the list is never empty.
//
// A deployment's real list is generated from THORIUM_KNOWN_CRITERIA_VARIANTS
// (cmake/CriteriaVariants.cmake) and never looks like this. "unknown" is the
// same obviously-absent placeholder core/src/journal.cpp uses for a
// traceability field it wasn't told: wrong and visibly so, rather than
// plausible.
//
"unknown",
