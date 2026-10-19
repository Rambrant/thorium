#pragma once

#include <string>

#include "hal/io/scpi.hpp"

//
// What both faces check on the session they share.
//
// Part of hal/keysight_34980a.hpp -- include that, not this.
//

namespace hal::keysight_34980a
{
    namespace detail
    {
        //
        // *IDN?, and a refusal unless the box is a 34980A -- the check both of
        // this package's faces make on the session they share. See its
        // definition for what is accepted and why the vendor field is not.
        //
        [[nodiscard]]
        auto verifyMainframe( io::ScpiSession & session) -> std::string;
    } // namespace detail

} // namespace hal::keysight_34980a
