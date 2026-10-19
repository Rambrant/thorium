#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

#include "hal/keysight_34980a_switch_unit.hpp"

//
// The second face of the mainframe: the internal DMM. It shares the switching
// face's box and session, so it needs that face complete; the switching face
// knows nothing of this one. See hal/keysight_34980a.hpp, which includes both.
//

namespace hal::keysight_34980a
{
    //
    // The 34980A's internal DMM -- the mainframe's *other* face.
    //
    // A 34980A is a switch and a meter in one box, and this is the meter: a
    // 6.5-digit DMM fitted inside the mainframe, reached by the PC through the
    // same address and the same session as the switching (see
    // hal::BoxConnection), and reached by a signal through the mainframe's four
    // Analog Buses. A rig names it as an instrument on the chassis's box:
    //
    //     INSTRUMENT( Swu1, keysight_34980a::SwitchUnit,     Swu1, Usb( "MY53154781"))
    //     INSTRUMENT( Swu1, keysight_34980a::Multimeter, Dmm2, Usb( "MY53154781"))
    //
    // -- What it measures: the Analog Bus ----------------------------------
    //
    // Every reading is a MEASure query with no channel list, which measures
    // whatever is on the Analog Buses -- ABus1 for the input, ABus2 for a
    // 4-wire reading's sense pair. Not "a channel", because on this rack there
    // is no channel to name: a 34932A matrix's crosspoints are not scan
    // channels, so a signal reaches the meter by a script (and later the
    // fabric) closing a crosspoint onto a Matrix 2 row and that row's bus
    // relay -- 921 for ABus1 -- and then measuring the bus. The channel-list
    // form (MEAS:VOLT:DC? (@1001)) belongs to a multiplexer module, where the
    // mainframe closes the channel for you, and is not modelled here.
    //
    // UNCONFIRMED ON HARDWARE until the first run on the dev desk: that this
    // mainframe's firmware accepts the bare MEASure form. If it does not, the
    // instrument refuses the query and sends no reply -- and read() turns that
    // into an hal::io::ScpiFault naming the command and the instrument's own
    // error, rather than into a bare timeout. So a wrong assumption here costs
    // one clear failure, not a confusing one.
    //
    // -- What it does not measure ------------------------------------------
    //
    // Current. The internal DMM's current input is reached through a 34921A
    // multiplexer's dedicated current channels (041-044), not over the Analog
    // Buses, and this rack has none. Capacitance is not an internal DMM
    // function at all, and temperature wants a transducer and a reference
    // junction this driver has no model of.
    //
    // -- Preparing a shared session ----------------------------------------
    //
    // This face prepares the chassis's session for itself, as its own family
    // (see hal::BoxConnection): the error queue drained, the box checked to be
    // a 34980A, and then the two facts a meter on this box depends on --
    // INST:DMM:INST? (the assembly is fitted) and INST:DMM? (it is enabled,
    // which is to say connected to the Analog Buses). A box with no meter, or
    // with its meter disabled, fails at the first reading -- or at startup,
    // since preflight asks every row's identity.
    //
    // It never enables the meter itself. INST:DMM ON makes the mainframe issue
    // a factory reset (see SwitchUnit::setInternalDmm), and a meter face that did
    // that on its first reading would wipe the configuration of the switching
    // face it shares the box with.
    //
    class Multimeter : public InstrumentTag
    {
        public:
            //
            // The mainframe's own back panel: the meter is inside it, so it is
            // reached over whatever the chassis is.
            //
            using Buses = SwitchUnit::Buses;

            //
            // The functions this face drives, and the MEASure query for each.
            //
            enum class Function
            {
                DcVoltage,
                AcVoltage,
                Resistance,
                FourWireResistance,
                Frequency
            };

            enum class Mode           { Dc, Ac };
            enum class ResistanceMode { TwoWire, FourWire };

