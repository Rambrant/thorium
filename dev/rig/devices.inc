// ReSharper disable once CppMissingIncludeGuard

//
// The dev bench's switching hardware: none.
//
// An empty table rather than a missing one, for the reason hal/wiring.hpp
// gives for the empty SOURCE_WIRING table: the readers are declared
// unconditionally and defined only by this expansion, so omitting the file
// would make a translation unit that asks "which cards are there" fail to
// link rather than answer "none".
//
// This is the file that says what kind of deployment this is. A desk with a
// meter on it has no matrix, no mux and no relay cards, so hal::SwitchDeviceId
// has no enumerators, HOP( ...) can name nothing, and dev/rig/wiring.inc has
// nothing it could route through -- which is consistent all the way down
// rather than a gap: with no cards there are no routes, so every reading here
// is an instrument readback (see core::MeasureEngine's point-free operator()).
//
// What that costs is real and worth stating: the routed half of the framework
// -- path composition, hal::SwitchFabric, the electrical interlock -- is not
// exercised by this deployment at all. One real card on the desk would buy all
// of it back, as one row here plus one row in each wiring block. See
// dev/README.md.
//
SWITCH_DEVICES
END_SWITCH_DEVICES
