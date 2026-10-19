#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/driver/describe.hpp"
#include "core/meta.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

//
// phases, symmetry, voltage ranges
//
// Part of hal/keysight_ac6834b.hpp -- include that, not this.
//

namespace hal::keysight_ac6834b
{
    class AcSource;

    //
    // Which of the three phases a setting or a reading belongs to. Named
    // A/B/C after the conductors themselves, matching the rig's four
    // WIRE_SOURCE rows for AcP1 and the dut::AcInput bundle's lines (see
    // rig/wiring.inc, dut/adapter.inc) -- the same three wires seen from the
    // instrument side.
    //
    // Numbered 1/2/3 by the instrument itself -- INSTrument:NSELect takes
    // 1|2|3 and the reset phase angles are documented as phase 1 = 0,
    // phase 2 = 240, phase 3 = 120. A/B/C is the conductor naming, which is
    // what the DUT side uses and what a bench technician reads off a cable,
    // so it is what this enum spells; a real driver maps A/B/C onto NSELect
    // 1/2/3, which is indexOf() + 1.
    //
    // The neutral/ground return is deliberately not an enumerator, but the
    // reason is narrower than it first looks. Nothing is *programmed* onto
    // it -- there is no neutral voltage or current-limit setpoint -- so it
    // has no place in any of the per-phase setters, which is what this enum
    // exists for. It is not unmeasurable, though -- the instrument answers
    // MEASure:CURRent:NEUTral? on three-phase models, and that reading has a
    // home of its own: hal::keysight_ac6834b::AcSource::measuredNeutralCurrent(), which takes no
    // Phase precisely because the neutral is not one.
    //
    enum class Phase
    {
        A,
        B,
        C
    };

    //
    // Reflected over Phase's own enumerators, not a switch -- see
    // core::meta::to_string's own comment in core/meta.hpp, which names the
    // two hand-written switches it replaced. A third one here would have
    // spelled A/B/C a second time for no reason beyond stringifying them.
    //
    // Kept as a function in this driver's own namespace rather than calling
    // core::meta::to_string at each site, so an unqualified to_string( phase)
    // resolves by ADL on Phase the way
    // hal::to_string( InstrumentId) already does (see hal/src/driver/instrument.cpp,
    // the same one-line delegation).
    //
    [[nodiscard]]
    constexpr auto to_string( const Phase phase) -> std::string_view
    {
        return core::meta::to_string( phase);
    }

    //
    // All three, in declaration order -- for the "check each phase" loop,
    // which would otherwise repeat { Phase::A, Phase::B, Phase::C } at every
    // call site. Reflected for the same reason to_string() above is.
    //
    inline constexpr auto phases = core::meta::values<Phase>;

    //
    // One value per phase, in A/B/C order. A fixed-size array rather than a
    // braced list of three: an initializer_list would let a two-element or
    // four-element list compile, and "I gave it the wrong number of phases"
    // is exactly the mistake worth having the compiler catch.
    //
    template<typename QuantityT>
    using PerPhaseValues = std::array<QuantityT, 3>;

    //
    // Where a phase's value sits in a PerPhaseValues. The cast is safe only
    // because Phase leaves its enumerators at their default 0/1/2, which the
    // assertions below pin down -- an explicitly-numbered enumerator would
    // index past the array or alias another phase, and neither shows up as
    // anything but a wrong reading.
    //
    [[nodiscard]]
    constexpr auto indexOf( const Phase phase) -> std::size_t
    {
        return static_cast<std::size_t>( phase);
    }

    static_assert( phases.size() == std::tuple_size_v<PerPhaseValues<int>>,
                   "PerPhaseValues holds one slot per Phase enumerator");
    static_assert( indexOf( Phase::A) == 0 && indexOf( Phase::B) == 1 && indexOf( Phase::C) == 2,
                   "Phase enumerators must keep their default values -- indexOf() indexes "
                   "PerPhaseValues with them directly");

    //
    // A quantity tagged with the phase it is meant for -- what phaseA()/
    // phaseB()/phaseC() below produce, and what the per-phase setters take.
    //
    // The phase lives in the type, not in a runtime field, so the three
    // arguments of a per-phase setter can only be supplied in A/B/C order:
    // writing .phaseVoltage( phaseB( ...), phaseA( ...), phaseC( ...)) is a
    // compile error rather than a silent transposition.
    //
    // This is the same reasoning core::at() is built on (see core/verbs/at.hpp):
    // the alternative spelling, a bare triple like { 115_V, 113_V, 117_V },
    // is shorter but depends on the reader knowing which position is which
    // conductor -- and reads identically whether or not it is right.
    //
    template<typename QuantityT, Phase P>
    struct PhaseValue
    {
        static constexpr Phase Which = P;

