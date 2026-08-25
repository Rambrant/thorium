//
// What remains of a file that used to hold hal::L4411A's and hal::DSO8064A's
// tests alongside this one: both drivers now live under instruments/ and their
// tests went with them (instruments/l4411a/tests/, instruments/dso8064a/tests/).
// hal::InstrumentId and to_string() over it are hal's own -- generated from the
// rig's instrument.inc, meaningful without any driver present at all -- so this
// is where they stay.
//
#include "hal/instrument.hpp"

#include <gtest/gtest.h>

#include <string>

#include "core/meta.hpp"

//
// Over whatever InstrumentId actually declares, rather than over a list of this
// bench's eight enumerators spelled out by hand.
//
// The list was the original form and it was wrong in a way nothing showed until
// a second deployment existed: hal_tests is the generic library's test target,
// deliberately linked against plain hal so that a test reaching for an
// instrument global or an Apply fails to link (see libs/hal/CMakeLists.txt).
// Naming Dmm2 and Osc1 slipped past that check, because an enumerator is not a
// global -- so the file read as a test of hal's mechanism while actually
// asserting the contents of rig/instrument.inc. Configured against dev/rig,
// which declares one instrument, it failed to compile.
//
// What the mechanism promises is that an enumerator's name comes back verbatim
// and that every enumerator has one, and that is checkable without knowing any
// of the names: reflect over the enum, and require the round trip. A bench with
// eight instruments exercises it eight times, a desk with one exercises it once,
// and neither has to be written down here.
//
// Which enumerators a given deployment *should* have is a real question, and it
// has an owner: the deployment. rig/tests/ and dev/rig/tests/ each assert their
// own instrument list (see dev/rig/tests/test_dev_rig.cpp), which is where a
// claim about one bench belongs.
//
TEST( HalInstrument, ToStringNamesEveryInstrumentTheRigDeclares)
{
    ASSERT_FALSE( core::meta::values<hal::InstrumentId>.empty())
        << "the linking rig's instrument.inc declared no instruments at all";

    for( const auto id : core::meta::values<hal::InstrumentId>)
    {
        const auto name = to_string( id);

        EXPECT_FALSE( name.empty());
        EXPECT_NE( name, "Unknown") << "no enumerator name came back for instrument " << static_cast<int>( id);

        //
        // The round trip, which is the half a name-by-name list could never
        // check: a to_string() that returned some other enumerator's spelling
        // would satisfy every assertion above.
        //
        EXPECT_EQ( core::meta::fromString<hal::InstrumentId>( name), id);
    }
}
