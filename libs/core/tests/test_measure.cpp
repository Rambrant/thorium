//
// Created by Thomas Rambrant on 2026-07-05.
//
#include "core/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Voltage;

namespace
{
    class FakeMatrix : public core::ISwitchMatrix
    {
        public:
            void open( core::Crosspoint ) override {}

            void close( core::Crosspoint point ) override
            {
                lastClosed = point;
                ++closeCount;
            }

            void openAll() override
            {
                ++openAllCount;
            }

            [[nodiscard]]
            auto isClosed( core::Crosspoint point) const -> bool override
            {
                return point == lastClosed;
            }

            core::Crosspoint lastClosed{ 0xFFFF, 0xFFFF };
            int              closeCount{ 0 };
            int              openAllCount{ 0 };
    };

    class FakeVoltmeter : public core::IVoltmeter
    {
        public:
            Voltage reading{ 0.0 };

            [[nodiscard]]
            auto measureVoltage() -> Voltage override
            {
                return reading;
            }
    };

    class FakeScope : public core::IOscilloscope
    {
        public:
            Voltage level{ 0.0 };

            [[nodiscard]]
            auto measureLevel() -> Voltage override
            {
                return level;
            }
    };
} // namespace

TEST( CoreMeasure, RoutesMatrixThenReadsVoltmeter)
{
    FakeMatrix    matrix;
    FakeVoltmeter voltmeter;
    voltmeter.reading = Voltage{ 5.01 };

    const auto result = core::Measure( matrix, core::Crosspoint{ 3, 7 }, voltmeter);

    EXPECT_DOUBLE_EQ( result.value(), 5.01);
    EXPECT_EQ( matrix.lastClosed, (core::Crosspoint{ 3, 7 }));
    EXPECT_EQ( matrix.openAllCount, 1);
}

TEST( CoreMeasure, RoutesMatrixThenReadsOscilloscope)
{
    FakeMatrix matrix;
    FakeScope  scope;
    scope.level = Voltage{ 3.28 };

    const auto result = core::Measure( matrix, core::Crosspoint{ 1, 2 }, scope);

    EXPECT_DOUBLE_EQ( result.value(), 3.28);
    EXPECT_EQ( matrix.lastClosed, (core::Crosspoint{ 1, 2 }));
    EXPECT_EQ( matrix.openAllCount, 1);
}

TEST( CoreMeasure, ClearsMatrixBeforeClosingIntendedPath)
{
    FakeMatrix    matrix;
    FakeVoltmeter voltmeter;

    const auto reading = core::Measure( matrix, core::Crosspoint{ 0, 0 }, voltmeter);

    EXPECT_EQ( matrix.openAllCount, 1);
    EXPECT_EQ( matrix.closeCount, 1);
    EXPECT_DOUBLE_EQ( reading.value(), 0.0);
}