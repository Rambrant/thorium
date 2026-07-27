#include "hal/safing.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/instrument.hpp"

namespace hal
{
    //
    // The safing pass itself. The instrument list is not written out here
    // -- it is the linking rig's instrument.inc (rig/instrument.inc in this
    // repo, reached via THORIUM_INSTRUMENT_TABLE -- see
    // hal/instrument.hpp's own comment on THORIUM_INSTRUMENT_IDS for why
    // this file, being generic hal code shared by every rig, can't name any
    // one rig's instrument.inc directly), re-expanded a second time with
    // INSTRUMENT redefined to call safe() instead of declaring a global.
    // rig/active_instruments.hpp already expanded the same file once, with
    // the declaring definition; this is the same X-macro-with-two-meanings
    // pattern the criteria variants use for their parity checks, and for
    // the same reason: a hand-maintained list of instruments to safe would
    // be a second copy of instrument.inc, and the failure mode of a second
    // copy is that it silently falls behind the first. An instrument added
    // to the rig is safed because it was added to the rig, not because
    // somebody also remembered to add it here.
    //
    // push_macro/pop_macro rather than a bare #undef: INSTRUMENT is left
    // defined by hal/active_instruments.hpp (it's a name test scripts and
    // .inc content use, not private plumbing), so this file borrows it and
    // puts it back exactly as it found it, leaving nothing for a later
    // include in this same TU to trip over.
    //
    // The static_assert is what turns "this driver forgot safe()" into a
    // one-line diagnostic naming the instrument, rather than the
    // no-member-named-safe error that the call below would produce on its
    // own -- readable enough for one instrument, much less so once it's
    // wrapped in macro expansion. See hal::SafeableInstrument in
    // hal/instrument.hpp, and hal::L4411A::safe() for why every driver
    // must have one rather than only the sources that need a real body.
    //
    // type/id/__VA_ARGS__ are accepted and ignored: this expansion needs
    // only the instance name. The signature has to match the declaring
    // definition regardless, since the same .inc feeds both.
    //
    auto safeRig() -> void
    {
#pragma push_macro( "INSTRUMENT")
#undef INSTRUMENT

#define INSTRUMENT( type, name, id, ...)                                        \
        static_assert( SafeableInstrument< decltype( name)>,                     \
            "every instrument in hal/instrument.inc needs a safe() member --"    \
            " see hal::SafeableInstrument in hal/instrument.hpp");               \
        name.safe();

        //
        // Sources first, relays after -- and note that this ordering needs
        // no table and no sort, because it isn't an ordering *within* this
        // expansion at all. Every instrument's safe() runs here, in
        // whatever order instrument.inc happens to list them; the fabric is
        // opened once, below, after all of them. The passive instruments'
        // safe() calls are empty bodies that fold away entirely (see
        // hal::L4411A::safe()), so what actually reaches the hardware is
        // exactly "every output off, then every relay open" regardless of
        // how instrument.inc is arranged or what is added to it later.
        //
        // This is why there is no SAFING_ORDER table alongside
        // hal/wiring.inc: the one ordering constraint that matters is
        // structural here, so a table would exist only to be kept in sync
        // with something that cannot drift. If some future instrument
        // turns out to need to go strictly first or last relative to its
        // peers -- a real constraint this file cannot express -- that is
        // when an explicit order earns its place.
        //
#include THORIUM_INSTRUMENT_TABLE

#pragma pop_macro( "INSTRUMENT")

        //
        // Everything, not a computed set of paths: openAll() clears the
        // fabric's use counts outright rather than walking the wiring
        // tables to work out which elements a dead test might have left
        // closed. Deliberate -- see hal/safing.hpp on why this function
        // reads no state. A reference-counted disconnect() of a path
        // nobody is left holding could leave a relay closed on a use count
        // some crashed script never released, which is exactly the
        // outcome safing exists to prevent.
        //
        fabric.openAll();
    }
} // namespace hal
