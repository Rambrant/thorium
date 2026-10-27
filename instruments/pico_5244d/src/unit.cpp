#include "hal/pico_5244d.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <variant>

//
// Opening the unit and the identity check -- the part of a PicoScope's
// connection both faces share. See hal/pico_5244d/unit.hpp.
//
namespace hal::pico_5244d
{
    namespace
    {
        constexpr std::string_view kModel = "5244D";

        [[nodiscard]]
        auto trimmed( std::string text) -> std::string
        {
            while( !text.empty() && ( text.back() == '\0' || text.back() == ' ' || text.back() == '\r' || text.back() == '\n'))
            {
                text.pop_back();
            }

            return text;
        }

        [[nodiscard]]
        auto nextGeneration() -> std::uint64_t
        {
            static std::atomic<std::uint64_t> counter{ 0 };

            return ++counter;
        }

        [[nodiscard]]
        auto hardwareOf( BoxConnection & connection) -> Hardware &
        {
            return connection.native<Hardware>( [] { return std::make_shared<Hardware>(); });
        }
    } // namespace

    Unit::~Unit()
    {
        if( Api && Handle > 0)
        {
            // A destructor that throws abandons the process.
            static_cast<void>( Api->stop( Handle));
            static_cast<void>( Api->closeUnit( Handle));
        }
    }

    namespace detail
    {
        void check( const char * call, const Status status)
        {
            if( status != kPicoOk)
            {
                throw PicoFault( call, status);
            }
        }

        auto installApi( BoxConnection & connection, std::shared_ptr<Ps5000a> api) -> void
        {
            auto hardware = std::make_shared<Hardware>();

            hardware->Api = std::move( api);

            connection.installNative( std::move( hardware));
        }

        auto closeUnit( BoxConnection & connection) -> void
        {
            if( auto * const hardware = connection.openNative<Hardware>())
            {
                hardware->Open.reset();
            }
        }

        auto heldUnit( BoxConnection & connection) -> Unit *
        {
            auto * const hardware = connection.openNative<Hardware>();

            return hardware ? hardware->Open.get() : nullptr;
        }

        auto openedUnit( BoxConnection & connection, const std::optional<Resolution> resolution) -> Unit &
        {
            auto & hardware = hardwareOf( connection);

            if( hardware.Open)
            {
                return *hardware.Open;
            }

            if( !hardware.Api)
            {
                hardware.Api = loadPs5000a();
            }

            auto &     api    = *hardware.Api;
            auto       opened = std::make_shared<Unit>();
            const auto & address = connection.address();

            opened->Api = hardware.Api;

            const std::string serial = std::holds_alternative<Usb>( address)
                                     ? std::string( std::get<Usb>( address).serialNumber)
                                     : std::string{};

            opened->Bits = resolution.value_or( Resolution::Bits8);

            Status status = api.openUnit( opened->Handle, serial, static_cast<std::int32_t>( opened->Bits));

            //
            // A unit on USB power alone, or on a USB 2.0 port, answers a status
            // rather than opening. Both are "carry on with what you have", which
            // is what ChangePowerSource says and which a two-channel unit can do
            // (a four-channel one could not, and is not this driver).
            //
            if( status == kPicoPowerSupplyNotConnected || status == kPicoUsb3DeviceOnNonUsb3Port)
            {
                status = api.changePowerSource( opened->Handle, status);
            }

            if( status != kPicoOk)
            {
                std::string attached;

                static_cast<void>( api.enumerateUnits( attached));

                opened->Handle = 0;

                throw PicoFault( "OpenUnit", status,
                    "asked for " + ( serial.empty() ? std::string( "the first unit") : "serial \"" + serial + "\"")
                    + "; units found: " + ( attached.empty() ? std::string( "none") : attached));
            }

            check( "GetUnitInfo(VARIANT_INFO)", api.unitInfo( opened->Handle, kInfoVariant, opened->Variant));
            check( "GetUnitInfo(BATCH_AND_SERIAL)", api.unitInfo( opened->Handle, kInfoBatchAndSerial, opened->Serial));

            opened->Variant = trimmed( opened->Variant);
            opened->Serial  = trimmed( opened->Serial);

            //
            // The model, refused if it is not this one: a serial copied from the
            // wrong row, or a bench re-cabled, gives some other PicoScope answering
            // -- and the ranges, memory, channel count and generator this driver
            // states as facts belong to the 5244D alone. The 5244D MSO shares the
            // analog front end and is refused anyway: its digital inputs change what
            // "channel" means to the SDK, and a driver should not guess. (The
            // Unit's destructor closes the handle on the way out.)
            //
            if( opened->Variant != kModel)
            {
                throw io::TransportError(
                    "expected a PicoScope " + std::string( kModel) + " and found \"" + opened->Variant + "\" (serial "
                    + serial + ") -- check this instrument's address in the rig's instrument table");
            }

            // The driver version is informational; its absence is not a refusal.
            static_cast<void>( api.unitInfo( opened->Handle, kInfoDriverVersion, opened->Driver));
            opened->Driver = trimmed( opened->Driver);

            check( "MaximumValue", api.maximumValue( opened->Handle, opened->AdcMax));

            opened->Generation = nextGeneration();
            hardware.Open      = std::move( opened);

            return *hardware.Open;
        }

        auto identityOf( const Unit & unit) -> std::string
        {
            auto driver = unit.Driver;

            // Commas separate the fields of an identity; the SDK's version string
            // has some of its own ("1, 0, 3, 5").
            std::erase( driver, ' ');
            std::ranges::replace( driver, ',', '.');

            return "Pico Technology," + unit.Variant + "," + unit.Serial + "," + driver;
        }
    } // namespace detail
} // namespace hal::pico_5244d
