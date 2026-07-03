#include "core/logger.hpp"

#include <gtest/gtest.h>

TEST( CoreLogger, StoresName)
{
    core::Logger logger( "unit-test");
    EXPECT_EQ( logger.name(), "unit-test");
}

TEST( CoreLogLevel, ToStringMapsKnownLevels)
{
    EXPECT_EQ( core::to_string(core::LogLevel::Debug), "DEBUG");
    EXPECT_EQ( core::to_string(core::LogLevel::Info),  "INFO");
    EXPECT_EQ( core::to_string(core::LogLevel::Warn),  "WARN");
    EXPECT_EQ( core::to_string(core::LogLevel::Error), "ERROR");
}
