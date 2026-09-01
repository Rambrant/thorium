#pragma once

#include <type_traits>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/instrument.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when it
// moves, and instruments/README.md for why a driver package has to say this at
// all (it travels separately from the hal it compiles against).
//
THORIUM_REQUIRE_HAL_API( 1);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and here that matters more than usual:
// "34450A" alone would not even distinguish this meter from its own sibling,
// the Keysight 34450A whose command set it shares.
//
namespace hal::keysight_edu34450a
{
    //
    // Keysight EDU34450A: the 5.5-digit dual-display bench DMM from the Smart
    // Bench Essentials line. It measures eleven input signals -- DC and true
    // RMS AC volts, DC and true RMS AC amps, two- and four-wire ohms,
    // frequency, continuity, diode test, temperature and capacitance -- of
    // which this class exposes the eight that this framework has a quantity
    // for (see "What is deliberately not here" below).
    //
    // It shares its SCPI dialect with the Keysight 34450A, whose programmer's
    // reference is the document the comments here cite; the EDU is that
    // instrument's education-market packaging, not a different command set.
    //
    // -- Why this is not simply another L4411A --------------------------------
    //
    // Dmm1 and Dmm2 used to be two hal::keysight_l4411a::L4411A instances, and
    // Dmm2 still is (see rig/instrument.inc). The two meters are close enough
    // that a script measuring a rail cannot tell them apart, and far enough
    // apart that one C++ type would have had to lie about one of them:
    //
    //   digits       5.5 here, 6.5 on the L4411A -- a criterion tightened to
    //                the L4411A's resolution is not a criterion this meter can
    //                answer.
    //
    //   integration  the L4411A integrates over power-line cycles and takes an
    //                NPLC. This family has no NPLC command at all: resolution
    //                is one of exactly three discrete values, and that is what
    //                Resolution below names. See setResolution().
    //
    //   functions    this meter adds capacitance, temperature and a secondary
    //                display; the L4411A has none of those and reaches further
    //                in range and accuracy instead.
    //
    // Which is the argument this codebase already made when it retired the
    // generic hal::Dmm placeholder: once the real model is known, naming the
    // class after it documents the non-portability of its measurement-function
    // set rather than pretending a DMM driver is interchangeable across models.
    // Two meters on one bench being two different C++ types is that argument
    // arriving where it was always heading.
    //
    // -- The one sharp edge, unchanged from the L4411A ------------------------
    //
    // AC/DC mode and 2-wire/4-wire resistance mode are stored on the
    // instrument, not the port -- calling .acVoltage()/.acCurrent() switches
    // this meter into AC, .voltage()/.current() switch it back to DC, and
    // .resistance()/.fourWireResistance() switch between 2-wire and 4-wire
    // ohms, exactly the way the front-panel function keys work and exactly
    // what SCPI's FUNC "VOLT:AC" does. So a port handle obtained before a mode
    // switch reads whichever mode is current when rawMeasure() is eventually
    // called, not the mode active when the handle was created. That never
    // matters for real usage, since Measure(port, at(...)) reads a port
    // immediately and discards it, and it is tested rather than designed away.
    //
    // -- What is deliberately not here ----------------------------------------
    //
    // temperature   core does have a Temperature, but this function is a
    //               2-wire measurement of a 5 kOhm thermistor plugged into the
    //               meter's own front terminals, auto-ranging only. It is not a
    //               reading that can travel through the matrix to a DUT pin, so
    //               a port for it would be one nothing on this rig could
    //               legitimately route (see rig/wiring.inc, and TAP_WIRING for
    //               the shape a directly-bolted lead would take if one ever
    //               appeared).
    //
    // continuity /  both are threshold answers rather than measurements -- a
    // diode test    fixed 10 Ohm continuity threshold with a beeper behind it,
    //               and a forward-voltage check on a fixed 1 V range, both
    //               available in fast mode only. What a script wants from
    //               either is a verdict, and this framework already spells a
    //               verdict as a criterion over a real reading (see
    //               core/criteria/criterion.hpp): resistance() with a
    //               LT( 10_Ohm) says the same thing and says it in the
    //               vocabulary the report is written in.
    //
    // secondary     SCPI reaches the second display through a whole parallel
    // display       command tree (SEC:FUNC, CONF:SEC:..., MEAS:SEC:...?) and it
    //               is a genuinely useful thing -- one acquisition answering
    //               "DC volts and the AC ripple on them" at once. It is also a
    //               second reading per port, which core::Port has no shape for:
    //               a port is one quantity. Exposing it means a second port
    //               accessor pair and a decision about how two readings key
    //               into one session slot, which is a design question and not a
    //               driver detail.
    //
    class EDU34450A : public InstrumentTag
    {
        public:
            enum class Mode           { Dc, Ac };
            enum class ResistanceMode { TwoWire, FourWire };

