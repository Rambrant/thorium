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
            if( core::plainText( line).find( needle) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    // The emphasis of a line's leading span -- what the line reads as at a
    // glance, before any quieter trailing description.
    auto styleOfFirst( const std::vector<core::ReportLine> & lines) -> core::Emphasis
    {
        return lines.at( 0).Spans.at( 0).Style;
    }

    // The span carrying a line's trailing description, if it has one.
    auto trailingSpan( const core::ReportLine & line) -> core::ReportSpan
    {
        return line.Spans.size() > 1 ? line.Spans.back() : core::ReportSpan{};
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

    //
    // An observation, exactly like a Measure: a value that came back off the
    // DUT and that a criterion is about to be checked against.
    //
    EXPECT_TRUE( core::isHumanRelevant( eventOf( core::Verb::Read)));

    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Apply)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Remove)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Connect)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Disconnect)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Safe)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Note)));

    //
    // Stimulus, not observation -- what the bench was told to do, which is the
    // machine stream's business. Setup in particular is the one that would be
    // tempting to carry here, since a UART's framing is genuinely interesting
    // when a reply comes back as garbage; it stays out because a human log that
    // reports configuration alongside readings stops being a list of what the
    // DUT did.
    //
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Setup)));
    EXPECT_FALSE( core::isHumanRelevant( eventOf( core::Verb::Write)));
}

//
// Same columns as a Measure, so a script that both measures and reads produces
// a log whose lines still align -- only the verb differs.
//
TEST( CoreReport, ReadLineNamesTheInterfaceItsPayloadAndItsInstrument)
{
    auto event = eventOf( core::Verb::Read);
    event.Subject    = "Console";
    event.Detail     = "RS232 debug console";
    event.Instrument = "Ser1";
    event.Value      = "\"0xF5\\r\"";

    const auto lines = core::humanEventLines( event);

    ASSERT_EQ( lines.size(), 1u);

    const auto text = core::plainText( lines.front());

    EXPECT_NE( text.find( "read"),        std::string::npos);
    EXPECT_NE( text.find( "Console"),     std::string::npos);
    EXPECT_NE( text.find( "0xF5"),        std::string::npos);
    EXPECT_NE( text.find( "(Ser1)"),      std::string::npos);
    EXPECT_EQ( text.find( "measure"),     std::string::npos);
}

