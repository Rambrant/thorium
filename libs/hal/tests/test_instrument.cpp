//
// What remains of a file that used to hold hal::L4411A's and hal::DSO8064's
// tests alongside this one: both drivers now live under instruments/ and their
// tests went with them (instruments/l4411a/tests/, instruments/dso8064/tests/).
// hal::InstrumentId and to_string() over it are hal's own -- generated from the
// rig's instrument.inc, meaningful without any driver present at all -- so this
// is where they stay.
//
#include "hal/instrument.hpp"

#include <gtest/gtest.h>

TEST( HalInstrument, ToStringNamesEachInstrument)
{
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm1), "Dmm1");
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm2), "Dmm2");
    EXPECT_EQ( to_string( hal::InstrumentId::Osc1), "Osc1");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP1), "DcP1");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP2), "DcP2");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP3), "DcP3");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP4), "DcP4");
    EXPECT_EQ( to_string( hal::InstrumentId::AcP1), "AcP1");
}
