#include "hal/verbs/interlock.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/driver/instrument.hpp"

#include <concepts>
#include <meta>
#include <vector>

#include "hal/topology/wiring.hpp"

//
// The two rig facts this answer is composed from, and the reason this file is
// in hal_rig rather than hal: which pin each source is cabled onto (the wiring
// table, via hal::sourceWiring) and whether that source is energised right now
// (the instrument globals, via THORIUM_ACTIVE_INSTRUMENTS above). Neither is
// visible outside a translation unit that expands them -- see hal/src/verbs/measure.cpp
// and hal/src/verbs/route.cpp, which are the same shape for the same reason.
//
#include THORIUM_WIRING_TABLE

namespace hal
{
    namespace
    {
        //
        // In an unnamed namespace, unlike hal/src/verbs/safing.cpp's identical pair
        // in hal::detail: two translation units in one library defining the
        // same namespace-scope consteval function is an ODR question nobody
        // should have to think about, and this file has no reason to export
        // them.
        //
        consteval auto membersOfInfo( std::meta::info scope) -> std::vector<std::meta::info>
        {
            return std::meta::members_of( scope, std::meta::access_context::current());
        }

        template<std::meta::info Scope>
        constexpr auto members = std::define_static_array( membersOfInfo( Scope));

        //
        // "This driver has an output that can be turned off", which is the
        // same thing as "this driver has an output at all" -- every source in
        // this repo pairs applyOutput with removeOutput, and a source that
        // could be energised but not de-energised would be a bug of its own.
        //
        // Matched on removeOutput rather than applyOutput because that one
        // takes no arguments in every driver that has it, so the requirement
        // states itself without a plausible-looking argument list standing in
        // for a real signature (hal::keysight_ac6834b::Ac6834B overloads applyOutput twice --
        // per-phase and symmetric -- and a requires-expression naming one of
        // them would be asserting something narrower than it appears to).
        //
        template<typename InstrumentT>
        concept EnergisableInstrument = requires( InstrumentT & instrument)
        {
            instrument.removeOutput();
        };

        //
        // What the walk below needs of an energisable instrument: which id it
        // is, and whether it is live.
        //
        template<typename InstrumentT>
        concept LiveReportingInstrument = requires( const InstrumentT & instrument)
        {
            { instrument.id()        } -> std::convertible_to<InstrumentId>;
            { instrument.isEnabled() } -> std::convertible_to<bool>;
        };
    } // namespace

    //
    // Reflected over rather than written out, exactly as hal::safeRig() is and
    // for the same reason: a hand-maintained list of "the sources the interlock
    // knows how to ask" would be a second copy of rig/instrument.inc, and the
    // failure mode of a second copy is that it silently falls behind the first.
    // Here that failure mode is worse than safing's -- an interlock that has
    // quietly stopped covering a rail reports every route to it as safe.
    //
    // ^^:: is the global namespace, which is where rig/active_instruments.hpp
    // declares the instrument globals; see hal/src/verbs/safing.cpp's own comment on
    // why they live there and why the filter is "derives from InstrumentTag"
    // rather than "has the member I am about to call".
    //
    auto energisedSourceAt( const VpcLocation location) -> std::string
    {
        //
        // Asked first, and non-throwing on purpose (see
        // hal::SourceWiring::findLanding): most pins have no source cabled
        // onto them and that is the ordinary answer, not an error. It is also
        // the cheap half -- a walk of a handful of table rows, against a
        // reflection over every global in the program below.
        //
        const auto landing = sourceWiring.findLanding( location);

        if( !landing)
        {
            return {};
        }

        template for( constexpr auto member : members<^^::>)
        {
            if constexpr( std::meta::is_variable( member))
            {
                using InstrumentT = [: std::meta::type_of( member) :];

                if constexpr( std::derived_from<InstrumentT, InstrumentTag>)
                {
                    //
                    // Only instruments with an output are asked, and every
                    // instrument with an output *must* be answerable -- which
                    // is the whole point of splitting these two conditions.
                    //
                    // A single `if constexpr( LiveReportingInstrument<...>)`
                    // would read the same and be silently wrong: a new supply
                    // driver whose author forgot isEnabled() would fail the
                    // concept, be skipped, and make this function report a live
                    // rail as cold -- a safety check disappearing on exactly
                    // the instrument it exists for. The assertion turns that
                    // into a one-line build failure naming the type instead,
                    // the same trick hal::SafeableInstrument plays on a driver
                    // that forgot safe().
                    //
                    if constexpr( EnergisableInstrument<InstrumentT>)
                    {
                        static_assert( LiveReportingInstrument<InstrumentT>,
                            "an instrument with an output to energise must also report id() and"
                            " isEnabled() -- the electrical interlock asks every source whether it"
                            " is live before a low-impedance reading is routed to its landing pin;"
                            " see hal/verbs/interlock.hpp");

                        if( [: member :].id() == *landing && [: member :].isEnabled())
                        {
                            //
                            // The landing id, not the global's own -- they are
                            // equal on this branch, and taking it from the
                            // wiring table keeps the answer in the terms the
                            // question was asked in.
                            //
                            return std::string( to_string( *landing));
                        }
                    }
                }
            }
        }

        //
        // A source lands here and it is not energised. The commonest way to
        // reach this line is the correct bench sequence -- Remove the rail,
        // then measure it -- which is exactly the case that must not be
        // refused.
        //
        return {};
    }
} // namespace hal
