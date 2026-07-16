#include "core/port.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    //
    // A minimal instrument satisfying exactly what core::Port needs
    // (rawMeasure<Q>() + id()), with nothing else -- demonstrating that
    // core::Port has no idea what a "real" instrument (hal::Dmm etc.) looks
    // like.
    //
    class MockInstrument
    {
        public:
            explicit MockInstrument( int id) : mId( id) {}

            [[nodiscard]] auto id() const -> int { return mId; }

            auto setSimulatedVoltage( Voltage v) -> void { mVoltage = v; }

            template<core::quantities::QuantityType Q>
            [[nodiscard]] auto rawMeasure( const core::MeasureSetup<Q> & ) -> Q { return mVoltage; }

        private:
            int     mId;
            Voltage mVoltage{};
    };
} // namespace

TEST( CorePort, RawMeasureForwardsToTheInstrument)
{
    MockInstrument instrument{ 7 };
    instrument.setSimulatedVoltage( 5.02_V);

    core::Port<Voltage, MockInstrument> port{ instrument };

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 5.02);
}

TEST( CorePort, InstrumentIdForwardsToTheInstrument)
{
    MockInstrument instrument{ 7 };
    core::Port<Voltage, MockInstrument> port{ instrument };

    EXPECT_EQ( port.instrumentId(), 7);
}
