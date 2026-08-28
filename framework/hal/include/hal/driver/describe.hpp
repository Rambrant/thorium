#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/quantities/format.hpp"
#include "core/meta.hpp"
#include "core/quantities/quantity.hpp"

namespace hal
{
    //
    // Shared helpers for the describeConfig() overloads each source instrument
    // provides for the run journal (see core/driver/describe.hpp's own comment on that
    // customization point, and hal/n6701a.hpp / hal/ac6834b.hpp for the
    // overloads themselves).
    //
    // These exist because every such overload has the same two problems --
    // rendering an optional field only if it was set, and joining however many
    // of them survived -- and a config's field list is exactly the kind of thing
    // that grows. Solving it once here means adding a field to a builder needs
    // one line in that instrument's describeConfig, not another copy of the
    // "was it set, do I need a comma" dance.
    //
    // Note the asymmetry with applyDriver, which each instrument writes entirely
    // by itself: applyDriver's body is genuinely per-instrument (which SCPI, in
    // which order), where the *formatting* of "name=value, name=value" is not
    // instrument-specific at all.
    //

    //
    // One "name=value" fragment, or an empty string for an unset field. An unset
    // optional means the builder was never told (see core::MeasureSetup's own
    // comment on nullopt meaning "use whatever's already configured"), so a log
    // must not report a value for it -- an Apply that left the current limit
    // alone and one that set it to zero are very different instructions to a
    // supply.
    //
    template<typename Unit>
    [[nodiscard]]
    auto describeSetting( const std::string_view name, const std::optional<core::quantities::Quantity<Unit>> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        return std::string( name) + "=" + core::describeValue( value.value());
    }

    //
    // The same fragment for a field whose value is one of a fixed set -- a
    // coupling, an acquisition mode, a trigger slope. Unset means the same
    // thing it does above, and is rendered the same way: not at all.
    //
    // The text comes from the enumerator's own spelling, reflected
    // (core::meta::to_string), rather than from a hand-written switch per enum.
    // That is the choice core/meta.hpp exists to make available, and it is the
    // right one *here* specifically because these settings are already named
    // for what they are: an instrument enum in this codebase is spelled
    // HighResolution or Div10 or LowFrequencyReject, and a log saying
    // "mode=HighResolution" is saying the thing the script wrote. Where an
    // enum's log text genuinely differs from its enumerator -- hal::Parity's
    // lowercase "none", a fault code's sentence of prose -- that enum keeps its
    // own to_string and does not come through here. Same split
    // core::to_string(QuantityKind) already draws.
    //
    template<typename EnumT>
        requires std::is_enum_v<EnumT>
    [[nodiscard]]
    auto describeChoice( const std::string_view name, const std::optional<EnumT> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        return std::string( name) + "=" + std::string( core::meta::to_string( value.value()));
    }

    //
    // And for a plain count -- a memory depth, a number of averages. Numbers
    // that are selectors rather than physical quantities, so they have no unit
    // to render and no Quantity to carry them (the same distinction
    // core::MeasureSetup draws for NPLC).
    //
    template<typename CountT>
        requires std::integral<CountT>
    [[nodiscard]]
    auto describeCount( const std::string_view name, const std::optional<CountT> & value) -> std::string
    {
        if( !value.has_value())
        {
            return {};
        }

        return std::string( name) + "=" + std::to_string( value.value());
    }

    //
    // Joins whichever fragments are non-empty with ", ". Takes a vector rather
    // than a variadic pack purely for readability at the call sites -- a
    // describeConfig body reads as a list of the fields that config has, which
    // is the thing worth being able to check at a glance against the config
    // struct next to it.
    //
    [[nodiscard]]
    inline auto describeSettings( const std::vector<std::string> & fragments) -> std::string
    {
        std::string result;

        for( const auto & fragment : fragments)
        {
            if( fragment.empty())
            {
                continue;
            }

            if( !result.empty())
            {
                result += ", ";
            }

            result += fragment;
        }

        return result;
    }
} // namespace hal
