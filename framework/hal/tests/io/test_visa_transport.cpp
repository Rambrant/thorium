//
// hal::io::VisaTransport's tests -- which are the tests of everything about it
// that is not the vendor library.
//
// That division is worth being explicit about, because it is unusual in this
// tree and it is not an excuse. VISA is closed software that is installed on
// the bench and on no machine this repository is developed or tested on, so
// nothing here opens a session through it: there is no `viOpen` in this file
// and no fake VISA either, since a fake of a library whose real behaviour
// cannot be observed would only assert this author's guesses back at him.
//
// What is tested is the half that is ordinary code, and it happens to be the
// half where the mistakes actually live:
//
//   the resource strings built from a hal::Address -- one typo in
//   "GPIB0::14::INSTR" is an instrument that cannot be opened, and it is a
//   typo no compiler and no reviewer reliably catches.
//
//   the serial-number matching that picks a USB instrument out of what VISA
//   enumerated, including the field counting, which is off-by-one bait.
//
//   the refusals: no library, an address VISA cannot be given, and the
//   Simulated address that is not a gap.
//
// The other half -- whether viOpen on a real USBTMC meter behaves -- is
// confirmed by running it on the bench. See dev/README.md's table of which
// question is answered where, and note that the same division applied to the
// socket transport: its SCPI was asserted against a fake and then confirmed
// against a real listener. This one is missing the second step until somebody
// plugs a meter in.
//
#include "hal/io/transport.hpp"
#include "hal/io/visa_transport.hpp"

#include <gtest/gtest.h>

#include <string>

namespace
{
    //
    // Whether this machine has a VISA at all, which decides what several tests
    // below can assert. Answered once and named, rather than repeated as
    // `visaLibrary().has_value()` at each site, so that a reader sees
    // immediately that these tests are deliberately conditional rather than
    // flaky.
    //
    // Every one of them asserts something in *both* directions -- see
    // OpenTransport's own tests, which assert that a bus kind is supported
    // exactly when a library was found. A test that simply skipped itself on a
    // developer machine would be a test that nobody ever saw fail.
    //
    [[nodiscard]]
    auto haveVisa() -> bool
    {
        return hal::io::visaLibrary().has_value();
    }
} // namespace

//
// ---------------------------------------------------------------------------
// Resource strings
// ---------------------------------------------------------------------------
//

TEST( VisaResource, AGpibAddressIsABoardAndAPrimaryAddress)
{
    EXPECT_EQ( hal::io::visaResourceFor( hal::Gpib{ 0, 14, {} } ), "GPIB0::14::INSTR");
    EXPECT_EQ( hal::io::visaResourceFor( hal::Gpib{ 1,  5, {} } ), "GPIB1::5::INSTR");
}

//
// The secondary address is a field in the resource name rather than a separate
// concept -- and is omitted when the instrument has none, which is not the same
// string as one with a zero in it.
//
TEST( VisaResource, AGpibSecondaryAddressIsAThirdFieldWhenThereIsOne)
{
    EXPECT_EQ( hal::io::visaResourceFor( hal::Gpib{ 0, 9, 3 } ), "GPIB0::9::3::INSTR");
    EXPECT_EQ( hal::io::visaResourceFor( hal::Gpib{ 0, 9, 0 } ), "GPIB0::9::0::INSTR");
}

//
// ::SOCKET rather than ::INSTR, which are different instruments as far as VISA
// is concerned: ::INSTR over TCPIP is VXI-11 or HiSLIP, with a session
// handshake, where hal::Lan means the raw port. Getting this wrong produces a
// resource that opens against many instruments and then times out on every
// read.
//
TEST( VisaResource, ALanAddressIsARawSocketResourceAndNotAnInstrOne)
{
    EXPECT_EQ( hal::io::visaResourceFor( hal::Lan{ "bench-dmm1" } ), "TCPIP0::bench-dmm1::5025::SOCKET");
    EXPECT_EQ( hal::io::visaResourceFor( hal::Lan{ "192.168.0.7", 5023 } ),
        "TCPIP0::192.168.0.7::5023::SOCKET");
}

