#include "hal/safing.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/instrument.hpp"

#include <concepts>
#include <meta>
#include <vector>

namespace hal
{
    namespace detail
    {
        consteval auto membersOfInfo( std::meta::info scope) -> std::vector<std::meta::info>
        {
            return std::meta::members_of( scope, std::meta::access_context::current());
        }

        template<std::meta::info Scope>
        constexpr auto members = std::define_static_array( membersOfInfo( Scope));
    } // namespace detail

    //
    // The safing pass itself. The instrument list is not written out here --
    // it's whichever global variables in this translation unit are
    // InstrumentTag-derived (see hal/instrument.hpp's own comment on that
    // marker), reflected over rather than hand-maintained: a hand-maintained
    // list of instruments to safe would be a second copy of rig/instrument.inc,
    // and the failure mode of a second copy is that it silently falls behind
    // the first. An instrument added to the rig (rig/active_instruments.hpp,
    // reached via THORIUM_ACTIVE_INSTRUMENTS above) is safed because it was
    // added to the rig, not because somebody also remembered to add it here
    // -- and, unlike the X-macro-redefinition version this replaced, that
    // holds even for an instrument global declared some other way entirely,
    // as long as its type derives from InstrumentTag; there is no second
    // .inc read to fall out of sync with in the first place.
    //
    // ^^:: is the global namespace -- rig/active_instruments.hpp deliberately
    // declares Dmm1/Dmm2/Osc1/DcP1..DcP4/AcP1 unqualified there (see that
    // file's own comment on why), so that's where they live to reflect over.
    // Filtering on "derives from InstrumentTag" rather than "has a safe()
    // member" is deliberate too: the latter would sweep in any unrelated
    // global that happened to also spell a method safe(), where the former
    // only ever matches a type that explicitly opted in.
    //
    // The static_assert is what turns "this driver forgot safe()" into a
    // one-line diagnostic naming the instrument's type, rather than the
    // no-member-named-safe error the call below would produce on its own.
    // See hal::SafeableInstrument in hal/instrument.hpp, and
    // hal::L4411A::safe() for why every driver must have one rather than
    // only the sources that need a real body.
    //
    auto safeRig() -> void
    {
        //
        // Sources first, relays after -- and note that this ordering needs
        // no table and no sort, because it isn't an ordering *within* this
        // loop at all. Every instrument's safe() runs here, in whatever
        // order this reflects the global namespace's declarations in; the
        // fabric is opened once, below, after all of them. The passive
        // instruments' safe() calls are empty bodies that fold away
        // entirely (see hal::L4411A::safe()), so what actually reaches the
        // hardware is exactly "every output off, then every relay open"
        // regardless of that order or what is added later.
        //
        // This is why there is no SAFING_ORDER table alongside
        // hal/wiring.inc: the one ordering constraint that matters is
        // structural here, so a table would exist only to be kept in sync
        // with something that cannot drift. If some future instrument turns
        // out to need to go strictly first or last relative to its peers --
        // a real constraint this loop cannot express -- that is when an
        // explicit order earns its place.
        //
        template for( constexpr auto member : detail::members<^^::>)
        {
            if constexpr( std::meta::is_variable( member))
            {
                using InstrumentT = [: std::meta::type_of( member) :];

                if constexpr( std::derived_from<InstrumentT, InstrumentTag>)
                {
                    static_assert( SafeableInstrument<InstrumentT>,
                        "every InstrumentTag-derived instrument needs a safe() member --"
                        " see hal::SafeableInstrument in hal/instrument.hpp");

                    [: member :].safe();
                }
            }
        }

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