            //
            // The whole of this meter's speed/precision axis, and the reason
            // core::MeasureSetup::Nplc means nothing here.
            //
            // An L4411A is told how many power-line cycles to integrate over,
            // and any number is a number it can be told. This family instead
            // accepts exactly three resolutions per function -- SCPI
            // VOLT:DC:RES takes 1.50E-6, 2.00E-5 or 3.00E-5 and nothing else --
            // which the front panel and the data sheet both name Slow, Medium
            // and Fast:
            //
            //   Slow    5.5 digits, ~1.3 readings/s on DCV, 60 dB of normal-
            //           mode rejection at the line frequency. What the
            //           instrument resets to, and what its accuracy
            //           specifications are quoted at.
            //   Medium  4.5 digits, ~49 readings/s, still 60 dB rejection.
            //   Fast    4.5 digits, up to 110 readings/s, and no normal-mode
            //           rejection at all (0 dB) -- line hum lands in the
            //           reading. Right for sequencing through many pins, wrong
            //           for a rail tolerance check.
            //
            // Named for what the instrument calls them rather than for their
            // digit counts, because Medium and Fast are both 4.5 digits and a
            // pair of enumerators that differed only in a suffix would be worse
            // at saying which is which.
            //
            enum class Resolution     { Slow, Medium, Fast };

            //
            // LAN or USB, and nothing else: gigabit LAN and a USB device port
            // are what this model's back panel has. No GPIB connector and no
            // option that adds one -- the 34450A whose command set this shares
            // does offer GPIB (its SCPI even has SYST:COMM:GPIB:ADDR), and
            // this is the box that does not. So a rig row addressing it over
            // GPIB fails to compile rather than failing to open; see
            // hal::ReachableOver in hal/driver/address.hpp,
            // which is also where the reasoning lives for why the bus *kind* is
            // checked here and the address itself is just a value this driver
            // carries.
            //
            // Note what this does NOT constrain: the front-panel USB host port
            // is for a flash drive holding saved setups, and is not a way for
            // the PC to reach the instrument. hal::Usb here means the rear
            // USBTMC device port.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Lan, Usb>
            EDU34450A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            //
            // Where the PC reaches this meter -- nothing reads it yet, the same
            // way hal::keysight_n6701a::N6701A's mainframe slot was carried
            // before any driver needed it, because the alternative is a rig
            // table that cannot say the thing at all.
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

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, EDU34450A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Voltage, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto current() -> core::Port<core::quantities::Current, EDU34450A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Current, EDU34450A>{ *this };
            }

