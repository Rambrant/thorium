#include "hal/switch_matrix.hpp"

#include <stdexcept>
#include <string>

namespace hal
{
    auto SwitchMatrix::validate( const core::Crosspoint point) const -> void
    {
        if( point.mRow >= mRows || point.mCol >= mCols)
        {
            throw std::out_of_range(
                "crosspoint (" + std::to_string( point.mRow) + "," + std::to_string( point.mCol) +
                ") outside matrix bounds " + std::to_string( mRows) + "x" + std::to_string( mCols));
        }
    }
} // namespace hal
