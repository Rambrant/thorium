#pragma once

#include <optional>
#include <type_traits>
#include <utility>

#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"

namespace hal
{
    //
    // Remember how a source was programmed, and put it back afterwards -- the
    // mechanism behind a SETUP/TEARDOWN pair that brackets a group whose test
    // takes a rail away (see suite/scripts/transient_bracket.cpp and
    // capacitance_bracket.cpp, which are now two wrappers around this).
    //
    // What there is to remember, and how to program it again, are the driver's
    // to say: it supplies three ADL hooks beside its applyDriver, found in its
    // own namespace --
    //
    //     snapshotDriver( const Source &)                 -> its state value
    //     restoreDriver ( Source &, const State &)        -> a builder, programmed
    //     portDriver    ( Source &)                       -> a builder, bare
    //
    // The bare one is what Connect is given, as it is in a script (Connect(
    // DcP7.dc())): a Connect is journalled with its config's settings, and
    // handing it the programmed builder would log a voltage the Connect did not
    // set. Everything else is the same for every source and lives here, once:
    //
    //   * The teardown runs whether or not the test did. The runner constructs
    //     the group's teardown guard before the group's setup, so a restore
    //     happens on the paths where the script returned early, failed, or never
    //     started. Hence restore() on a source nothing was captured from does
    //     nothing -- "reached with nothing recorded" has to have an answer.
    //
    //   * The values put back are the ones actually found, not a second copy of
    //     the power-up sequence's literals. They are written once, there.
    //
    //   * Nothing is restored unless something took it away. Three conditions
    //     all have to hold: it was captured, it was delivering when captured,
    //     and it is not delivering now. The last keeps the fabric's use counts
    //     balanced -- hal::SwitchFabric counts closes against opens rather than
    //     holding a bool, so a Connect on a path nobody opened would leave the
    //     count one too high, invisibly on the first pass and cumulatively under
    //     --repeat. Asking the source whether it is still energised answers
    //     "did the script get as far as dropping it" without reaching into the
    //     fabric. The second keeps a detached bench (--replay, --inject,
    //     --skeleton, where no Apply reaches a driver) from having a teardown
    //     energise a source that was never on.
    //
    //   * Connect before Apply -- the relay closes on a dead path, then the
    //     output comes up (see core/verbs/source.hpp). Skipped, by constraint
    //     rather than by a runtime check, for a source with no relay of its own
    //     (a DirectWiring output), where there is nothing to close.
    //
    // What this deliberately does not do is take the source down. That is the
    // script's own precondition -- the stimulus it measures the effect of -- so
    // it belongs next to the reading, not in a hook that runs whether or not
    // anything was measured.
    //
    // One object per source, file-local in the translation unit that owns the
    // pair, so the only two functions that can reach it are the two whose
    // invariant it is.
    //
    template<typename Source>
    class SourceSnapshot
    {
        public:
            using State = std::remove_cvref_t<decltype( snapshotDriver( std::declval<const Source &>()))>;

            //
            // Per pass, not per run: --repeat=3 enters the setup three times, and
            // a state that accumulated across passes would have the second
            // teardown restoring what the first one had already put back.
            //
            auto capture( const Source & source) -> void
            {
                mHeld.reset();
                mHeld = Held{ snapshotDriver( source), source.isEnabled() };
            }

            //
            // Setpoints back to what capture() found, if and only if something
            // took the output away since. See the class comment for the three
            // conditions; returns whether it restored anything.
            //
            auto restore( Source & source) const -> bool
            {
                if( ! mHeld || ! mHeld->Energised || source.isEnabled())
                {
                    return false;
                }

                const auto restored = restoreDriver( source, mHeld->Value);

                if constexpr( requires( SwitchFabric & fabric, const InstrumentWiring & wiring, const ConnectorWiring & connector)
                              { connectDriver( fabric, wiring, connector, portDriver( source).config()); })
                {
                    Connect( portDriver( source));
                }

                Apply( restored);

                return true;
            }

        private:
            struct Held
            {
                State Value;
                bool  Energised;
            };

            std::optional<Held> mHeld{};
    };
} // namespace hal
