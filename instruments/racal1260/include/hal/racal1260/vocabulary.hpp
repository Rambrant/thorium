#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/quantities/bytes.hpp"
#include "core/driver/describe.hpp"
#include "core/verbs/transfer.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/builder.hpp"
#include "hal/topology/bundle.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

//
// parity, stop bits
//
// Part of hal/racal1260.hpp -- include that, not this.
//

namespace hal::racal1260
{
    //
    // Word framing, as the three settings a UART is actually configured with.
    // Enums rather than the bare ints and chars the legacy test language spelled
    // them as ("BaudRate=9600 StopBits=1 WordLength=8 Parity=NONE" in one
    // string), because every one of those was a runtime typo waiting to happen
    // in a language that could only discover it when the port refused to open.
    //
    enum class Parity
    {
        None,
        Even,
        Odd
    };

    //
    // 1.5 stop bits is a real RS232 framing and cannot be an int, which is the
    // reason this is an enum rather than a count -- StopBits::OnePointFive has
    // nowhere to go in an `int stopBits` field, and a driver that silently
    // rounded it would misframe every word.
    //
    enum class StopBits
    {
        One,
        OnePointFive,
        Two
    };

    [[nodiscard]]
    constexpr auto to_string( const Parity parity) -> std::string_view
    {
        switch( parity)
        {
            case Parity::None: return "none";
            case Parity::Even: return "even";
            case Parity::Odd:  return "odd";
        }

        return "none";
    }

    [[nodiscard]]
    constexpr auto to_string( const StopBits bits) -> std::string_view
    {
        switch( bits)
        {
            case StopBits::One:          return "1";
            case StopBits::OnePointFive: return "1.5";
            case StopBits::Two:          return "2";
        }

        return "1";
    }

    class Racal1260;
} // namespace hal::racal1260
