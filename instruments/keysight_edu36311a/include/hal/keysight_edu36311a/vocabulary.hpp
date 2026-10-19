#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/io/scpi.hpp"
#include "hal/topology/wiring.hpp"

//
// wiring tags, outputs, rating errors
//
// Part of hal/keysight_edu36311a.hpp -- include that, not this.
//

namespace hal::keysight_edu36311a
{
    //
    // -- Whether a rail's lead has an isolation relay in it -----------------
    //
    // Whether a given DC rail has a real isolation relay in its path at all,
    // or is wired straight to its VPC pin with nothing to switch. Tag types
    // rather than a runtime flag, and constrained through the concept below,
    // so that Connect( DcP5.dc()) on a relay-less rail is a compile error --
    // "no matching function" -- rather than a call that quietly closes
    // nothing.
    //
    // Deliberately the same shape and the same names as
    // the DirectWiring/RelayIsolated tags the retired N6701A driver carried,
    // which is where this shape came from -- and deliberately
    // *not* shared types: a driver package compiles against hal and nothing
    // else, so that it can be zipped and dropped into another rig on its own
    // (see instruments/README.md and this directory's CMakeLists.txt, where
    // the rule is enforced rather than described). Reaching into the other
    // driver's namespace for two one-line tags would trade that property for
    // two lines.
    //
    // Two drivers is not yet a reason to hoist them into hal, and the third
    // one is. If a third DC supply arrives wanting the same distinction, the
    // move to make is hal::DirectWiring/hal::RelayIsolated in
    // hal/driver/instrument.hpp with an API-version bump -- not a third copy
    // of this paragraph.
    //
    struct DirectWiring  { static constexpr bool HasRelay = false; };  // no relay -- hard-wired, nothing to Connect/Disconnect
    struct RelayIsolated { static constexpr bool HasRelay = true;  };  // has one -- Connect/Disconnect close/open it

    template<typename Isolation>
    concept SwitchableIsolation = Isolation::HasRelay;

    //
    // -- Which of the box's three outputs, and what it can do ---------------
    //
    // One tag per physical output, carrying the channel number the instrument
    // knows it by and the rating printed beside its terminals.
    //
    // Those two facts are one fact on this instrument, which is why they share
    // a tag rather than being a tag and a constructor argument. The N6701A
    // takes its channel as a constructor argument (see
    // the retired N6701A driver did exactly that, and was right to: that
    // mainframe had four
    // *slots*, any module can be in any of them, so which slot a channel is in
    // says nothing about what it can source. An EDU36311A has three outputs
    // built into one chassis -- output 1 is the 6 V / 5 A one and cannot be
    // anything else -- so a channel number and a rating supplied separately
    // would let a rig write down the combination the box does not have, and
    // nothing would object.
    //
    //   1   0 to 6 V, 0 to 5 A. The high-current output.
    //   2   0 to 30 V, 0 to 1 A.
    //   3   0 to 30 V, 0 to 1 A. Identical to output 2 on this model.
    //
    // Outputs 2 and 3 differ in their channel number and in nothing else, and
    // that is worth stating because it is where this model parts company from
    // the E36311A whose programming guide covers its SCPI: there, outputs 2
    // and 3 are a +25 V / -25 V *pair*, with a negative rail on channel 3 and
    // the output identifiers P6V/P25V/N25V to match. Here they are two
    // independent positive 30 V outputs. See the .cpp on why this driver
    // therefore addresses channels by number and never by name.
    //
    // MaxVolts/MaxAmps are the front-panel ratings, not the programming
    // limits. The instrument accepts a few percent above rating (the E36311A's
    // 6 V output programs to 6.18 V), and this driver deliberately does not
    // model that headroom -- see RatingExceeded below for what it does with
    // the difference and why.
    //
    struct Output1
    {
        static constexpr int              Channel  = 1;
        static constexpr double           MaxVolts = 6.0;
        static constexpr double           MaxAmps  = 5.0;
        static constexpr std::string_view Rating   = "6 V / 5 A";
    };

    struct Output2
    {
        static constexpr int              Channel  = 2;
        static constexpr double           MaxVolts = 30.0;
        static constexpr double           MaxAmps  = 1.0;
        static constexpr std::string_view Rating   = "30 V / 1 A";
    };

    struct Output3
    {
        static constexpr int              Channel  = 3;
        static constexpr double           MaxVolts = 30.0;
        static constexpr double           MaxAmps  = 1.0;
        static constexpr std::string_view Rating   = "30 V / 1 A";
    };

    //
    // What an Output tag has to say about itself. Its purpose is the
    // diagnostic: a tag missing a member instantiates EDU36311A's members one
    // by one and fails somewhere inside them, where this fails on the
    // declaration naming the tag.
    //
    template<typename OutputT>
    concept OutputChannel = requires
    {
        { OutputT::Channel  } -> std::convertible_to<int>;
        { OutputT::MaxVolts } -> std::convertible_to<double>;
        { OutputT::MaxAmps  } -> std::convertible_to<double>;
        { OutputT::Rating   } -> std::convertible_to<std::string_view>;
    };

    static_assert( OutputChannel<Output1> && OutputChannel<Output2> && OutputChannel<Output3>);

    //
    // Declared here rather than at the top of this namespace, where a forward
    // declaration would more naturally go, because it has to carry the same
    // constraint as its definition -- and the constraint is the concept above.
    //
    template<typename Output, typename Isolation>
        requires OutputChannel<Output>
    class DcSource;

    //
    // -- Asking an output for more than it has ------------------------------
    //
    // A script asked this output for a voltage or a current beyond its rating.
    // Thrown by Apply, before anything reaches the wire.
    //
    // Checked here rather than left to the instrument, and that is the one
    // place this driver does not simply relay the box's own judgement. An
    // attached EDU36311A refuses 24 V on output 1 by itself: -222 "Data out of
    // range", which arrives as an hal::io::ScpiFault naming the command, and
    // that would be a perfectly good report. A *simulated* one refuses
    // nothing, because there is nothing there to refuse -- so without this
    // check the same script passes in CI and fails on the bench, which is the
    // one asymmetry this framework cannot afford (see rig/instrument.inc on
    // what the address column now means, and hal::io::ScpiSession's own
    // comment on the silent-wrong-configuration failure this whole layer is
    // written against).
    //
    // So the rating is enforced in the driver, identically either way, and the
    // instrument's own few percent of programming headroom is deliberately
    // given up: 6.18 V is a number the E36311A accepts and no bench rail is
    // ever meant to sit at. A script that genuinely needs it wants a supply
    // with more range, not this one running over its badge.
    //
    // A logic_error rather than a runtime_error, because it is: nothing about
    // the bench changed, a script asked an instrument for something the
    // instrument is not. Not caught anywhere -- unlike
    // core::UnmeasurableReading, which core::MeasureEngine turns into a failed
    // criterion and carries on from, this is not a result, and a run that
    // continues past it is a run driving a rail nobody chose.
    //
    class RatingExceeded : public std::out_of_range
    {
        public:
            RatingExceeded( const std::string & instrument, const std::string_view setting,
                            const double asked, const double rated, const std::string_view unit) :
                std::out_of_range(
                    instrument + ": " + std::string( setting) + " of "
                    + io::ScpiSession::number( asked) + " " + std::string( unit)
                    + " is beyond this output's rating of " + io::ScpiSession::number( rated)
                    + " " + std::string( unit)
                    + " -- an EDU36311A output is 6 V / 5 A or 30 V / 1 A, see "
                      "instruments/keysight_edu36311a/README.md")
            {}
    };
} // namespace hal::keysight_edu36311a