//
// A check that could not be made (core::Fail) is a Verify event with no value
// and no tolerance-defying number behind it, so it renders through the same
// branch and lines up in the same columns as the checks around it -- which is
// the whole reason it posts as a Verb::Verify rather than as a verb of its own
// (see core/verify.hpp).
//
// The event is built by hand here, as every other rendering test in this file
// does; what Fail actually posts is pinned in core/tests/test_verify.cpp.
//
TEST( CoreReport, AnUncheckedCriterionRendersAsAFailedCheckWithNoValue)
{
    auto event = eventOf( core::Verb::Verify);
    event.Subject       = "FS_Console_Ready";
    event.SubjectGroup  = "FS_Console_1";
    event.Detail        = "console reply is 4 bytes, too short to hold a status byte";
    event.Value         = "<unchecked>";
    event.CriterionText = "bits 0x08 set";
    event.Passed        = false;

    const auto lines = core::humanEventLines( event);

    ASSERT_EQ( lines.size(), 1u);

    const auto text = core::plainText( lines.front());

    EXPECT_NE( text.find( "verify"),           std::string::npos);
    EXPECT_NE( text.find( "FS_Console_Ready"), std::string::npos);
    EXPECT_NE( text.find( "<unchecked>"),      std::string::npos);
    EXPECT_NE( text.find( "[FAIL]"),           std::string::npos);

    //
    // Both of these matter beside the verdict: what was required is still
    // stated, so the row can be read without the criteria table open, and the
    // reason is what says the [FAIL] is an absence of evidence rather than a
    // finding about the DUT.
    //
    EXPECT_NE( text.find( "bits 0x08 set"), std::string::npos);
    EXPECT_NE( text.find( "too short"),     std::string::npos);
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
    EXPECT_TRUE( core::plainText( lines[ 0]).starts_with( "\t")) << "log output nests under its test";
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

//
// The whole reason core/predicate_text.hpp exists: a reader has to be able to
// agree with a [FAIL] from the log line alone. Measured *and* required, side by
// side, with no criteria file open.
//
TEST( CoreReport, VerifyLineStatesWhatWasRequiredNotJustWhatWasMeasured)
{
    auto event = eventOf( core::Verb::Verify);
    event.Subject       = "FS_Supply_5V0";
    event.SubjectGroup  = "FS_Supply_1";
    event.Value         = "0 V";
    event.CriterionText = "= 5 V +/-0.05 V";
    event.Detail        = "5Vdc port within +/-50mV";
    event.Passed        = false;

    const auto lines = core::humanEventLines( event);

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_TRUE( containsText( lines, "0 V"));               // measured
    EXPECT_TRUE( containsText( lines, "= 5 V +/-0.05 V"));   // required
    EXPECT_TRUE( containsText( lines, "[FAIL]"));

    // The description stays too -- it is the traceability prose, and it is a
    // different claim from the tolerance beside it.
    EXPECT_TRUE( containsText( lines, "5Vdc port within +/-50mV"));
}

//
// A criterion is named by its own CRITERIA group and CRIT id, qualified: the id
// alone is only unique within its group, and "FS_Supply_1::FS_Supply_5V0" is the
// spelling a test spec traces to and that can be searched for in a criteria
// file.
//
TEST( CoreReport, VerifyLineQualifiesTheCriterionWithItsCriteriaGroup)
{
    auto event = eventOf( core::Verb::Verify);
    event.Subject      = "FS_Supply_5V0";
    event.SubjectGroup = "FS_Supply_1";
    event.Passed       = true;

    EXPECT_TRUE( containsText( core::humanEventLines( event), "FS_Supply_1::FS_Supply_5V0"));

    // An ad-hoc check with no criteria group is named by its id alone rather
    // than gaining a stray "::".
    event.SubjectGroup.clear();

    const auto lines = core::humanEventLines( event);

    EXPECT_TRUE( containsText( lines, "FS_Supply_5V0"));
    EXPECT_FALSE( containsText( lines, "::"));
}

//
// A predicate that can't describe itself (a lambda -- see
// core/predicate_text.hpp) leaves the required column empty rather than
// producing a line with a hole punched in it.
//
TEST( CoreReport, VerifyLineSurvivesAnUndescribableCriterion)
{
    auto event = eventOf( core::Verb::Verify);
    event.Subject = "AdHoc";
    event.Value   = "1.5";
    event.Passed  = true;

    const auto lines = core::humanEventLines( event);

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_TRUE( containsText( lines, "AdHoc"));
    EXPECT_TRUE( containsText( lines, "1.5"));
    EXPECT_TRUE( containsText( lines, "[PASS]"));
}

//
// Descriptions are the bulk of the characters on a line and the least of its
// information -- fixed text from dut/adapter.inc and the criteria tables,
// identical every run. They are carried in their own quiet span so a reader's
// eye can travel down the values and verdicts without them competing.
//
TEST( CoreReport, DescriptionsAreCarriedInTheirOwnQuietSpan)
{
    auto measure = eventOf( core::Verb::Measure);
    measure.Subject    = "Output5V";
    measure.Instrument = "Dmm1";
    measure.Value      = "5.021 V";
    measure.Detail     = "5Vdc supply port";

    const auto measureLines = core::humanEventLines( measure);

    ASSERT_EQ( measureLines.at( 0).Spans.size(), 2u);
    EXPECT_EQ( trailingSpan( measureLines.at( 0)).Style, core::Emphasis::Detail);
    EXPECT_EQ( trailingSpan( measureLines.at( 0)).Text,  "5Vdc supply port");

    // The reading itself keeps the line's own weight -- it is the information.
    EXPECT_EQ( styleOfFirst( measureLines), core::Emphasis::Plain);

    auto verify = eventOf( core::Verb::Verify);
    verify.Subject = "FS_Supply_5V0";
    verify.Value   = "0 V";
    verify.Detail  = "5Vdc port within +/-50mV";
    verify.Passed  = false;

    const auto verifyLines = core::humanEventLines( verify);

    ASSERT_EQ( verifyLines.at( 0).Spans.size(), 2u);
    EXPECT_EQ( trailingSpan( verifyLines.at( 0)).Style, core::Emphasis::Detail);

    // The verdict stays in the verdict's own colour -- greying the description
    // must not grey what a reader is scanning for.
    EXPECT_EQ( styleOfFirst( verifyLines), core::Emphasis::Fail);
}

TEST( CoreReport, GroupAndTestDescriptionsAreQuietToo)
{
    const auto groupLines = core::humanGroupHeadingLines( "OutputVoltage", "Tests validating DUT output voltage rails");

    ASSERT_EQ( groupLines.at( 0).Spans.size(), 2u);
    EXPECT_EQ( styleOfFirst( groupLines),                    core::Emphasis::Heading);
    EXPECT_EQ( trailingSpan( groupLines.at( 0)).Style,       core::Emphasis::Detail);
    EXPECT_EQ( trailingSpan( groupLines.at( 0)).Text,        "Tests validating DUT output voltage rails");

    const auto testLines = core::humanTestHeadingLines( "SupplyRail", "Verify supply rail voltages");

    ASSERT_EQ( testLines.at( 0).Spans.size(), 2u);
    EXPECT_EQ( styleOfFirst( testLines),                     core::Emphasis::Heading);
    EXPECT_EQ( trailingSpan( testLines.at( 0)).Style,        core::Emphasis::Detail);
}

//
// No description means no span for one, rather than an empty span -- a sink must
// never emit a colour change for nothing.
//
TEST( CoreReport, ALineWithNoDescriptionHasASingleSpan)
{
    auto event = eventOf( core::Verb::Measure);
    event.Subject = "Output5V";
    event.Value   = "5.021 V";

    EXPECT_EQ( core::humanEventLines( event).at( 0).Spans.size(), 1u);
    EXPECT_EQ( core::humanGroupHeadingLines( "OutputVoltage", {}).at( 0).Spans.size(), 1u);
}

//
// The body's outer level: a group states its own name and description once, and
// unindented -- its tests nest under it rather than each repeating "Group::Test".
//
TEST( CoreReport, GroupNameAndDescriptionAreTheOuterHeading)
{
    const auto lines = core::humanGroupHeadingLines( "OutputVoltage", "Tests validating DUT output voltage rails");

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_EQ( styleOfFirst( lines), core::Emphasis::Heading);
    EXPECT_EQ( core::plainText( lines[ 0]), "OutputVoltage Tests validating DUT output voltage rails");

    // Unindented -- this is the level everything else hangs off.
    EXPECT_FALSE( core::plainText( lines[ 0]).starts_with( "\t"));
}

TEST( CoreReport, GroupWithNoDescriptionIsJustItsName)
{
    const auto lines = core::humanGroupHeadingLines( "OutputVoltage", {});

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_EQ( core::plainText( lines[ 0]), "OutputVoltage");
}

//
// The inner level: indented one tab, and naming only the test -- the group
// heading above has already said which group this is.
//
TEST( CoreReport, TestNameAndDescriptionAreTheIndentedHeading)
{
    const auto lines = core::humanTestHeadingLines( "SupplyRail", "Verify supply rail voltages");

    ASSERT_EQ( lines.size(), 1u);
    EXPECT_EQ( styleOfFirst( lines), core::Emphasis::Heading);
    EXPECT_EQ( core::plainText( lines[ 0]), "\tSupplyRail Verify supply rail voltages");
}

TEST( CoreReport, TestResultRestatesTheTestNameWithItsVerdict)
{
    const auto passed = core::humanTestResultLines( "SupplyRail", true);

    EXPECT_EQ( styleOfFirst( passed), core::Emphasis::Pass);
    EXPECT_TRUE( containsText( passed, "SupplyRail"));
    EXPECT_TRUE( containsText( passed, "[PASS]"));

    const auto failed = core::humanTestResultLines( "SupplyRail", false);

    EXPECT_EQ( styleOfFirst( failed), core::Emphasis::Fail);
    EXPECT_TRUE( containsText( failed, "[FAIL]"));
}

//
// The blank line that closes a test's block trails the result rather than
// leading the next heading -- a run's last test has no next heading, and a log
// whose final block runs straight into the summary reads as truncated.
//
TEST( CoreReport, TestResultIsFollowedByTheBlankLineThatClosesItsBlock)
{
    const auto lines = core::humanTestResultLines( "SupplyRail", true);

    ASSERT_EQ( lines.size(), 2u);
    EXPECT_TRUE( core::plainText( lines[ 1]).empty());
}

namespace
{
    auto fullRunInfo() -> core::RunInfo
    {
        core::RunInfo info;
        info.FrameworkName    = "Thorium";
        info.FrameworkVersion = "0.1.0";
        info.CriteriaVariant  = "stress";
        info.DutName          = "DeviceX";
        info.DutSerial        = "SN-000123";
        info.RigName          = "bench-7";
        info.Operator         = "thomas";
        info.HostName         = "rig-console";
        info.CommandLine      = "run_scripts --select=SupplyRail";
        info.SuiteVersion     = "7e4ff59";
        info.DutVersion       = "7e4ff59";
        info.RigVersion       = "7e4ff59";
        info.StartedUtc       = "2026-08-01T09:14:02.371Z";
        info.StartedLocal     = "Sat 01 Aug 2026 11:14:02 CEST";
        return info;
    }
} // namespace

//
// The title answers the two questions somebody holding a stack of reports has:
// which unit, and when. Local time, because this line is for the person who was
// in the room -- the UTC instant is a row below for whoever is comparing runs.
//
TEST( CoreReport, TitleIsTheDeviceFollowedByTheLocalTime)
{
    const auto lines = core::humanHeaderLines( fullRunInfo());

    ASSERT_FALSE( lines.empty());
    EXPECT_EQ( styleOfFirst( lines), core::Emphasis::Heading);
    EXPECT_EQ( core::plainText( lines[ 0]), "DeviceX -- Sat 01 Aug 2026 11:14:02 CEST");
}

TEST( CoreReport, HeaderCarriesTheFullTraceabilityBag)
{
    const auto lines = core::humanHeaderLines( fullRunInfo());

    EXPECT_TRUE( containsText( lines, "SN-000123"));
    EXPECT_TRUE( containsText( lines, "thomas"));
    EXPECT_TRUE( containsText( lines, "stress"));
    EXPECT_TRUE( containsText( lines, "Thorium 0.1.0"));
    EXPECT_TRUE( containsText( lines, "7e4ff59"));
    EXPECT_TRUE( containsText( lines, "2026-08-01T09:14:02.371Z"));
    EXPECT_TRUE( containsText( lines, "run_scripts --select=SupplyRail"));
}

//
// One row when suite/, dut/ and rig/ share a revision -- the normal case, where
// three identical lines would be noise -- and three when they diverge, which is
// when each one starts to matter separately.
//
TEST( CoreReport, ContentRevisionsCollapseToOneRowWhenTheyAgree)
{
    EXPECT_TRUE( containsText( core::humanHeaderLines( fullRunInfo()), "Suite/DUT/rig"));

    auto diverged = fullRunInfo();
    diverged.RigVersion = "a1b2c3d";

    const auto lines = core::humanHeaderLines( diverged);

    EXPECT_FALSE( containsText( lines, "Suite/DUT/rig"));
    EXPECT_TRUE(  containsText( lines, "Suite version"));
    EXPECT_TRUE(  containsText( lines, "DUT version"));
    EXPECT_TRUE(  containsText( lines, "Rig version"));
    EXPECT_TRUE(  containsText( lines, "a1b2c3d"));
}

//
// The header is a report form, not a summary: every row is present whether or
// not the run had a value for it. A blank field says "nobody filled this in",
// which is itself the finding, and it keeps two runs of the same suite
// comparable line for line.
//
TEST( CoreReport, HeaderKeepsItsRowsEvenWhenAFieldIsUnset)
{
    auto info = fullRunInfo();
    info.DutSerial.clear();

    const auto lines = core::humanHeaderLines( info);

    EXPECT_TRUE( containsText( lines, "DUT serial"));

    // Present, and empty -- not filled in with a placeholder that could be
    // mistaken for a real serial.
    EXPECT_FALSE( containsText( lines, "SN-000123"));

    // Same row count either way, which is the property that makes two reports
    // diffable.
    EXPECT_EQ( lines.size(), core::humanHeaderLines( fullRunInfo()).size());
}

TEST( CoreReport, SummaryStatesTheRunVerdict)
{
    EXPECT_TRUE( containsText( core::humanSummaryLines( true),  "ALL SCRIPTS PASSED"));
    EXPECT_TRUE( containsText( core::humanSummaryLines( false), "SOME SCRIPTS FAILED"));
}

//
// A subject wider than its column must not run into the value beside it --
// "...at nominal28 V" reads as a corrupted value rather than as a wide field.
//
// Latent for as long as the column has existed: nothing enforces that a
// criterion id or a DUT point name fits 28 characters, and a long one silently
// ate the separator. It surfaced while an earlier draft of the three-argument
// Verify put an ad-hoc check's prose in this column; that prose now goes to the
// description slot instead (see core/verify.hpp), so this is back to guarding
// the case it always should have.
//
// The claim is separation, not alignment: a long subject is allowed to push the
// rest of its line right. What it may not do is abut it.
//
TEST( CoreReport, AnOverlongSubjectIsStillSeparatedFromTheValue)
{
    auto event = eventOf( core::Verb::Verify);
    event.Subject       = "Secondary backup supply at nominal";   // wider than the subject column
    event.Value         = "28 V";
    event.CriterionText = "= 28 V +/-0.1 V";
    event.Passed        = true;

    const auto text = core::plainText( core::humanEventLines( event).front());

    EXPECT_NE( text.find( "Secondary backup supply at nominal 28 V"), std::string::npos)
        << "subject and value ran together in: " << text;
}

//
// The same guarantee for a measurement, which has the same column and can hit
// it with a long DUT point name -- this was latent before any ad-hoc check
// existed to expose it.
//
TEST( CoreReport, AnOverlongPointNameIsStillSeparatedFromItsReading)
{
    auto event = eventOf( core::Verb::Measure);
    event.Subject = "AuxiliaryBusReferenceRailProbe";
    event.Value   = "3.3 V";

    const auto text = core::plainText( core::humanEventLines( event).front());

    EXPECT_NE( text.find( "AuxiliaryBusReferenceRailProbe 3.3 V"), std::string::npos)
        << "point name and reading ran together in: " << text;
}
