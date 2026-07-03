#include "hal/switch_matrix.hpp"

#include <stdexcept>
#include <string>

namespace hal {

void SwitchMatrix::validate(core::Crosspoint p) const {
    if (p.row >= rows_ || p.col >= cols_) {
        throw std::out_of_range(
            "crosspoint (" + std::to_string(p.row) + "," + std::to_string(p.col) +
            ") outside matrix bounds " + std::to_string(rows_) + "x" + std::to_string(cols_));
    }
}

}  // namespace hal
