#pragma once

#include <set>

#include "core/switch_matrix.hpp"

namespace hal
{
    //
    // Concrete switch matrix. On a real rig this would command the physical
    // crosspoint switch hardware; here it just tracks which crosspoints are
    // closed so the routing logic above can be exercised and asserted on.
    //
    // The set of *legal* crosspoints (matrix dimensions) is a fixed property of
    // the rig's hardware and is enforced here.
    //
    class SwitchMatrix : public core::ISwitchMatrix
    {
        public:
            SwitchMatrix( const std::uint16_t rows, const std::uint16_t cols) :
                mRows( rows),
                mCols( cols) {}

            auto close( core::Crosspoint point ) -> void override
            {
                validate(point);
                mClosed.insert(key(point));
            }

            auto open( core::Crosspoint point ) -> void override
            {
                validate(point);
                mClosed.erase(key(point));
            }

            auto openAll() -> void override
            {
                mClosed.clear();
            }

            [[nodiscard]]
            auto isClosed( const core::Crosspoint point) const -> bool override
            {
                return mClosed.count(key(point)) > 0;
            }

            [[nodiscard]]
            auto rows() const -> std::uint16_t
            {
                return mRows;
            }

            [[nodiscard]]
            auto cols() const -> std::uint16_t
            {
                return mCols;
            }

        private:
            [[nodiscard]]
            static auto key( const core::Crosspoint point) -> std::uint32_t
            {
                return (static_cast< std::uint32_t>( point.mRow) << 16) | point.mCol;
            }

            void validate( core::Crosspoint point) const;

            std::uint16_t           mRows;
            std::uint16_t           mCols;
            std::set<std::uint32_t> mClosed;
    };
} // namespace hal