TEST( VisaResource, AWindowsComPortBecomesAnAsrlResource)
{
    EXPECT_EQ( hal::io::visaResourceFor( hal::Serial{ "COM3" } ),  "ASRL3::INSTR");
    EXPECT_EQ( hal::io::visaResourceFor( hal::Serial{ "COM11" } ), "ASRL11::INSTR");
    EXPECT_EQ( hal::io::visaResourceFor( hal::Serial{ "com3" } ),  "ASRL3::INSTR");
}

//
// An ASRL resource written out by a rig that knows its own VISA aliases is
// passed through -- which is the only way a Unix serial device can be named
// here at all.
//
TEST( VisaResource, AnAsrlResourceIsPassedThroughUnchanged)
{
    EXPECT_EQ( hal::io::visaResourceFor( hal::Serial{ "ASRL1::INSTR" } ), "ASRL1::INSTR");
}

//
// A Unix device path is refused rather than guessed at: VISA's mapping from
// /dev/ttyUSB0 to an ASRL index is part of that installation's configuration
// and is not derivable from the path. Refusing produces a message telling the
// rig how to say it; guessing would produce a run against the wrong port.
//
TEST( VisaResource, AUnixDevicePathIsNotTranslatable)
{
    EXPECT_FALSE( hal::io::visaResourceFor( hal::Serial{ "/dev/ttyUSB0" } ).has_value());
    EXPECT_FALSE( hal::io::visaResourceFor( hal::Serial{ "COM" } ).has_value());
    EXPECT_FALSE( hal::io::visaResourceFor( hal::Serial{ "COMx" } ).has_value());
}

//
// The two addresses with no resource string, for two unrelated reasons. A USB
// instrument is *found* -- a resource needs vendor and product ids that
// hal::Usb deliberately does not carry -- and a Simulated one is not an
// instrument.
//
TEST( VisaResource, UsbAndSimulatedHaveNoResourceStringOfTheirOwn)
{
    EXPECT_FALSE( hal::io::visaResourceFor( hal::Usb{ "MY60012345" } ).has_value());
    EXPECT_FALSE( hal::io::visaResourceFor( hal::Simulated{} ).has_value());
}

//
// ---------------------------------------------------------------------------
// Finding a USB instrument by serial number
// ---------------------------------------------------------------------------
//

TEST( VisaUsbSerial, IsTheFourthFieldOfAUsbResource)
{
    EXPECT_EQ( hal::io::usbSerialOf( "USB0::0x2A8D::0x1401::MY60012345::INSTR"), "MY60012345");
}

//
// The interface number is an optional *fifth* field, so counting from the left
// is what keeps this right -- counting back from ::INSTR would return "0" for
// this one, which matches no meter's label and would make a plugged-in
// instrument unfindable.
//
TEST( VisaUsbSerial, SurvivesTheOptionalInterfaceNumberField)
{
    EXPECT_EQ( hal::io::usbSerialOf( "USB0::0x2A8D::0x1401::MY60012345::0::INSTR"), "MY60012345");
}

TEST( VisaUsbSerial, ReadsAnyUsbBoardNumber)
{
    EXPECT_EQ( hal::io::usbSerialOf( "USB1::0x0957::0x0607::MY44012345::INSTR"), "MY44012345");
}

//
// Anything not shaped like a USB resource is empty rather than an error: VISA
// enumerating something unexpected is not a reason to fail a run, it is a
// reason for that entry not to match.
//
TEST( VisaUsbSerial, IsEmptyForAnythingThatIsNotAUsbResource)
{
    EXPECT_TRUE( hal::io::usbSerialOf( "GPIB0::14::INSTR").empty());
    EXPECT_TRUE( hal::io::usbSerialOf( "TCPIP0::bench-dmm1::5025::SOCKET").empty());
    EXPECT_TRUE( hal::io::usbSerialOf( "USB0::0x2A8D").empty());
    EXPECT_TRUE( hal::io::usbSerialOf( "").empty());
}

//
// ---------------------------------------------------------------------------
// What happens with, and without, a VISA installation
// ---------------------------------------------------------------------------
//

