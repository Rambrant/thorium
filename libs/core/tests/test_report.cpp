#include "core/report.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{
    auto eventOf( const core::Verb method) -> core::JournalEvent
    {
        core::JournalEvent event;
        event.Method = method;
        return event;
    }

    //
    // Whether any line in the set contains needle -- the human stream's layout
    // is a column format, so asserting on substrings rather than whole lines
    // keeps these tests from failing every time a column width is adjusted,
    // while still pinning down what must be present.
    //
    auto containsText( const std::vector<core::ReportLine> & lines, const std::string & needle) -> bool
    {
        for( const auto & line : lines)
        {
            if( line.Text.find( needle) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    auto styleOfFirst( const std::vector<core::ReportLine> & lines) -> core::Emphasis
    {
        return lines.at( 0).Style;
    }
} // namespace

//
// The filtering rule the user-facing log is built on: measurements and checks,
// nothing else. The sourcing/routing verbs and the safing pass go to the
// machine log only.
//
TEST( CoreReport, HumanStreamCarriesMeasureAndVerifyOnly)
{
    EXPECT_TRUE( core::isHumanRelevant( eventOf( core::Verb::Measure)));
    EXPECT_TRUE( core::isHumanRelevant( eventOf( core::Verb::Verify)));

    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Apply)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Remove)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Connect)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Disconnect)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Safe)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Note)));
}

TEST( CoreReport, EventsTheHumanStreamDoesNotCarryProduceNoLines)
{
    EXPECT_TRUE( core::humanEventLines( eventOf( core::Verb::Connect)).empty());
    EXPECT_TRUE( core::humanEventLines( eventOf( core::Verb::Safe)).empty());
}

TEST( CoreReport, MeasureLineNamesThePointItsValueAndItsInstrument)
{
    auto event = eventOf( core::Verb::Measure);
    event.Subject    = "Output5V";
    event.Detail     = "5Vdc supply port";
    event.Instrument = "Dmm1";
    event.Value      = "5.021 V";

    const auto lines = core::humanEventLines( event);

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_EQ( styleOfFirst( lines), core::Emphasis::Plain);
    EXPECT_TRUE( containsText( lines, "measure"));
    EXPECT_TRUE( containsText( lines, "Output5V"));
    EXPECT_TRUE( containsText( lines, "5.021 V"));
    EXPECT_TRUE( containsText( lines, "(Dmm1)"));
    EXPECT_TRUE( containsText( lines, "5Vdc supply port"));
}

//
// Both markers, not either: colour alone is lost in a black-and-white printout
// or to a reader who can't distinguish red from green, and the bracketed word
// alone has to be hunted for down a column.
//
TEST( CoreReport, VerifyResultIsMarkedByBothTextAndEmphasis)
{
    auto passing = eventOf( core::Verb::Verify);
    passing.Subject = "FS_Supply_5V0";
    passing.Value   = "5.021 V";
    passing.Detail  = "5Vdc port within +/-50mV";
    passing.Passed  = true;

    const auto passLines = core::humanEventLines( passing);

    ASSERT_EQ( passLines.size(), 1u);
    EXPECT_EQ( styleOfFirst( passLines), core::Emphasis::Pass);
    EXPECT_TRUE( containsText( passLines, "[PASS]"));
    EXPECT_TRUE( containsText( passLines, "FS_Supply_5V0"));
    EXPECT_FALSE( containsText( passLines, "[FAIL]"));

    auto failing = passing;
    failing.Passed = false;

    const auto failLines = core::humanEventLines( failing);

    ASSERT_EQ( failLines.size(), 1u);
    EXPECT_EQ( styleOfFirst( failLines), core::Emphasis::Fail);
    EXPECT_TRUE( containsText( failLines, "[FAIL]"));
}

TEST( CoreReport, TestNameIsItsOwnHeadingLine)
{
    const auto lines = core::humanTestHeadingLines( "OutputVoltage", "SupplyRail", "Verify supply rail voltages");

    EXPECT_TRUE( containsText( lines, "OutputVoltage::SupplyRail"));
    EXPECT_TRUE( containsText( lines, "Verify supply rail voltages"));

    // A heading somewhere in the set, so it is findable by style as well as text.
    bool sawHeading = false;

    for( const auto & line : lines)
    {
        sawHeading = sawHeading || line.Style == core::Emphasis::Heading;
    }

    EXPECT_TRUE( sawHeading);
}

TEST( CoreReport, TestResultRestatesTheTestNameWithItsVerdict)
{
    const auto passed = core::humanTestResultLines( "OutputVoltage", "SupplyRail", true);

    ASSERT_EQ( passed.size(), 1u);
    EXPECT_EQ( styleOfFirst( passed), core::Emphasis::Pass);
    EXPECT_TRUE( containsText( passed, "OutputVoltage::SupplyRail"));
    EXPECT_TRUE( containsText( passed, "[PASS]"));

    const auto failed = core::humanTestResultLines( "OutputVoltage", "SupplyRail", false);

    EXPECT_EQ( styleOfFirst( failed), core::Emphasis::Fail);
    EXPECT_TRUE( containsText( failed, "[FAIL]"));
}

TEST( CoreReport, HeaderCarriesTheFullTraceabilityBag)
{
    core::RunInfo info;
    info.FrameworkName    = "Thorium";
    info.FrameworkVersion = "0.1.0";
    info.CriteriaVariant  = "stress";
    info.DutName          = "DeviceX_StdAdapter";
    info.DutSerial        = "SN-000123";
    info.RigName          = "bench-7";
    info.Operator         = "thomas";
    info.HostName         = "rig-console";
    info.CommandLine      = "run_scripts --select=SupplyRail";
    info.StartedUtc       = "2026-07-30T09:14:02.371Z";

    const auto lines = core::humanHeaderLines( info);

    EXPECT_TRUE( containsText( lines, "Thorium 0.1.0"));
    EXPECT_TRUE( containsText( lines, "DeviceX_StdAdapter"));
    EXPECT_TRUE( containsText( lines, "SN-000123"));
    EXPECT_TRUE( containsText( lines, "bench-7"));
    EXPECT_TRUE( containsText( lines, "stress"));
    EXPECT_TRUE( containsText( lines, "thomas"));
    EXPECT_TRUE( containsText( lines, "rig-console"));
    EXPECT_TRUE( containsText( lines, "2026-07-30T09:14:02.371Z"));
    EXPECT_TRUE( containsText( lines, "run_scripts --select=SupplyRail"));
}

//
// An absent serial means nobody said which unit was in the fixture; a row
// reading "DUT serial" with nothing after it would invite a reader to assume
// the value was blank rather than never supplied.
//
TEST( CoreReport, HeaderOmitsFieldsNobodySupplied)
{
    core::RunInfo info;
    info.FrameworkName    = "Thorium";
    info.FrameworkVersion = "0.1.0";
    info.DutName          = "DeviceX_StdAdapter";

    const auto lines = core::humanHeaderLines( info);

    EXPECT_TRUE( containsText( lines, "DeviceX_StdAdapter"));
    EXPECT_FALSE( containsText( lines, "DUT serial"));
    EXPECT_FALSE( containsText( lines, "Operator"));
}

TEST( CoreReport, SummaryStatesTheRunVerdict)
{
    EXPECT_TRUE( containsText( core::humanSummaryLines( true),  "ALL SCRIPTS PASSED"));
    EXPECT_TRUE( containsText( core::humanSummaryLines( false), "SOME SCRIPTS FAILED"));
}
