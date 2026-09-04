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
#include "hal/driver/api_version.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/io/scpi.hpp"
#include "hal/topology/wiring.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all (it travels separately from the hal it compiles against).
//
// Three, because this driver opens a real session and so needs hal/io/ -- the
// same number hal::keysight_edu34450a::EDU34450A and
// hal::keysight_dsox1202g::DSOX1202G ask for.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and here that matters more than usual:
// "36311A" alone does not even distinguish this supply from the E36311A whose
// programming guide the .cpp is written against, and the two differ in exactly
// the numbers a driver has to get right (see Output2 below).
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
    class EDU36311A;

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

    //
    // What a single Apply( DcP6.dc().voltage( ...).currentLimit( ...)) call
    // boils down to: which instrument, and whichever of the three settings
    // were actually named. A bare Remove( DcP6.dc()) leaves all three at
    // nullopt -- removeDriver below only ever reads Instrument.
    //
    // No Loc/AdapterPointTag here -- a DC rail's output is hard-cabled to one
    // VPC pin rather than routed through the fabric to whichever pin a script
    // names, so there was never a second point for at() to choose between (see
    // this file's comment on EDU36311A -- the retired N6701A driver's DcConfig
    // said the same thing for the same reason).
    //
    template<typename Output, typename Isolation>
    struct DcConfig
    {
        EDU36311A<Output, Isolation> &            Instrument;
        std::optional<core::quantities::Voltage>  Voltage{};
        std::optional<core::quantities::Current>  CurrentLimit{};
        std::optional<core::quantities::Voltage>  OverVoltage{};
    };

    //
    // The fluent chain a script builds up before handing it to Apply/Remove.
    // The copy-modify-return shape is hal::ConfigBuilder's (see
    // hal/driver/builder.hpp), so what is left here is one line per setting,
    // and a bare `DcP6.dc()` with no further calls is still a valid (if
    // underspecified) config, because every field defaults to unset.
    //
    template<typename Output, typename Isolation>
    class DcBuilder : public ConfigBuilder<DcBuilder<Output, Isolation>, DcConfig<Output, Isolation>>
    {
        public:
            using Config = DcConfig<Output, Isolation>;

            explicit DcBuilder( EDU36311A<Output, Isolation> & instrument) :
                DcBuilder::ConfigBuilder( Config{ instrument })
            {}

            [[nodiscard]]
            auto voltage( const core::quantities::Voltage v) const
            {
                return this->with( &Config::Voltage, v);
            }

            [[nodiscard]]
            auto currentLimit( const core::quantities::Current c) const
            {
                return this->with( &Config::CurrentLimit, c);
            }

            //
            // The level at which the instrument's own overvoltage protection
            // trips the output off -- SCPI VOLT:PROT, and hardware that acts
            // in under 5 ms whatever the PC is doing.
            //
            // Worth exposing where a current limit already is, because the two
            // protect against different things and only one of them is on by
            // default. A current limit stops the supply delivering more than
            // the rail should draw; OVP stops it delivering more *volts* than
            // the DUT can survive, which is the failure a mistyped setpoint or
            // a script pointed at the wrong rail actually produces. It is off
            // at *RST (the trip level sits at the output's maximum), so a
            // config that does not name it gets no protection beyond the
            // rating check above -- which is a decision about the DUT and
            // belongs in the script, not a default this driver invents.
            //
            // Not modelled here: the OCP subsystem (CURR:PROT), which is a
            // *trip* rather than the limit .currentLimit() sets -- it turns
            // the output off after a programmable delay in constant-current
            // mode instead of holding the rail there. That is a different
            // question about a DUT ("must never draw this much" against "must
            // not be given more than this"), and nothing on this bench asks it
            // yet.
            //
            [[nodiscard]]
            auto overVoltageProtection( const core::quantities::Voltage v) const
            {
                return this->with( &Config::OverVoltage, v);
            }
    };

    //
    // -- Everything this driver puts on the wire ----------------------------
    //
    // Declared here and defined in src/keysight_edu36311a.cpp, which is where
    // every command, every query and the whole ordering argument live, in one
    // ordinary translation unit a bench engineer can check against the
    // programming guide without reading a template.
    //
    // Free functions over (session, channel) rather than out-of-line members
    // of the class below, and that is not a style choice -- it is what a class
    // *template* forces. EDU36311A has two type parameters, so out-of-line
    // members would only link for the combinations the .cpp explicitly
    // instantiated, and the six aliases below are not the whole set a rig may
    // legitimately name: this driver promises that a new isolation tag needs
    // no change here (see SwitchableIsolation), and its tests hold it to that
    // with a tag it has never heard of. Such a tag cannot appear in an
    // explicit instantiation list that already exists.
    //
    // So the split lands where it always wanted to land anyway. Nothing about
    // talking to this supply depends on which output tag or isolation tag the
    // caller picked -- the channel number is an int by the time it gets here
    // -- and the class below is left holding only the glue: which branch,
    // simulated or attached, and what to remember about it.
    // hal::keysight_edu34450a::EDU34450A drew the same line and could afford
    // to draw it through a member, being a plain class.
    //
    namespace detail
    {
        //
        // Which of the two readback queries to send -- the whole of what
        // varies between them.
        //
        enum class Reading
        {
            Voltage,
            Current
        };

        //
        // A session on the supply at this address, not yet spoken to. See
        // EDU36311A::session() on why opening is lazy and why this is not done
        // in a constructor.
        //
        [[nodiscard]]
        auto openSession( const Address & address) -> std::unique_ptr<io::ScpiSession>;

        //
        // The once-per-session exchange: drain whatever the last user left in
        // the error queue, then ask *IDN? and refuse a model this driver is
        // not for.
        //
        auto prepare( io::ScpiSession & session) -> void;

        //
        // *IDN?, checked. What EDU36311A::identity() answers with.
        //
        [[nodiscard]]
        auto identify( io::ScpiSession & session) -> std::string;

        //
        // One Apply, on the wire: the settings that were named, in the order
        // that makes them safe, then the output on, then *OPC?. Values are in
        // volts and amps -- the units a SCPI argument is written in, which is
        // what core::quantities::Voltage::value() already holds.
        //
        auto program( io::ScpiSession & session, int channel,
                      std::optional<double> volts,
                      std::optional<double> amps,
                      std::optional<double> overVolts) -> void;

        // One Remove: the output off, the setpoint left alone.
        auto disableOutput( io::ScpiSession & session, int channel) -> void;

        [[nodiscard]]
        auto measure( io::ScpiSession & session, int channel, Reading reading) -> double;

        // OUTP? -- whether this output is live, asked of the instrument.
        [[nodiscard]]
        auto outputIsOn( io::ScpiSession & session, int channel) -> bool;

        //
        // What safe() says down an already-open session, swallowing a
        // transport error rather than propagating it -- see
        // EDU36311A::safe(), whose whole contract this carries out.
        //
        auto sendSafe( io::ScpiSession & session, int channel) -> void;
    } // namespace detail

    //
    // One output of a Keysight EDU36311A triple-output DC bench power supply:
    // 90 W in one chassis as 6 V / 5 A, 30 V / 1 A and 30 V / 1 A, each
    // independently programmable and independently switchable.
    //
    // One instance models one *output*, not the box. DcP5/DcP6/DcP7 (see
    // rig/instrument.inc) are three such instances behind one address, exactly
    // the way DcP1..DcP4 are four instances behind one N6701A mainframe
    // address: one box, three endpoints, so there is one address column
    // repeated three times and the endpoint is in the type rather than in the
    // address (see hal/driver/address.hpp on why, and Output1 above on why the
    // endpoint is in the *type* here where the N6701A has it as an argument).
    //
    // -- Smart Bench Essentials, and its sibling on this bench --------------
    //
    // The same product line as hal::keysight_edu34450a::EDU34450A: education-
    // market packaging of an instrument whose command set is the professional
    // model's. Its rear panel is the same story too -- gigabit LAN and a USB
    // device port, no GPIB connector and no option that adds one -- so the
    // constructor constraint below is the meter's.
    //
    // What it is *not* is a smaller E36311A, and this is the trap the model
    // number sets. The E36300 series programming guide is the document this
    // driver's SCPI comes from and is the right document; the E36311A it was
    // written for has outputs of 6 V / 5 A, +25 V / 1 A and -25 V / 1 A, one
    // of which is a *negative* rail with its own output identifier (N25V) and
    // its own MIN/MAX inversion. This model has two positive 30 V outputs
    // instead. Every number in that guide's range table is therefore wrong
    // here, and every command in the rest of it is right -- which is exactly
    // the split between Output1..Output3 above (this instrument's ratings,
    // from its own data sheet) and src/keysight_edu36311a.cpp (the series'
    // commands, from the guide).
    //
    // -- This driver talks, and it is the first source that does ------------
    //
    // hal/io/ arrived with a meter and grew a scope (see
    // instruments/keysight_edu34450a and instruments/keysight_dsox1202g). This
    // is the first *sourcing* driver on it, and the thing that makes a source
    // different from both is that its commands complete after they return: a
    // supply told to go to 24 V accepts the command in under 10 ms and reaches
    // the rail some tens of milliseconds later. So Apply ends with the *OPC?
    // that hal::io::ScpiSession::waitForComplete() was written for and that no
    // driver had yet used -- and *OPC? answers when the instrument has
    // finished processing, which is not the same as the rail having settled.
    // See applyOutput() for what that means for the line after an Apply.
    //
    // What the supply is told, in full, for one Apply of a 24 V rail on
    // output 2:
    //
    //     *IDN?                  once per session -- and refused if the model
    //                            is not this one (verifyIdentity)
    //     SYST:ERR?              once per session, until empty: whatever the
    //                            last user left queued is not ours
    //     CURR 0.5, (@2)         the limit first -- see applyOutput()
    //     SYST:ERR?              did it accept that
    //     VOLT:PROT 27, (@2)     then the trip level, before the setpoint it
    //                            has to be above
    //     SYST:ERR?
    //     VOLT 24, (@2)          then the setpoint
    //     SYST:ERR?
    //     OUTP 1, (@2)           and only then the output on
    //     SYST:ERR?
    //     *OPC?                  and wait until it has all been processed
    //
    // and for one reading of what that rail is actually doing:
    //
    //     MEAS:VOLT? (@2)
    //
    // Which one it does -- wire or simulation hooks -- is decided by the
    // address column of the rig's instrument table and by nothing else.
    // hal::Simulated means the hooks below are the instrument; hal::Lan means
    // the box at that hostname is. A script cannot tell the difference and no
    // script should have to (see isSimulated()).
    //
    // -- Isolation, and one real bench consequence -------------------------
    //
    // Isolation (DirectWiring/RelayIsolated above) says whether this rail's
    // lead has an isolation relay in it, as a template parameter rather than a
    // runtime flag, so that Connect/Disconnect exist only where there is
    // something for them to move.
    //
    // On this rig the answer is not the same for all three outputs, and the
    // reason is arithmetic rather than preference. The isolation relays live
    // on Spst1, a Racal 1260-18, whose relays are rated 2 A (see
    // rig/devices.inc). Outputs 2 and 3 are 1 A outputs and fit inside that
    // with room to spare. Output 1 is a 5 A output, and no relay in this rack
    // will carry it -- so its lead is cabled straight through, and its row
    // says DirectOutput1. That is the same distinction DcP1/DcP2 against
    // DcP3/DcP4 already records, arrived at from the current rating instead of
    // from history.
    //
    // -- What is deliberately not here -------------------------------------
    //
    // series/parallel coupling  outputs 2 and 3 can be strapped in series for
    //                           60 V or in parallel for 2 A, and the
    //                           instrument has commands for it
    //                           (OUTP:PAIR). Two of these instances would
    //                           then be one rail, which is not something one
    //                           InstrumentId per output can describe -- and it
    //                           is a cabling fact besides, so a rig that did
    //                           it would want one instance with a different
    //                           Output tag, not a mode on these.
    //
    // output sequencing         OUTP:DEL:RISE/FALL, which stage several
    //                           outputs coming up. Real and useful on a DUT
    //                           with a rail order, and a script already has
    //                           that ordering in it explicitly, one Apply per
    //                           line, where a reader can see it.
    //
    // data logging, front panel  INIT:DLOG, DISP:TEXT and the flash-drive
    //                           store. Nothing a test rig needs from an
    //                           instrument it is driving.
    //
    template<typename Output, typename Isolation>
        requires OutputChannel<Output>
    class EDU36311A : public InstrumentTag
    {
        public:
            //
            // LAN or USB, and nothing else -- what this model's back panel
            // has. No GPIB connector and no option that adds one, so a rig row
            // addressing it over GPIB fails to compile rather than failing to
            // open (see hal::ReachableOver in hal/driver/address.hpp).
            //
            // Note what this does not constrain: the *front*-panel USB port is
            // for a flash drive holding saved setups, and is not a way for the
            // PC to reach the instrument. hal::Usb here means the rear USBTMC
            // device port.
            //
            // One address for the whole chassis, and no channel argument: the
            // three outputs are three endpoints behind one interface, so all
            // three rows in a rig's instrument.inc carry the same address, and
            // which output a row is is the type column's business (see Output1
            // above).
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Lan, Usb>
            EDU36311A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            //
            // Where the PC reaches this supply -- and, since this driver has a
            // transport, the column that decides whether an Apply reaches
            // hardware or the simulation state below. See isSimulated().
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            // --- What this output is, as constants a caller can read ---

            [[nodiscard]]
            static constexpr auto channel() -> int
            {
                return Output::Channel;
            }

            [[nodiscard]]
            static constexpr auto maxVoltage() -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ Output::MaxVolts };
            }

            [[nodiscard]]
            static constexpr auto maxCurrent() -> core::quantities::Current
            {
                return core::quantities::Current{ Output::MaxAmps };
            }

            //
            // "6 V / 5 A" -- the badge beside the terminals, for the one place
            // it is genuinely needed: the message of a RatingExceeded, and a
            // journal line that should say which of three outputs was
            // programmed rather than only which InstrumentId it wears.
            //
            [[nodiscard]]
            static constexpr auto rating() -> std::string_view
            {
                return Output::Rating;
            }

            //
            // Whether there is nothing at the other end -- a hal::Simulated
            // address and no transport handed in.
            //
            // The one branch in this driver that decides between a socket and
            // the state below, and it is a property of the *address* rather
            // than a mode a caller sets: a rig says what it has once, in its
            // instrument table. An injected transport wins over a Simulated
            // address, deliberately -- see useTransport().
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return !mSession && std::holds_alternative<Simulated>( mAddress);
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address.
            //
            // Two callers, and neither is a script. A test hands in a fake and
            // asserts the command strings (see this driver's tests). A rig
            // with a VISA installation, or one reaching an instrument over a
            // bus hal::io::openTransport() does not implement, hands in its
            // own hal::io::ITransport and gets this driver working over it
            // with no change here.
            //
            // Replaces any session already open, closing it, and sends
            // nothing: the identity check and the error-queue drain belong to
            // the first *use* (see session()), so handing in a fake transport
            // is not a thing that can throw.
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mSession  = std::make_unique<io::ScpiSession>( std::move( transport));
                mPrepared = false;
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, and that is not an optimisation -- a rig's instruments
            // are globals constructed before main() (see
            // hal/topology/active_instruments.hpp), so a constructor that
            // opened a socket would make every binary that links the rig try
            // to reach the bench at static-initialisation time, and would
            // throw from a constructor with nowhere to catch it. So the
            // connection happens at the first Apply or reading that needs one,
            // which is also exactly when a detached run (--replay, --inject,
            // --skeleton) does not need one.
            //
            // Public, because a bring-up session on a desk wants it: a
            // deployment test can send this supply a command this driver has
            // no accessor for -- OUTP:PAIR, DISP:TEXT, the data logger --
            // without that becoming a reason to widen the driver. Throws
            // hal::io::TransportError if the supply cannot be reached, and
            // hal::io::ScpiFault if it answers *IDN? with a model this driver
            // is not for.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &
            {
                if( !mSession)
                {
                    //
                    // Opened from the address the rig table wrote down. A
                    // hal::Simulated address reaching here is a bug in this
                    // driver rather than in the table -- every caller checks
                    // isSimulated() first -- and hal::io::openTransport says
                    // exactly that in the exception it throws for one.
                    //
                    // hal::Usb routes to VISA (see hal/io/visa_transport.hpp),
                    // so a machine with no VISA installed fails here with "no
                    // VISA library found" rather than with a timeout, which is
                    // the difference between a missing dependency and a
                    // missing instrument.
                    //
                    mSession = detail::openSession( mAddress);
                }

                if( mPrepared)
                {
                    return *mSession;
                }

                //
                // The once-per-session exchange, here rather than beside the
                // construction above because a session arrives two ways --
                // opened from the address, or handed in by useTransport() --
                // and preparing it where it is *used* is what makes both go
                // through it.
                //
                // Not marked prepared until it has succeeded, so a supply that
                // failed its identity check is asked again on the next command
                // rather than being treated as verified. Which is the right
                // way round for a bench: the instrument that was off when the
                // run started may be on now, and the run has already failed
                // whatever it tried meanwhile.
                //
                detail::prepare( *mSession);

                mPrepared = true;

                return *mSession;
            }

            //
            // *IDN? -- "Keysight Technologies,EDU36311A,CN12345678,..." off the
            // real instrument, opening the session if it is not already open.
            //
            // Its own accessor rather than leaving callers to spell
            // session().identify(): what a run's traceability header should
            // carry about an instrument is what the instrument says it is, not
            // what the rig table hoped it was.
            //
            [[nodiscard]]
            auto identity() -> std::string
            {
                //
                // session() has already asked and already checked the answer,
                // so this asks again rather than caching it. One extra round
                // trip on a call nothing makes per reading, against a cached
                // string that would be a second thing to keep true if the
                // socket were ever reopened onto a different box.
                //
                return detail::identify( session());
            }

            //
            // Drop the session, closing the socket. The next Apply or reading
            // opens a new one. For a long-lived process that should not hold a
            // bench instrument between runs, and for recovering from a wedged
            // one -- on a raw socket, closing and reopening is the whole of
            // the recovery vocabulary. Not called by safe(): see there.
            //
            auto closeSession() -> void
            {
                mSession.reset();
                mPrepared = false;
            }

            //
            // No point argument -- this output is cabled to one pin, and which
            // pin is a rig fact (SOURCE_WIRING in rig/wiring.inc), never
            // something a script or this config carries. See this class's own
            // comment.
            //
            [[nodiscard]]
            auto dc() -> DcBuilder<Output, Isolation>
            {
                return DcBuilder<Output, Isolation>{ *this };
            }

            //
            // What this supply reports about its own output, over its own
            // interface -- not a routed measurement. MEAS:VOLT? and MEAS:CURR?
            // on the real instrument, which is how rail current is read on this
            // rig: the signal switching carries signals, and a rail at several
            // amps is not a signal.
            //
            // There is no at(...) at the call site and the fabric is never
            // touched -- see core::MeasureEngine's point-free operator().
            //
            //     Apply( DcP6.dc().voltage( 24_V).currentLimit( 0.5_A));
            //     const auto drawn = Measure( DcP6.measuredCurrent());
            //
            // Named "measured..." rather than voltage()/current()
            // deliberately: this instrument has both a setpoint and a reading
            // for each of those quantities (dc().voltage( 24_V) sets,
            // measuredVoltage() reads), and on a supply that distinction is
            // worth spelling out at every call site rather than leaving it to
            // be inferred from which verb it was passed to.
            //
            // Reading the rail at the *DUT* is a stricter check than either of
            // these, and the two are not interchangeable: a supply's own
            // readback cannot see cable drop or a bad contact. See
            // suite/scripts/rig_power_on.cpp, which spells that out.
            //
            [[nodiscard]]
            auto measuredVoltage() -> core::Port<core::quantities::Voltage, EDU36311A>
            {
                return core::Port<core::quantities::Voltage, EDU36311A>{ *this };
            }

            [[nodiscard]]
            auto measuredCurrent() -> core::Port<core::quantities::Current, EDU36311A>
            {
                return core::Port<core::quantities::Current, EDU36311A>{ *this };
            }

            //
            // The read a core::Port performs.
            //
            // core::MeasureSetup's fields are all ignored here, and for once
            // that needs no argument per field: a supply's readback is not
            // configurable. There is no range to pick (MEAS:VOLT? reads the
            // output on the range the output has), no integration time, no
            // trigger, and no threshold. What the meter's driver spends a
            // paragraph each on -- see
            // hal::keysight_edu34450a::EDU34450A::rawMeasure -- this
            // instrument simply does not have.
            //
            // A disabled output reads zero for both quantities rather than
            // reporting the last setpoint. On the wire that is just what the
            // instrument answers; simulated, it is arranged to be, because it
            // is the reading a script checking "is this rail really off"
            // depends on.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    if( isSimulated())
                    {
                        return mEnabled ? mOutputVoltage : core::quantities::Voltage{};
                    }

                    return core::quantities::Voltage{ detail::measure( session(), Output::Channel, detail::Reading::Voltage) };
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    if( isSimulated())
                    {
                        return mEnabled ? mSimOutputCurrent : core::quantities::Current{};
                    }

                    return core::quantities::Current{ detail::measure( session(), Output::Channel, detail::Reading::Current) };
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "EDU36311A reports only its output voltage and current");
                }
            }

            //
            // Drop this output to a known idle state, unconditionally -- see
            // hal::safeRig() in hal/verbs/safing.hpp for who calls this and
            // why it takes no arguments and reads no state. Not Remove(
            // DcP6.dc()) under another name: Remove is a test-script step,
            // addressed through a config and a builder chain, and reaching it
            // requires knowing which supply a script was in the middle of
            // driving. safe() is the opposite -- it is called when nobody
            // knows what was running.
            //
            // Zeroes the programmed setpoint as well as disabling the output,
            // deliberately. On the real instrument OUTP OFF leaves the voltage
            // setpoint where a test left it, so a supply safed at 24 V comes
            // back at 24 V the instant anything enables the output again -- a
            // front-panel press, a half-initialised driver, a reconnecting
            // console. Safing is meant to survive exactly that kind of
            // unattended re-enable.
            //
            // On an attached supply it says both of those on the wire, and
            // only down a session that is already open -- see the body below,
            // and instruments/README.md's rule, of which this instrument is
            // the most important case: a rail left on by a failed run is the
            // one thing on a bench that safing genuinely has to reach.
            //
            auto safe() -> void
            {
                mEnabled       = false;
                mOutputVoltage = core::quantities::Voltage{};

                //
                // mCurrentLimit and mOverVoltage are deliberately left as they
                // are rather than cleared: with the output off and the
                // setpoint at zero neither has anything to act on, and an
                // accidental re-enable is safer finding a stale limit and a
                // stale trip level still in place than finding none at all.
                //
                // On the wire, only down a session that is *already* open, and
                // never opening one. Safing runs when a script has already
                // failed, quite possibly because this supply is unreachable;
                // opening a session at that moment would replace the run's
                // real failure with a transport error from the cleanup path,
                // and hal::safeRig() does not catch (see
                // hal/src/verbs/safing.cpp), so it would also abandon the
                // safing of every instrument after this one.
                //
                // The session is deliberately not closed either: its error
                // queue is the best evidence of what went wrong, and dropping
                // it is closeSession()'s job and the caller's call.
                //
                if( mSession)
                {
                    detail::sendSafe( *mSession, Output::Channel);
                }
            }

            //
            // What an Apply does. Public because applyDriver below is a free
            // function found by ADL, not because a script should call it --
            // scripts go through Apply (see core/verbs/source.hpp).
            //
            // Programs the settings that were named, then enables the output;
            // on an attached supply, in that order and for the reason
            // detail::program gives (and see this class's own comment for the
            // full exchange). Throws RatingExceeded before anything is sent if
            // a setpoint is beyond this output's badge.
            //
            // Returns when the instrument has *processed* the commands, not
            // when the rail has arrived. This model's up-programming settling
            // time is about 80 ms to within 1% of the excursion, and its
            // down-programming time into no load is about 500 ms -- so a
            // script that removes a rail and reads it on the very next line
            // can legitimately read a rail still on its way down. That is a
            // property of the hardware rather than of this driver, and the
            // place to deal with it is the script, which is the only thing
            // that knows what it is waiting for.
            //
            auto applyOutput( const std::optional<core::quantities::Voltage> voltage,
                              const std::optional<core::quantities::Current> currentLimit,
                              const std::optional<core::quantities::Voltage> overVoltage) -> void
            {
                checkAgainstRating( voltage, currentLimit, overVoltage);

                //
                // Remembered before the wire, and remembered whether or not
                // there is a wire: these are what a journal line and a failure
                // message are written from (see describeConfig below), and an
                // Apply that threw on its way to the instrument still
                // happened as far as a report is concerned.
                //
                mOutputVoltage = voltage.value_or( core::quantities::Voltage{});
                mCurrentLimit  = currentLimit;
                mOverVoltage   = overVoltage;
                mEnabled       = true;

                if( isSimulated())
                {
                    return;
                }

                detail::program( session(), Output::Channel,
                                 voltage      ? std::optional{ voltage->value()      } : std::nullopt,
                                 currentLimit ? std::optional{ currentLimit->value() } : std::nullopt,
                                 overVoltage  ? std::optional{ overVoltage->value()  } : std::nullopt);
            }

            //
            // What a Remove does: the output off, and nothing else. The
            // setpoint is deliberately left where the script put it -- unlike
            // safe(), a Remove is a step in a script that knows what it is
            // doing and may well enable the same rail again two lines later.
            //
            auto removeOutput() -> void
            {
                mEnabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::disableOutput( session(), Output::Channel);
            }

            //
            // -- Two answers to "is this output live", and why there are two --
            //
            // Both questions are the electrical interlock's, and they are
            // asked from two different places with two different budgets.
            //
            // isEnabled() is what hal::energisedSourceAt() reads before a
            // low-impedance reading is routed to a pin a supply is cabled onto
            // (see hal/src/verbs/interlock.cpp, which reflects over every
            // instrument global in the program to find the one that lands
            // there). Its concept takes a *const* instrument, deliberately --
            // it is an observer on the measurement path -- so this reports what
            // this driver last commanded and performs no I/O.
            //
            // outputIsOn() asks the instrument, and is what isEnergised()
            // below uses: the Connect/Disconnect path, where a relay in this
            // rail's lead is about to move and the question is the difference
            // between a contact moving cold and one arcing. One round trip, on
            // a path that is about to actuate hardware either way, in exchange
            // for being right about the cases this process did not cause -- an
            // output left on by a previous run, switched on from the front
            // panel, or brought up by a coupled channel. A driver reading back
            // its own memory would call those cold and close a relay into
            // them.
            //
            // So the truthful-but-expensive answer is used exactly where
            // hardware is about to move, and the cheap one where a reading is
            // about to be taken. Making the measurement-side check ask the
            // instrument too is a change to that concept in hal rather than
            // something this driver can decide on its own, and the case for it
            // is weaker: routing a reading closes fabric relays, which the
            // Connect path has already been asked about.
            //
            // outputIsOn() is not const and not exception-free, for the same
            // reason: a supply that cannot be reached fails the Connect
            // instead of letting it proceed on an assumption.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto outputIsOn() -> bool
            {
                if( isSimulated())
                {
                    return mEnabled;
                }

                return detail::outputIsOn( session(), Output::Channel);
            }

            // --- Test/simulation hooks -- real hardware has no such setters ---

            //
            // What the simulated output is delivering -- the current a real
            // instrument would report back, which no setpoint determines. An
            // attached instrument never reads this; setting it on one is not an
            // error and does nothing, the same shape every other driver in
            // this tree has.
            //
            auto setSimulatedOutputCurrent( const core::quantities::Current c) -> void
            {
                mSimOutputCurrent = c;
            }

            //
            // The last thing this driver was told to do, whether or not there
            // was an instrument to tell. Kept on an attached supply as well --
            // it is what describeConfig's journal line and a failure message
            // are written from, and re-querying the instrument to render a log
            // entry would be a round trip to learn what this process just said.
            //
            // Which makes these the *setpoint*, never the reading. What the
            // rail is actually at is measuredVoltage(), and on an attached
            // supply the two can legitimately differ -- a current-limited
            // output sits below its voltage setpoint, which is the whole point
            // of a limit.
            //
            [[nodiscard]]
            auto outputVoltage() const -> core::quantities::Voltage
            {
                return mOutputVoltage;
            }

            [[nodiscard]]
            auto currentLimit() const -> std::optional<core::quantities::Current>
            {
                return mCurrentLimit;
            }

            [[nodiscard]]
            auto overVoltageProtection() const -> std::optional<core::quantities::Voltage>
            {
                return mOverVoltage;
            }

        private:
            //
            // Refuse a setpoint beyond this output's badge -- see
            // RatingExceeded. Called by applyOutput before anything is sent,
            // on a simulated instrument as well as an attached one, which is
            // the entire point of it existing at all.
            //
            // The overvoltage trip level is checked against the voltage
            // rating too, and that is not the same check as the setpoint's: a
            // trip level above what the output can produce is not a
            // protection, it is a protection that can never fire, and a
            // script that wrote one believes the rail is guarded when it is
            // not.
            //
            auto checkAgainstRating( const std::optional<core::quantities::Voltage> & voltage,
                                     const std::optional<core::quantities::Current> & currentLimit,
                                     const std::optional<core::quantities::Voltage> & overVoltage) const -> void
            {
                const std::string instrument{ to_string( mId) };

                if( voltage && voltage->value() > Output::MaxVolts)
                {
                    throw RatingExceeded( instrument, "voltage", voltage->value(), Output::MaxVolts, "V");
                }

                if( overVoltage && overVoltage->value() > Output::MaxVolts)
                {
                    throw RatingExceeded( instrument, "overvoltage trip level", overVoltage->value(), Output::MaxVolts, "V");
                }

                if( currentLimit && currentLimit->value() > Output::MaxAmps)
                {
                    throw RatingExceeded( instrument, "current limit", currentLimit->value(), Output::MaxAmps, "A");
                }
            }

            InstrumentId                      mId;
            Address                           mAddress;

            //
            // Null until the first command that needs hardware, and null
            // forever on a simulated instrument -- see session() on why it
            // cannot be opened in the constructor, and isSimulated() on what
            // its being null means.
            //
            // A unique_ptr, which makes this class non-copyable, and that is
            // correct rather than incidental: a copy of a driver would be a
            // second object claiming the same output, and one of the two would
            // hold the socket. Nothing copies one -- a rig's instruments are
            // globals and core::Port holds a reference -- so the restriction
            // costs nothing and removes a mistake.
            //
            std::unique_ptr<io::ScpiSession>  mSession;

            //
            // Whether mSession has had the once-per-session exchange: the
            // error-queue drain and the identity check. A flag beside the
            // pointer rather than something done where the session is created,
            // because a session arrives two ways -- opened from the address,
            // or handed in by useTransport() -- and both have to be prepared.
            //
            bool                              mPrepared{ false };

            core::quantities::Voltage                 mOutputVoltage{};
            core::quantities::Current                 mSimOutputCurrent{};
            std::optional<core::quantities::Current>  mCurrentLimit;
            std::optional<core::quantities::Voltage>  mOverVoltage;
            bool                                      mEnabled{ false };
    };

    //
    // A rig's instrument.inc names instruments by these aliases rather than by
    // EDU36311A<...> directly -- partly readability, partly mechanical: the
    // INSTRUMENT( type, id, address, ...) macro in
    // hal/topology/active_instruments.hpp splits its arguments on top-level
    // commas, and EDU36311A<Output2, RelayIsolated> would arrive there as two
    // arguments. Plain identifiers sidestep the question entirely.
    //
    // Six, because the two axes are independent and a rig may need any
    // combination: three physical outputs, each either cabled straight through
    // or through an isolation relay. Written out rather than generated, so
    // that the list of what a row may say is the list a reader sees.
    //
    // The name says both facts in the order a bench engineer would: whether
    // there is a relay in the lead, then which output of the box it is. So
    //
    //     INSTRUMENT( keysight_edu36311a::DirectOutput1, DcP5, Simulated{})
    //
    // reads as "the 6 V output, hard-wired", which is the whole of what that
    // row has to say beyond its id and its address.
    //
    using DirectOutput1 = EDU36311A<Output1, DirectWiring>;
    using DirectOutput2 = EDU36311A<Output2, DirectWiring>;
    using DirectOutput3 = EDU36311A<Output3, DirectWiring>;
    using RelayOutput1  = EDU36311A<Output1, RelayIsolated>;
    using RelayOutput2  = EDU36311A<Output2, RelayIsolated>;
    using RelayOutput3  = EDU36311A<Output3, RelayIsolated>;

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see
    // core/verbs/source.hpp's own comment on the applyDriver/removeDriver
    // customization points. Programs -- or disables -- the output only; the
    // fabric path is a separate concern, see connectDriver/disconnectDriver
    // below. Defined for both Isolation kinds identically: programming a
    // supply's output does not care whether it has a relay in its path.
    //
    // Found via ADL because DcConfig lives in this namespace, which is also
    // what keeps these out of the pile every driver used to share.
    //
    template<typename Output, typename Isolation>
    auto applyDriver( const DcConfig<Output, Isolation> & config) -> void
    {
        config.Instrument.applyOutput( config.Voltage, config.CurrentLimit, config.OverVoltage);
    }

    template<typename Output, typename Isolation>
    auto removeDriver( const DcConfig<Output, Isolation> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // ADL target for the electrical interlock -- see core/verbs/interlock.hpp
    // on the isEnergised customization point, and
    // core::ConnectEngine/DisconnectEngine for the two callers. Answers
    // whether this output is on at the moment a relay in its path is about to
    // move.
    //
    // Through outputIsOn() rather than isEnabled(), which asks the instrument
    // rather than this driver's memory -- see those two members' shared
    // comment for the whole argument. Note this config is const and its
    // Instrument member is a reference, so a non-const query through it is
    // available here: the constness that stops hal::energisedSourceAt() doing
    // the same is the constness of the *instrument*, not of the config.
    //
    // Required of this config rather than optional, and the requirement is
    // checked: core::detail::energisedNow static_asserts it for any config
    // that has an applyDriver, on the grounds that a config Apply can energise
    // and that cannot say whether it currently is would silently report every
    // relay move as cold. Defined for both Isolation kinds even though only
    // SwitchableIsolation ever reaches Connect/Disconnect -- the question is
    // about the output, which both kinds have.
    //
    template<typename Output, typename Isolation>
    auto isEnergised( const DcConfig<Output, Isolation> & config) -> bool
    {
        return config.Instrument.outputIsOn();
    }

    //
    // ADL target for the run journal -- see core/driver/describe.hpp's own
    // comment on the describeConfig customization point, and
    // hal::describeSetting in hal/driver/describe.hpp for the optional-field
    // helper. Only this config's own type knows which fields it has, so only
    // code alongside it can say what an Apply of it actually did.
    //
    // The output's identity is included, and it has to be: three instances of
    // this driver sit behind one address on this rig, and a log line naming
    // only "DcP6" tells a reader nothing about which of the box's three
    // outputs was programmed -- nor, more usefully, what that output's limits
    // were. "output 2 (30 V / 1 A)" says both.
    //
    template<typename Output, typename Isolation>
    auto describeConfig( const DcConfig<Output, Isolation> & config) -> core::SourceDescription
    {
        using Instrument = EDU36311A<Output, Isolation>;

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeSetting( "voltage",      config.Voltage),
                describeSetting( "currentLimit", config.CurrentLimit),
                describeSetting( "ovp",          config.OverVoltage),
                "output " + std::to_string( Instrument::channel())
                    + " (" + std::string( Instrument::rating()) + ")"
            })
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // core/verbs/route.hpp's own comment on the connectDriver/disconnectDriver
    // customization points. Closes -- or opens -- every fixed path registered
    // for this instrument, together (see hal::InstrumentWiring::findAll() and
    // hal::WireRole on why force and any sense leads are meant to move as one
    // unit here). No connector-side hop: there is no connector path to look up
    // (see DcConfig's own comment), so connectorWiring is accepted for
    // signature symmetry with every other instrument's connectDriver and never
    // consulted.
    //
    // Constrained by SwitchableIsolation, so there is no version of these that
    // instantiates for a DirectWiring output at all: Connect( DcP5.dc()) fails
    // overload resolution outright, which is the same
    // "misspelled-or-impossible fails to compile" guarantee this project
    // applies everywhere else. One generic template rather than one overload
    // per relay-having tag, so that a future isolation tag setting HasRelay
    // gets these for free.
    //
    template<typename Output, typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const DcConfig<Output, Isolation> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Output, typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const DcConfig<Output, Isolation> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal::keysight_edu36311a
