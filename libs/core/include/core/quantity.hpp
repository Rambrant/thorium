#pragma once

#include <compare>
#include <concepts>

namespace core {

// Unit tags. These exist only to make Quantity<Tag> a distinct type per unit,
// so e.g. a Voltage can never be silently compared against a Current.
struct V_Tag {};
struct A_Tag {};
struct VA_Tag {};
struct W_Tag {};
struct Ohm_Tag {};
struct dB_Tag {};
struct s_Tag {};

template<typename Unit>
class Quantity
{
public:
    using unit_type = Unit;

    constexpr Quantity() = default;

    template<std::floating_point T>
    constexpr explicit Quantity(T value) : m_value(static_cast<double>(value)) {}

    [[nodiscard]] constexpr double value() const { return m_value; }

    // Defining both == and <=> (rather than just <=>) gives the compiler
    // everything it needs to synthesize ==, !=, <, <=, >, >= via C++20's
    // rewritten-candidate rules. Comparisons only compile between the same
    // Unit, so mixing e.g. Voltage and Current is a compile error.
    friend constexpr bool operator==(Quantity lhs, Quantity rhs)
    {
        return lhs.m_value == rhs.m_value;
    }

    friend constexpr std::partial_ordering operator<=>(Quantity lhs, Quantity rhs)
    {
        return lhs.m_value <=> rhs.m_value;
    }

private:
    double m_value{};
};

using Voltage       = Quantity<V_Tag>;
using Current       = Quantity<A_Tag>;
using Power         = Quantity<W_Tag>;
using ApparentPower = Quantity<VA_Tag>;
using Resistance    = Quantity<Ohm_Tag>;
using Time          = Quantity<s_Tag>;
using Decibel       = Quantity<dB_Tag>;

// Algebra: combining distinct units to produce a new unit.
constexpr ApparentPower operator*(Voltage lhs, Current rhs)
{
    return ApparentPower{lhs.value() * rhs.value()};
}

// Literals live in their own namespace (mirrors std::chrono::literals) so
// pulling them in requires an explicit `using namespace core::literals;`
// rather than polluting every translation unit that includes this header.
namespace literals {

constexpr Voltage operator""_V(long double v) { return Voltage{static_cast<double>(v)}; }
constexpr Voltage operator""_kV(long double v) { return Voltage{static_cast<double>(v * 1000.0)}; }
constexpr Voltage operator""_mV(long double v) { return Voltage{static_cast<double>(v / 1000.0)}; }

constexpr Current operator""_A(long double v) { return Current{static_cast<double>(v)}; }
constexpr Current operator""_mA(long double v) { return Current{static_cast<double>(v / 1000.0)}; }

constexpr Resistance operator""_Ohm(long double v) { return Resistance{static_cast<double>(v)}; }

}  // namespace literals

}  // namespace core
