# hal::Racal1260

An RS232 port on a Racal 1260-series switching/instrumentation chassis, routed
to the DUT through the matrix. Header-only, `namespace hal`, included as
`"hal/racal1260.hpp"`.

Target: `hal_racal1260` / `Thorium::hal_racal1260`. Depends on `Thorium::hal`
only.

Reachable over `Serial` or `Gpib` — the one driver here whose set is wider than
one panel, because a matrix-routed RS232 port is either a port on the PC with
its conductors cabled into the matrix or a serial module in the switching
chassis commanded over that chassis's bus, and which this bench has is unknown
for the same reason the model name below is a placeholder. Narrow it when the
bench is known.

Either way, the framing this driver configures is what the port speaks *out* at
the DUT, not how the PC reaches it. Under the `Serial` arrangement those are the
same physical port, which is when that distinction is easiest to lose.

> **The model name is a placeholder.** This driver is modelled on how a
> matrix-routed serial resource behaves, not on a datasheet. The legacy test
> script it was reconstructed from named its serial resource only as
> `Rs.Normal Type=RS232`, which says what that ATE called it and nothing about
> which box provided it. Rename the directory, the class and the header when the
> bench is known — one directory per driver is exactly what makes that touch
> nothing but `rig/instrument.inc`.
>
> It is named after *a* model rather than `hal::Rs232Port` on purpose. That was
> the retired `hal::Dmm`/`hal::Oscilloscope` mistake: a serial driver's command
> set, framing limits and timeout semantics are as model-specific as any SCPI
> dialect, and a generic name promises an interchangeability no real driver has.

## The verbs it answers to

| Verb | ADL hook | What it does |
|---|---|---|
| `Connect` / `Disconnect` | `connectDriver` / `disconnectDriver` | closes/opens the whole route — its own channels **and** the destination interface's |
| `Setup` | `setupDriver` | baud rate, word length, parity, stop bits |
| `Write` | `writeDriver` | sends a `core::Bytes` payload |
| `Read` | `readDriver` | takes a payload back, through the session seam |

`Apply` and `Remove` are deliberately **absent**. A serial port has no output to
energise, so there is nothing `Apply( Ser1.rs232())` could mean — and with no
`applyDriver` to find, writing one is "no matching function" at compile time
rather than a call that silently does nothing. Same guarantee
`hal::SwitchableIsolation` gives `Connect` on a relay-less supply.

## A dialogue

```cpp
Connect( Ser1.rs232(), at( dut::Console));
Setup(   Ser1.rs232().baudRate( 9600).wordLength( 8)
                     .parity( Parity::None).stopBits( StopBits::One));

Write( Ser1.rs232(), "RD 30\r");

const auto reply = Read( Ser1.rs232().terminator( "\r").timeout( 500ms));

Verify( FS_Console_1::FS_Console_Ack, reply.before( "\r"));

Disconnect( Ser1.rs232(), at( dut::Console));
```

Three things in that sequence are worth knowing.

**The route is held open for the whole dialogue.** `Connect` closes it and
`Disconnect` opens it; `Setup`, `Write` and `Read` never touch the fabric. This
is the one real difference from `Measure`, which connects, reads and disconnects
within the single call — a reading is instantaneous and independent, where
dropping the path between a command and its acknowledgement would break the
exchange, and re-closing relays around every byte would wear them out for
nothing.

**`Connect` takes the interface, not a pin.** `dut::Console` is a `BUNDLE` of
three `LINE`s — transmit, receive, and the signal-ground return — and all three
are closed as one path. An RS232 console is not usable a wire at a time, so
connecting two of the three is not a degraded link, it is no link; making the
bundle the unit means a script cannot express the half-connected case at all.
Adding a line to the interface in `dut/adapter.inc` changes what `Connect`
closes without touching a single call site.

**The terminator is not stripped.** `.terminator( "\r")` says what a read stops
at; the terminator still comes back in the payload. Whether a reply ended the way
the protocol says is a thing a criterion may want to check, and a driver that
quietly removed the evidence would make "the DUT answered `OK`" and "the DUT
answered `OK` and then stopped talking" indistinguishable. Stripping it is
`reply.before( "\r")`, at the script's discretion.

## Framing is four settings, not a shorthand

`Parity` and `StopBits` are enums. `StopBits` in particular is not an `int`
because 1.5 stop bits is a real RS232 framing with nowhere to go in one, and a
driver that silently rounded it would misframe every word.

Every field of the config is optional and means "leave what is already
configured" — the same convention `core::MeasureSetup` uses on the sensing side.
So a `Setup` naming only the baud rate leaves the parity a previous `Setup`
chose, and the log reports the settings it was actually given rather than the
`9600 8N1` shorthand, which cannot express an unset field.

## Testing a script that talks to the DUT

`Read` goes through the same `core::SessionBank` as `Measure`, so a console
dialogue injects and replays exactly like a reading:

```cpp
Read.inject( "Ser1.Data", { "ACK\r", "0xF5\r" });

EXPECT_TRUE( consoleScript());
```

The key is `"<instrument>.Data"`, not the interface name: `Read` takes only the
builder, because `Connect` is what named the destination. Reading more times
than the test authored replies for throws rather than repeating the last one.

## Adding it to a rig

```
rig/instrument.inc          INSTRUMENT( Racal1260, Ser1)
rig/active_instruments.hpp  #include "hal/racal1260.hpp"
rig/wiring.inc              WIRE_INSTRUMENT rows for its own channels,
                            WIRE_CONNECTOR rows for the interface's pins
dut/adapter.inc             a BUNDLE for the interface it talks to
```

Both halves of the route are rig data and live nowhere in this directory. Note
that this driver needs `WIRE_CONNECTOR` rows where `hal::N6701A` and
`hal::Ac6834B` need `WIRE_SOURCE` ones: their outputs are cabled to a known pin,
where a console is switched onto the DUT and off again.

Which of this port's channels meets which DUT line is not asserted anywhere in
this driver, deliberately. A crosspoint matrix has no notion of transmit and
receive — it closes the crosspoints it is given, and which of them forms the
outbound path is a fact about how the bench was cabled.
