#include "core/session/bench.hpp"

namespace core
{
    auto bench() -> Bench &
    {
        //
        // Function-local static, for core::journal()'s reason: it is
        // constructed on first use, so nothing depends on the initialisation
        // order between this translation unit and whichever one asks first.
        //
        static Bench instance;

        return instance;
    }
} // namespace core
