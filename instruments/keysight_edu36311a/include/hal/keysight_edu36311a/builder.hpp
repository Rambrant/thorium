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

#include "hal/keysight_edu36311a/vocabulary.hpp"

//
// DcConfig + DcBuilder
//
// Part of hal/keysight_edu36311a.hpp -- include that, not this.
//

namespace hal::keysight_edu36311a
{
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
        DcSource<Output, Isolation> &            Instrument;
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

            explicit DcBuilder( DcSource<Output, Isolation> & instrument) :
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
} // namespace hal::keysight_edu36311a
