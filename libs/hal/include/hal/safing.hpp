#pragma once

namespace hal
{
    //
    // Drop this rig to a known idle state: every source's output off and
    // its setpoint zeroed, then every relay in the switching fabric
    // opened. Takes no arguments, reads no state, returns nothing to check.
    //
    // The contract is the unusual part, so it's worth stating plainly:
    // safeRig() is called when nobody knows what the rig was doing. Not at
    // the end of a test that finished, not by a script unwinding its own
    // steps -- but after a test process has already died, possibly
    // mid-measurement, possibly by signal, possibly with a corrupt heap.
    // Everything about the shape of this function follows from that:
    //
    //   * Unconditional. It does not ask which test was running, which
    //     instruments that test happened to touch, or what the fabric's
    //     current state is. There is no such information available at the
    //     point of call, which is exactly why there is no parameter to
    //     pass it through.
    //
    //   * Idempotent. Calling it twice, or on an already-idle rig, is
    //     indistinguishable from calling it once. A caller that isn't sure
    //     whether safing already happened should just call it.
    //
    //   * Ordered sources-before-relays. Disabling every output first and
    //     only then opening the fabric is not tidiness -- opening a relay
    //     while current is flowing through it is hot switching, which arcs,
    //     welds contacts, and destroys the relay. The ordering falls out
    //     of the implementation's structure rather than being maintained
    //     as a list; see hal/src/safing.cpp.
    //
    // What this is NOT is the rig's safety guarantee. It cannot be: any
    // mechanism that depends on software on this machine still running is
    // defeated by the failures that matter -- SIGKILL, a kernel panic, a
    // pulled power cord. The actual guarantee is the rig's hardware
    // watchdog, which drops the outputs on I/O inactivity with no help
    // from any process here. safeRig() exists so the rig goes idle
    // immediately on an abnormal exit rather than sitting energised for
    // however long the watchdog's timeout is. That makes it best-effort
    // convenience code by design, and means it does not need to be
    // provably complete to be correct -- a distinction worth keeping in
    // mind before adding anything to it that a safety argument would end
    // up resting on.
    //
    // Note who the intended caller is, because it constrains this
    // function's shape more than it first appears. The rig console is a
    // separate process that supervises a suite binary and cannot reach
    // into that binary's hal::fabric or its instrument objects -- so on an
    // abnormal child exit it re-invokes the same suite binary with
    // --safe (see app/src/main.cpp), which calls this and exits. A fresh
    // process is the right thing rather than a compromise: real
    // instruments are addressed over LAN/GPIB, so a newly started process
    // can command them to idle perfectly well, and it does so with a clean
    // heap rather than the damaged one that just crashed. This is also why
    // safing must not depend on any state the previous process
    // accumulated -- in the case that matters most, none of it exists any
    // more.
    //
    auto safeRig() -> void;

    //
    // RAII counterpart to safeRig(), for the *normal* ways a suite run
    // ends -- falling off the end of main, an early return, or an
    // exception propagating out of a test script (core::asQuantity throws
    // on a kind mismatch; see core/quantity_kind.hpp) -- as opposed to the
    // abnormal one safeRig() itself exists for: a supervising process
    // re-invoking this binary with --safe after the previous one is
    // already dead and can't be unwound at all (see app/src/main.cpp).
    //
    // A destructor rather than a call at every return point: main() has
    // several (--list-tests, --safe, an unselected --select, allPassed/
    // !allPassed, a caught exception), and safeRig() at each one is the
    // kind of list that silently falls behind the next exit added to
    // main() later. A guard runs on all of them by construction, the same
    // argument safeRig() itself makes for reading instrument.inc instead
    // of a hand-maintained instrument list.
    //
    // No deleted copy/move, deliberately -- not an oversight. A guard that
    // gets copied or destroyed more than once just calls safeRig() more
    // than once, which is exactly as harmless as calling it once; see
    // safeRig()'s own comment on idempotency above.
    //
    class RigSafingGuard
    {
    public:
        ~RigSafingGuard() { safeRig(); }
    };
} // namespace hal
