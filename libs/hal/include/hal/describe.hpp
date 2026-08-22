#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/format.hpp"
#include "core/quantity.hpp"

namespace hal
{
    //
    // Shared helpers for the describeConfig() overloads each source instrument
    // provides for the run journal (see core/describe.hpp's own comment on that
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
