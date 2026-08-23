#pragma once

namespace core
{
    //
    // Whether there is anything at the other end of the verbs.
    //
    // ---------------------------------------------------------------------
    // The hole this closes
    // ---------------------------------------------------------------------
    //
    // Half of this framework's verbs observe -- Measure, Read, Await, Fetch --
    // and those have had an answer to "where did this come from" since the
    // beginning: core::ISession, and a run started with --replay takes every
    // reading from a file without an instrument being asked (the driver call
    // lives inside the liveRead callback, and a scripted session simply never
    // invokes it; see core::MeasureEngine, where the fabric work is inside that
    // same callback for the same reason).
    //
    // The other half instruct -- Apply, Remove, Setup, Write, Arm, Connect,
    // Disconnect -- and had no such answer. They call their driver
    // unconditionally, because there is no value coming back for a session to
    // substitute, so a replayed run took its readings from a file and then went
    // on energising rails and closing relays for real. That was survivable only
    // because every driver in this repo is still simulated: the day one of them
    // opens a VISA session, --replay at a desk stops being a desk operation.
    //
    // This is the missing half. An instruction reaches an instrument when a
    // bench is attached, and does not when one is not; the log says which, so a
    // run that instructed nothing cannot be read as one that did.
    //
    // ---------------------------------------------------------------------
    // Why a global, when core::SessionBank deliberately is not one
    // ---------------------------------------------------------------------
    //
    // The bank's argument for being an ordinary object is that it is reached by
    // exactly the verbs that read, all of which are constructed in one place,
    // so a unit test can own one instead of contending for shared state. That
    // argument does not transfer: this is reached by *every* verb, including
    // the four engines that hold no state at all and are constructed as
    // ApplyEngine{}, and threading a reference into all of them would put an
    // initialisation-order dependency between translation units in the way of
    // a single bool.
    //
    // The journal's argument does transfer, verbatim: every verb in the
    // framework reaches it, and a test script takes no parameters at all, so
    // there is nothing to carry one through. See core::journal().
    //
    // ---------------------------------------------------------------------
    // Why this is NOT the same fact as "readings are injected"
    // ---------------------------------------------------------------------
    //
    // Deliberately separate from core::SessionBank's state, which is the
    // tempting simplification and is wrong. A script unit test injects its
    // readings *and* wants Apply and Connect to reach their drivers -- the
    // simulated instrument state and the fabric are precisely what such a test
    // then asserts on (see libs/hal/tests, rig/tests). Deriving detachment from
    // injection would silence exactly the calls those tests exist to observe.
    //
    // So: injecting is about where a value comes from, attachment is about
    // whether an instruction goes anywhere, and only a caller who knows the
    // whole run -- app/src/main.cpp -- is in a position to say the second one.
    // It detaches for --replay, --inject and --skeleton, all three of which
    // mean the readings did not come from this rig; a run whose readings are
    // fiction must not be driving hardware.
    //
    // Attached by default, which is the overwhelmingly common case and the safe
    // direction for the default to be wrong in: a run that instructed the rig
    // when it need not have is a wasted relay cycle, where one that silently
    // did not is a green result about a DUT nothing ever powered.
    //
    class Bench
    {
        public:
            [[nodiscard]]
            auto isAttached() const -> bool { return mAttached; }

            auto attach() -> void { mAttached = true;  }
            auto detach() -> void { mAttached = false; }

        private:
            bool mAttached{ true };
    };

    //
    // The one bench a process runs against -- see this file's own comment on
    // why it is global where a session bank is not.
    //
    [[nodiscard]]
    auto bench() -> Bench &;

    //
    // What a stimulus event's Detail says when nothing was sent.
    //
    // Posted rather than suppressed, and that is the point. A log that simply
    // omitted the instruction would be missing the sequence -- and "a routing
    // step omitted for brevity is exactly the one that explains a failed
    // reading" is the argument core::SarifSink already makes for carrying every
    // verb. A log that posted it unmarked would be worse still: it would state
    // that 24 V went onto a rail that was never energised.
    //
    // It costs the human report nothing, which is worth noticing rather than
    // being lucky about: that report shows only the verbs that observed
    // something (see isHumanRelevant in core/src/report.cpp), so this marker
    // appears exclusively in the machine log, where per-event precision is the
    // whole purpose.
    //
    inline constexpr const char * kDetachedDetail = "not performed -- no bench attached";
} // namespace core