            //
            // True RMS, and worth saying rather than assuming: this reads the
            // AC component of whatever is on the terminals regardless of
            // waveshape, AC-coupled with up to 400 V of DC bias riding under
            // it. The data sheet's accuracies assume a sine and carry a crest-
            // factor adder for anything else.
            //
            [[nodiscard]]
            auto acVoltage() -> core::Port<core::quantities::Voltage, EDU34450A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Voltage, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto acCurrent() -> core::Port<core::quantities::Current, EDU34450A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Current, EDU34450A>{ *this };
            }

            //
            // 2-wire ohms: the meter's own lead resistance is included in the
            // reading, same as pressing the front-panel 2W key. The data
            // sheet's resistance accuracies are quoted with the NULL function
            // on; without it, add 0.2 Ohm.
            //
            [[nodiscard]]
            auto resistance() -> core::Port<core::quantities::Resistance, EDU34450A>
            {
                mResistanceMode = ResistanceMode::TwoWire;
                return core::Port<core::quantities::Resistance, EDU34450A>{ *this };
            }

            //
            // 4-wire (Kelvin) ohms: separate sense leads cancel out lead
            // resistance, same as pressing the front-panel 4W key --
            // requiresSensePath() (see core/driver/port.hpp) is what actually
            // tells core::MeasureEngine to route this instrument's sense
            // channel and the DUT point's sense channel alongside the normal
            // force path, only for this one reading. A plain resistance() call
            // above never sets it, so a 2-wire reading on this same instrument
            // never touches sense at all.
            //
            // Much slower than 2-wire here, at every resolution: about 6
            // readings/s against 110 in fast mode, and about 1 against 1.3 in
            // slow. That is a hardware fact rather than anything this driver
            // does, and it is the reason a scan of many pins is usually 2-wire
            // with a NULL rather than 4-wire.
            //
            [[nodiscard]]
            auto fourWireResistance() -> core::Port<core::quantities::Resistance, EDU34450A, core::SensePath::Required>
            {
                mResistanceMode = ResistanceMode::FourWire;
                return core::Port<core::quantities::Resistance, EDU34450A>{ *this }.requiresSensePath();
            }

            //
            // The FREQ function -- SCPI FUNC "FREQ", a counter on whatever is
            // already at the terminals, specified 20 Hz to 300 kHz off the
            // voltage input and 20 Hz to 10 kHz off the current one.
            //
            // Not to be confused with core::Port::frequency(), which is a
            // *setting* -- "the AC signal you are about to measure is at 50 Hz"
            // -- for meters that need telling. This one is a reading. The two
            // spellings sitting a dot apart is unfortunate and the names are
            // both right: Dmm1.frequency() asks the meter how fast the signal
            // is, Dmm1.acVoltage().frequency( 50_Hz) tells it. This model needs
            // no telling and ignores the setting (see rawMeasure below).
            //
            // Its own function rather than a mode of acVoltage(), because that
            // is what the instrument does: FUNC "FREQ" replaces the voltage
            // function outright rather than adding to it. Reading both at once
            // is what the secondary display is for, and that is not exposed --
            // see this class's own comment.
            //
            [[nodiscard]]
            auto frequency() -> core::Port<core::quantities::Frequency, EDU34450A>
            {
                return core::Port<core::quantities::Frequency, EDU34450A>{ *this };
            }

            //
            // The CAP function -- SCPI FUNC "CAP", 1 nF to 10 mF over eight
            // ranges. The one function on this meter that neither the L4411A
            // nor anything else on this bench has, and the reason core grew a
            // farad (see core::quantities::Capacitance).
            //
            // Measured the way a DMM measures a capacitance and not the way an
            // LCR bridge does: a known current into the node -- 100 nA on the
            // 1 nF range, 1 mA on the 10 mF one -- and the slope of the ramp
            // that current produces. Two consequences a script author should
            // know before trusting a reading:
            //
            //   it is a *sourcing* measurement, so the node has to be dead.
            //   core::requiresDeadNode names Capacitance for exactly this
            //   (core/verbs/interlock.hpp), which means a routed reading at a
            //   pin an energised supply is cabled onto is refused before
            //   anything closes rather than attempted. Remove the source
            //   first, or read the meter's own terminals with the point-free
            //   Measure overload.
            //
            //   it is a two-terminal DC measurement, so it reads the whole
            //   node: anything in parallel with the part is in the answer, and
            //   a leaky capacitor reads high because the leakage adds to the
            //   charging current. The data sheet's accuracies assume a NULL of
            //   the open test leads first -- on the 1 nF range the leads
            //   themselves are a meaningful fraction of full scale.
            //
            // No 4-wire variant, and no sense path: the instrument has one
            // capacitance function and it is 2-wire. Kelvin sensing answers a
            // question about lead resistance, which is not what limits this
            // measurement.
            //
            [[nodiscard]]
            auto capacitance() -> core::Port<core::quantities::Capacitance, EDU34450A>
            {
                return core::Port<core::quantities::Capacitance, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto resistanceMode() const -> ResistanceMode
            {
                return mResistanceMode;
            }

            //
            // Instrument state rather than a per-reading port setting, and
            // deliberately so. core::MeasureSetup is the per-reading channel,
            // and its precision field is Nplc -- a count of power-line cycles
            // this instrument has no command for. Squeezing Slow/Medium/Fast
            // into an integer NPLC would be a translation this driver invented,
            // wrong in both directions: a script asking for .nplc( 10) would
            // silently get a resolution the meter chose, and a reader of that
            // script would believe the rig had integrated for ten cycles.
            //
            // So a setter, in the shape hal::keysight_dso8064a::DSO8064A's
            // setMode() already uses for the same reason -- one front-panel
            // knob, set before a reading and left where it was put. A script
            // that wants a rail measured properly sets Slow once and reads;
            // one sequencing a hundred continuity checks sets Fast once.
            //
            // The instrument keeps a resolution per function and restores it on
            // returning to that function, which this stub does not model: one
            // value covers the whole meter here. Worth knowing before trusting
            // a readback against real hardware, and not worth modelling until
            // something reads it.
            //
            auto setResolution( const Resolution resolution) -> void
            {
                mResolution = resolution;
            }

            [[nodiscard]]
            auto resolution() const -> Resolution
            {
                return mResolution;
            }

            //
            // Nothing to do, and that is a hardware fact rather than an
            // unfinished implementation: a DMM is a passive instrument -- it
            // sources nothing into the DUT, so there is no output for
            // hal::safeRig() (see hal/verbs/safing.hpp) to drop. Its leads are
            // taken off the DUT by the fabric's own openAll(), which safeRig()
            // does after this, not by anything this instrument is asked to do.
            //
            // Written out as an explicit empty body rather than left off and
            // detected as absent, on purpose -- see
            // hal::keysight_l4411a::L4411A::safe(), which carries the full
            // argument for why safing is mandatory rather than opt-in, and
            // hal::SafeableInstrument in hal/driver/instrument.hpp.
            //
            auto safe() -> void
            {
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto setSimulatedVoltage( const core::quantities::Voltage v) -> void
            {
                mSimVoltage = v;
            }

            auto setSimulatedCurrent( const core::quantities::Current c) -> void
            {
                mSimCurrent = c;
            }

            auto setSimulatedAcVoltage( const core::quantities::Voltage v) -> void
            {
                mSimAcVoltage = v;
            }

            auto setSimulatedAcCurrent( const core::quantities::Current c) -> void
            {
                mSimAcCurrent = c;
            }

            auto setSimulatedResistance( const core::quantities::Resistance r) -> void
            {
                mSimResistance = r;
            }

            auto setSimulatedFourWireResistance( const core::quantities::Resistance r) -> void
            {
                mSimFourWireResistance = r;
            }

            auto setSimulatedFrequency( const core::quantities::Frequency f) -> void
            {
                mSimFrequency = f;
            }

            auto setSimulatedCapacitance( const core::quantities::Capacitance c) -> void
            {
                mSimCapacitance = c;
            }

            //
            // The setup is accepted and ignored, exactly as it is on every
            // other driver in this tree: nothing here opens a session yet. When
            // one does, Range maps onto VOLT:DC:RANG and friends, Nplc maps
            // onto nothing at all on this model (see Resolution above), and
            // Frequency maps onto nothing either -- this meter's AC path needs
            // no hint about the signal it is looking at.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & ) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? mSimAcVoltage : mSimVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mMode == Mode::Ac ? mSimAcCurrent : mSimCurrent;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    return mResistanceMode == ResistanceMode::FourWire ? mSimFourWireResistance : mSimResistance;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    //
                    // Not switched on mMode: FREQ is its own function on this
                    // meter, not the AC half of the voltage one, so what the
                    // DC/AC keys were last set to says nothing about it.
                    //
                    return mSimFrequency;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Capacitance>)
                {
                    // Its own function too, and likewise unaffected by mMode.
                    return mSimCapacitance;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "EDU34450A has no port for this quantity");
                }
            }

        private:
            InstrumentId                  mId;
            Address                       mAddress;
            Mode                          mMode{ Mode::Dc };
            ResistanceMode                mResistanceMode{ ResistanceMode::TwoWire };
            //
            // Slow, matching what the instrument resets to and what its
            // accuracy specifications are quoted at -- a default that is wrong
            // in the safe direction, since the failure it cannot cause is a
            // reading that quietly arrived with a digit less than the criterion
            // checking it assumes.
            //
            Resolution                    mResolution{ Resolution::Slow };
            core::quantities::Voltage     mSimVoltage{};
            core::quantities::Current     mSimCurrent{};
            core::quantities::Voltage     mSimAcVoltage{};
            core::quantities::Current     mSimAcCurrent{};
            core::quantities::Resistance  mSimResistance{};
            core::quantities::Resistance  mSimFourWireResistance{};
            core::quantities::Frequency   mSimFrequency{};
            core::quantities::Capacitance mSimCapacitance{};
    };
} // namespace hal::keysight_edu34450a
