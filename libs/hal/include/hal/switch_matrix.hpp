#pragma once

#include <set>

#include "core/switch_matrix.hpp"

namespace hal {

// Concrete switch matrix. On a real rig this would command the physical
// crosspoint switch hardware; here it just tracks which crosspoints are
// closed so the routing logic above can be exercised and asserted on.
//
// The set of *legal* crosspoints (matrix dimensions) is a fixed property of
// the rig's hardware and is enforced here.
class SwitchMatrix : public core::ISwitchMatrix {
public:
    SwitchMatrix(std::uint16_t rows, std::uint16_t cols) : mRows(rows), mCols(cols) {}

    void close(core::Crosspoint point) override {
        validate(point);
        closed_.insert(key(point));
    }

    void open(core::Crosspoint point) override {
        validate(point);
        closed_.erase(key(point));
    }

    void openAll() override { closed_.clear(); }

    [[nodiscard]] bool isClosed(core::Crosspoint point) const override {
        return closed_.count(key(point)) > 0;
    }

    [[nodiscard]] std::uint16_t rows() const { return mRows; }
    [[nodiscard]] std::uint16_t cols() const { return mCols; }

private:
    [[nodiscard]] std::uint32_t key(core::Crosspoint p) const {
        return (static_cast<std::uint32_t>(p.mRow) << 16) | p.mCol;
    }

    void validate(core::Crosspoint p) const;

    std::uint16_t mRows;
    std::uint16_t mCols;
    std::set<std::uint32_t> closed_;
};

}  // namespace hal
