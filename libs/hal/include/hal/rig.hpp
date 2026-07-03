#pragma once

#include "core/rig.hpp"
#include "hal/instruments.hpp"
#include "hal/switch_matrix.hpp"

namespace hal
{
    //
    // A concrete rig wiring together simulated instruments and a switch matrix.
    // This is the "collection of instruments" the physical bench owns; many
    // different DUTs are tested against the same rig. Construction of the real
    // thing (which instruments exist, matrix size) is a fixed rig property.
    //
    class SimRig : public core::IRig
    {
        public:
            SimRig()
                : mMatrix( *this, /*rows=*/8, /*cols=*/8),
                  mScope( core::Voltage{ 0.0 }),
                  mVoltmeter( core::Voltage{ 0.0 })
            {}

            [[nodiscard]]
            auto oscilloscope() -> core::IOscilloscope * override
            {
                return &mScope;
            }

            [[nodiscard]]
            auto voltmeter() -> core::IVoltmeter * override
            {
                return &mVoltmeter;
            }

            [[nodiscard]]
            auto powerSupply() -> core::IPowerSupply * override
            {
                return &mPowerSupply;
            }

            [[nodiscard]]
            auto matrix() -> core::ISwitchMatrix & override
            {
                return mMatrix;
            }

            //
            // Direct access to the concrete instruments, for tests/harness setup that
            // needs to program canned readings. Code above hal never uses these.
            //
            [[nodiscard]]
            auto simScope() -> SimOscilloscope &
            {
                return mScope;
            }

            [[nodiscard]]
            auto simVoltmeter() -> SimVoltmeter &
            {
                return mVoltmeter;
            }

            [[nodiscard]]
            auto simSupply() -> SimPowerSupply &
            {
                return mPowerSupply;
            }

        private:
            //
            // A matrix wrapper that, on close(), tells the simulated instruments
            // which crosspoint is now live -- so a SimVoltmeter programmed per
            // crosspoint returns the right value. On real hardware this coupling is
            // physical; here we model it explicitly.
            //
            class NotifyingMatrix : public SwitchMatrix
            {
                public:
                    NotifyingMatrix( SimRig & rig, const std::uint16_t rows, const std::uint16_t cols) :
                        SwitchMatrix( rows, cols),
                        mRig( rig)
                    {}

                    void close( core::Crosspoint point) override
                    {
                        SwitchMatrix::close( point);
                        mRig.mVoltmeter.setActiveCrosspoint( point);
                    }

                private:
                    SimRig & mRig;
            };

            NotifyingMatrix mMatrix;
            SimOscilloscope mScope;
            SimVoltmeter    mVoltmeter;
            SimPowerSupply  mPowerSupply;
    };
} // namespace hal
