# hal/ -- rig hardware facts and the Measure singleton

This directory holds everything about the physical rig itself: the
instruments it has, the VPC90 connector array's coordinate system, the
matrix/mux switching fabric, this rig's fixed wiring, and the one `Measure`
object every test script measures through. None of it knows what "Device X"
is -- DUT-specific data lives under `libs/dut/` instead (see its README).

## Layout

```
libs/hal/
    include/hal/
        vpc_location.hpp   # VpcLocation/VpcRack -- the VPC90 coordinate system
        switch_fabric.hpp  # SwitchElementId, SwitchFabric (matrix/mux relay state)
        instrument.hpp     # InstrumentId, Oscilloscope/Dmm/PowerSupply types
        instruments.hpp    # Dmm1/Dmm2/Osc1/PowerSupply1/fabric -- this rig's instances
        wiring.hpp         # InstrumentWiring/ConnectorWiring + WIRE macros
        adapter.hpp        # ADAPTER/POINT/END_ADAPTER macros
        measure.hpp        # MeasureEngine alias + extern Measure
    wiring.inc             # this rig's fixed instrument/connector wiring
```

## Two static wiring facts, composed at measurement time

An instrument's matrix/mux channel is fixed regardless of which DUT pin is
being measured; a VPC connector pin's channel is fixed regardless of which
instrument is doing the measuring. `hal::InstrumentWiring` and
`hal::ConnectorWiring` (see `wiring.hpp`) store exactly those two
independent facts -- not one combined table keyed by (instrument, pin),
which would need an entry per *combination* even though the underlying
physical facts are only per instrument and per pin. `core::MeasureEngine`
(see `libs/core/include/core/measure.hpp`) composes the two into one
crosspoint command at the moment a measurement is actually taken.

`wiring.inc` -- at this directory's top level, the same convention
`libs/dut/*.inc` uses -- holds the actual data, built via
`INSTRUMENT_WIRING`/`WIRE_INSTRUMENT`/`END_INSTRUMENT_WIRING` and
`CONNECTOR_WIRING`/`WIRE_CONNECTOR`/`END_CONNECTOR_WIRING` (see
`wiring.hpp`'s own comment). Each rig has exactly one instance of each
table, unlike `ADAPTER` (named per DUT profile) or `CRITERIA` (several
groups per file), so these macros build one fixed, namespaced global
(`hal::instrumentWiring`/`hal::connectorWiring`) rather than taking a name.

## Adapter points are compile-time-typed

`ADAPTER`/`POINT`/`END_ADAPTER` (see `adapter.hpp`) mirror
`CRITERIA`/`CRIT`/`END_CRITERIA`: each `POINT` becomes a genuine
`static constexpr` member of the group struct, carrying its location and
quantity kind as template parameters on `core::AdapterPointTag<Loc, Kind>`
(see `libs/core/include/core/adapter.hpp`) rather than as runtime data.
That is what makes a misspelled point name a "no such member" compile
error, and a quantity mismatch an overload-resolution failure -- see
`libs/dut/README.md` for the concrete examples, since the actual DUT
profile data lives there, not here.

## Measure

`measure.hpp`/`measure.cpp` assemble the one `Measure` object every test
script calls (`Measure( Dmm1.voltage(), DeviceX_StdAdapter::Output5V)`):
`core::MeasureEngine` instantiated with `hal::SwitchFabric`,
`hal::InstrumentWiring`, and `hal::ConnectorWiring`. Unlike the design this
replaced, this has no dependency on anything under `dut/` at all -- not
even a textual `#include` -- since an `AdapterPointTag` now carries
everything `Measure` needs to know about a point in its own type; the DUT
profile (`dut::DeviceX_StdAdapter`) is only ever named at each individual
`Measure(...)` call site in a script, not baked into this instantiation.

## What's still a runtime check

`InstrumentWiring::find()`/`ConnectorWiring::find()` are still ordinary
runtime searches, throwing if an instrument or pin isn't wired -- even
though a point's location is now known at compile time, the *wiring table
lookup* itself hasn't been upgraded to a compile-time search yet. See the
`TODO(reflection)` comment in `wiring.hpp` for what that would look like.
