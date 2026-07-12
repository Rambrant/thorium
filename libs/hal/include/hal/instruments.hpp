#pragma once

#include <map>

#include "core/instrument.hpp"
#include "core/switch_matrix.hpp"

namespace hal
{
    using core::quantities::Voltage;

    //
    // Concrete, simulated instruments. On a real rig these would drive actual
    // hardware (VISA/SCPI, vendor SDKs, etc.); here they return programmable
    // canned values so the whole stack is testable without a bench. Each
    // implements a core:: interface, so nothing above hal names these types.

    // A voltmeter whose reading depends on which matrix crosspoint is currently
    // routed to it -- mimicking real hardware, where the value you read reflects
    // whatever the matrix has connected. Falls back to a default reading for any
    // crosspoint not explicitly programmed.
    //
    class SimVoltmeter : public core::IVoltmeter
    {
        public:
            explicit SimVoltmeter( Voltage defaultReading) :
                mDefault( defaultReading)
            {}

            auto setReading( Voltage v ) -> void
            {
                mDefault = v;
            }

            //
            // Program the reading seen when a specific crosspoint is the live route.
            //
            auto setReadingAt( core::Crosspoint point, Voltage voltage) -> void
            {
                mByPoint[key(point)] = voltage;
            }

            //
            // Called by the rig to tell the instrument which crosspoint is live.
            //
            auto setActiveCrosspoint( core::Crosspoint point) -> void
            {
                mActive = point;
            }

            [[nodiscard]]
            auto measureVoltage() -> Voltage override
            {
                auto it = mByPoint.find(key( mActive));

                return it != mByPoint.end() ? it->second : mDefault;
            }

        private:
            [[nodiscard]]
            static auto key( const core::Crosspoint point) -> std::uint32_t
            {
                return (static_cast< std::uint32_t>(point.mRow) << 16) | point.mCol;
            }

            Voltage                          mDefault;
            core::Crosspoint                 mActive{ 0xFFFF, 0xFFFF };
            std::map<std::uint32_t, Voltage> mByPoint;
    };

    class SimOscilloscope : public core::IOscilloscope
    {
        public:
            explicit SimOscilloscope( Voltage level) :
                mLevel(level)
            {}

            auto setLevel( Voltage voltage) -> void
            {
                mLevel = voltage;
            }

            [[nodiscard]]
            auto measureLevel() -> Voltage override
            {
                return mLevel;
            }

        private:
            Voltage mLevel;
    };

    class SimPowerSupply : public core::IPowerSupply
    {
        public:
            auto setOutput( Voltage voltage ) -> void override
            {
                mOutput = voltage;
            }

            auto enable() -> void override
            {
                mEnabled = true;
            }

            auto disable() -> void override
            {
                mEnabled = false;
            }

            [[nodiscard]]
            auto output() const -> Voltage
            {
                return mOutput;
            }

            [[nodiscard]]
            auto enabled() const -> bool
            {
                return mEnabled;
            }

        private:
            Voltage mOutput{ 0.0 };
            bool    mEnabled{ false };
    };
} // namespace hal