            template<typename AddressT>
                requires Buses::allows<AddressT>
            Multimeter( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mConnection.address();
            }

            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return mConnection.isSimulated();
            }

            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mConnection.useTransport( std::move( transport));
            }

            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
            }

            //
            // The box's session, prepared for this face -- see this class's
            // comment on what preparing checks.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &;

            //
            // *IDN? of the mainframe -- which is what this face is inside.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            auto closeSession() -> void
            {
                mConnection.close();
            }

            // --- The ports ---

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, Multimeter>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Voltage, Multimeter>{ *this };
            }

            //
            // True RMS, AC coupled, as the meter's VOLT:AC function is.
            //
            [[nodiscard]]
            auto acVoltage() -> core::Port<core::quantities::Voltage, Multimeter>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Voltage, Multimeter>{ *this };
            }

            //
            // 2-wire ohms on ABus1 -- the bus wiring and every relay in the
            // path are in the reading.
            //
            [[nodiscard]]
            auto resistance() -> core::Port<core::quantities::Resistance, Multimeter>
            {
                mResistanceMode = ResistanceMode::TwoWire;
                return core::Port<core::quantities::Resistance, Multimeter>{ *this };
            }

            //
            // 4-wire ohms: source on ABus1, sense on ABus2, so the path's own
            // resistance is sensed out -- which is why it requires a sense
            // path, as hal::keysight_edu34450a::Multimeter's does.
            //
            [[nodiscard]]
            auto fourWireResistance() -> core::Port<core::quantities::Resistance, Multimeter, core::SensePath::Required>
            {
                mResistanceMode = ResistanceMode::FourWire;
                return core::Port<core::quantities::Resistance, Multimeter>{ *this }.requiresSensePath();
            }

            //
            // The frequency counter -- a reading, not a setting (see
            // hal::keysight_edu34450a::Multimeter::frequency() on the two).
            //
            [[nodiscard]]
            auto frequency() -> core::Port<core::quantities::Frequency, Multimeter>
            {
                return core::Port<core::quantities::Frequency, Multimeter>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto resistanceMode() const -> ResistanceMode
            {
                return mResistanceMode;
            }

            //
            // The read a core::Port performs. A range in the setup goes down
            // the wire as the query's first argument, in the port's own unit;
            // none means autorange. An overload is a core::UnmeasurableReading
            // naming the function and the range, which a script turns into a
            // value with whenUnmeasurable() -- see the open-bus check in
            // dev/suite/scripts/swu_dmm.cpp, where an overload is the answer.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & setup) -> QuantityT
            {
                if( isSimulated())
                {
                    return simulatedReading<QuantityT>();
                }

                const std::optional<double> range =
                    setup.Range ? std::optional<double>{ setup.Range->value() } : std::nullopt;

                return QuantityT{ read( functionFor<QuantityT>(), range) };
            }

            //
            // Nothing: a meter sources nothing, and its relays are the
            // chassis's (see SwitchUnit::safeRelays()).
            //
            auto safe() -> void
            {
            }

            // --- Test/simulation hooks ---

            auto setSimulatedVoltage( const core::quantities::Voltage v) -> void          { mSimVoltage = v; }
            auto setSimulatedAcVoltage( const core::quantities::Voltage v) -> void        { mSimAcVoltage = v; }
            auto setSimulatedFrequency( const core::quantities::Frequency f) -> void      { mSimFrequency = f; }

            //
            // A resistance on the bus, or none -- an open bus, which is what a
            // meter with nothing closed onto it sees, and which reads as an
            // overload rather than as any number.
            //
            auto setSimulatedResistance( const std::optional<core::quantities::Resistance> r) -> void
            {
                mSimResistance = r;
            }

        private:
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto functionFor() const -> Function
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? Function::AcVoltage : Function::DcVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    return mResistanceMode == ResistanceMode::FourWire ? Function::FourWireResistance : Function::Resistance;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return Function::Frequency;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "the 34980A's internal DMM has no port for this quantity");
                }
            }

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto simulatedReading() const -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? mSimAcVoltage : mSimVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    if( !mSimResistance)
                    {
                        throw core::UnmeasurableReading( "resistance overload -- nothing on the Analog Bus");
                    }

                    return *mSimResistance;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return mSimFrequency;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "the 34980A's internal DMM has no port for this quantity");
                }
            }

            //
            // One MEASure query on the Analog Bus, checked -- see the .cpp.
            //
            auto read( Function function, std::optional<double> range) -> double;

            //
            // The once-per-session exchange for this face: see this class's
            // comment.
            //
            static auto prepare( io::ScpiSession & session) -> void;

            InstrumentId    mId;

            //
            // The chassis's box and session -- shared with
            // hal::keysight_34980a::SwitchUnit on the same address, as a second
            // family ("keysight_34980a.dmm") that prepares for itself.
            //
            BoxConnection   mConnection;

            Mode            mMode{ Mode::Dc };
            ResistanceMode  mResistanceMode{ ResistanceMode::TwoWire };

            core::quantities::Voltage                    mSimVoltage{};
            core::quantities::Voltage                    mSimAcVoltage{};
            core::quantities::Frequency                  mSimFrequency{};
            std::optional<core::quantities::Resistance>  mSimResistance{};
    };

} // namespace hal::keysight_34980a
