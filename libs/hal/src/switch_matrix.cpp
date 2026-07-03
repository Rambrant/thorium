#include "hal/switch_matrix.hpp"

#include <stdexcept>
#include <string>

namespace hal
{

void SwitchMatrix::validate(core::Crosspoint p) const
    {
    if (p.mRow >= mRows || p.mCol >= mCols) {
        throw std::out_of_range(
            "crosspoint (" + std::to_string(p.mRow) + "," + std::to_string(p.mCol) +
            ") outside matrix bounds " + std::to_string(mRows) + "x" + std::to_string(mCols));
    }
}

}  // namespace hal