//
// The one test in this file that behaves differently on the bench and on a
// developer's machine, and it asserts the difference rather than tolerating it.
//
TEST( VisaLibrary, IsNamedWhenOneLoadsAndAbsentWhenNoneDoes)
{
    const auto library = hal::io::visaLibrary();

    if( library)
    {
        //
        // Named, not merely present: "which VISA" is the question an operator
        // with both Keysight's and NI's stacks installed actually has.
        //
        EXPECT_FALSE( library->empty());
    }
    else
    {
        //
        // No VISA here, which is the state every machine this repository is
        // developed on is in -- and the state in which GPIB, USB and serial
        // must refuse rather than crash. Asserted just below.
        //
        SUCCEED() << "no VISA installed on this machine";
    }
}

//
// A USB instrument is what this landed for, so it gets the assertion that
// matters: on a machine with VISA the address is openable in principle, and on
// one without it is refused with a message naming what to install.
//
TEST( OpenTransportVisa, UsbIsSupportedExactlyWhenAVisaLibraryIsInstalled)
{
    EXPECT_EQ( hal::io::isSupported( hal::Usb{ "MY60012345" } ), haveVisa());
    EXPECT_EQ( hal::io::isSupported( hal::Gpib{ 0, 14, {} } ),   haveVisa());
}

//
// LAN never depends on VISA, which is the whole reason the socket transport
// exists: an LXI instrument is reachable on a machine with no vendor software
// at all.
//
TEST( OpenTransportVisa, LanIsSupportedWhetherOrNotVisaIsInstalled)
{
    EXPECT_TRUE( hal::io::isSupported( hal::Lan{ "bench-dmm1" } ));
}

//
// A serial device path stays unsupported even with VISA present, because the
// refusal is about the address rather than about the library.
//
TEST( OpenTransportVisa, ADevicePathStaysUnsupportedEvenWithVisaInstalled)
{
    EXPECT_FALSE( hal::io::isSupported( hal::Serial{ "/dev/ttyUSB0" } ));
}

TEST( OpenTransportVisa, WithoutVisaTheRefusalNamesWhatToInstall)
{
    if( haveVisa())
    {
        GTEST_SKIP() << "this machine has VISA -- the no-library path cannot be reached here";
    }

    try
    {
        static_cast<void>( hal::io::openTransport( hal::Usb{ "MY60012345" } ));

        FAIL() << "USB is not openable without a VISA library";
    }
    catch( const hal::io::UnsupportedTransport & unsupported)
    {
        const std::string what{ unsupported.what() };

        // Names the address the rig wrote down...
        EXPECT_NE( what.find( "MY60012345"), std::string::npos) << what;

        // ...and what would fix it, on either of the two target platforms.
        EXPECT_NE( what.find( "IO Libraries"), std::string::npos) << what;
        EXPECT_NE( what.find( "NI-VISA"),      std::string::npos) << what;

        // ...including the override, for an install the loader cannot find.
        EXPECT_NE( what.find( "THORIUM_VISA_LIBRARY"), std::string::npos) << what;
    }
}

//
// And the Simulated address is refused whether or not VISA is installed,
// because it is not a bus at all -- the one refusal in this layer that is not
// a gap.
//
TEST( OpenTransportVisa, ASimulatedAddressIsRefusedByTheVisaPathToo)
{
    try
    {
        static_cast<void>( hal::io::openVisa( hal::Simulated{} ));

        FAIL() << "a Simulated address has nothing to connect to";
    }
    catch( const hal::io::UnsupportedTransport & unsupported)
    {
        const std::string what{ unsupported.what() };

        //
        // Either message is correct here and which one appears depends on the
        // machine: without a library the load failure is reported first, and
        // with one the address is. Both say the thing that matters, so the
        // assertion is on the disjunction rather than on the machine.
        //
        EXPECT_TRUE( what.find( "simulation hooks") != std::string::npos
                  || what.find( "nothing at the other end") != std::string::npos
                  || what.find( "no VISA library") != std::string::npos) << what;
    }
}