        QuantityT Value;
    };

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseA( const QuantityT value) -> PhaseValue<QuantityT, Phase::A>
    {
        return { value };
    }

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseB( const QuantityT value) -> PhaseValue<QuantityT, Phase::B>
    {
        return { value };
    }

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseC( const QuantityT value) -> PhaseValue<QuantityT, Phase::C>
    {
        return { value };
    }

    //
    // Whether a configuration drives all three phases alike or each one
    // separately.
    //
    // Tag types on the builder and its config rather than a runtime flag, the
    // same shape (and for the same reason) as hal::keysight_edu36311a::DirectWiring/
    // RelayIsolated in hal/keysight_edu36311a.hpp: what differs between the two is the
    // *type* of what a config holds -- one voltage or three -- so a driver
    // reading it never has to ask which case it is in.
    //
    // This is the only axis the builder has, and deliberately so: it is a
    // property the chain acquires rather than a second entry-point method
    // alongside ac(). Entry points multiply against anything else
    // that ever becomes selectable, where a builder-carried tag does not --
    // and on this instrument there is nothing else to select anyway (see
    // hal::keysight_ac6834b::AcSource on why the phase count is not a setting).
    //
    // This split is the instrument's own, not an invention of this header.
    // The 6834B has INSTrument:COUPle <ALL|NONE>: under COUPle ALL, sending a
    // command to any phase sends it to all three, and the commands this
    // affects are exactly those the programming guide marks "Phase
    // Selectable". Balanced is COUPle ALL; PerPhase is COUPle NONE plus
    // INSTrument:NSELect per phase. The guide's *RST value for COUPle is ALL,
    // which is also why ac() starts a Balanced chain rather than a
    // per-phase one.
    //
    // Which settings are phase-selectable is likewise the guide's answer and
    // not a guess: VOLTage and CURRent (the rms current limit) carry the
    // designation, FREQuency does not -- see AcConfig below, where that
    // asymmetry is the whole reason frequency is a plain scalar.
    //
    // One command does not fit this scheme and will need care when it is
    // added: PHASe, the per-phase angle, is "not influenced by
    // INSTrument:COUPle ALL. It applies only to the current output phase
    // selected by INSTrument:NSELect." So phase angle is per-phase *only* --
    // it can never be broadcast, and a scalar setter for it would be a lie
    // rather than a convenience. That makes three categories, where this
    // header currently models two: scalar-only (frequency), scalar-or-
    // per-phase (voltage, current limit), and per-phase-only (angle).
    //
    struct Balanced {};
    struct PerPhase {};

    //
    // A setting that may differ per phase: three values under PerPhase, one
    // under Balanced. Frequency deliberately never uses this -- see
    // AcConfig below.
    //
    template<typename Symmetry, typename QuantityT>
    using PhaseSetting = std::conditional_t<std::is_same_v<Symmetry, PerPhase>,
                                            std::optional<PerPhaseValues<QuantityT>>,
                                            std::optional<QuantityT>>;

    //
    // The range an arbitrary requested voltage actually selects. The
    // instrument resolves the parameter rather than storing it: "sending a
    // parameter greater than 150 selects the 300 volt range, otherwise the
    // 150 volt range is selected", and VOLTage:RANGe? reads back the range,
    // not what was sent. Modelling that here means range( 115_V) and
    // range( 150_V) are the same instruction, which is what the hardware
    // does, and means a readback can never report a value the instrument
    // would not.
    //
    // Not a validation step -- nothing here rejects anything. The current
    // limit that this range constrains is deliberately left unchecked; see
    // hal::keysight_ac6834b::AcSource's list of what is not modeled.
    //
    inline constexpr auto LowVoltageRange  = core::quantities::Voltage{ 150.0 };
    inline constexpr auto HighVoltageRange = core::quantities::Voltage{ 300.0 };

    [[nodiscard]]
    constexpr auto rangeFor( const core::quantities::Voltage requested) -> core::quantities::Voltage
    {
        return requested.value() > LowVoltageRange.value() ? HighVoltageRange : LowVoltageRange;
    }
} // namespace hal::keysight_ac6834b
