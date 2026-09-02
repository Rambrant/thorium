//
// hal::keysight_dsox1202g::DSOX1202G's real I/O: what this scope is actually
// told, and what its answers mean.
//
// The second .cpp in instruments/, after the EDU34450A's, and it exists for the
// same reason: none of what is in here depends on which C++ quantity type a
// reading will be wrapped in. Which :MEASure command to send, how a single-shot
// capture is armed and polled, and how a waveform record comes back are all
// ordinary code over this driver's own enums, so all of it can live in one file
// in the order the instrument sees it -- which is what lets a bench engineer
// with the programmer's guide open check it against the manual without reading
// a single template.
//
// -- The document this is written against ---------------------------------
//
// Keysight InfiniiVision 1000 X-Series Oscilloscopes Programmer's Guide,
// version 01.01.0000 (February 2017):
//
//     http://sophphx.caltech.edu/Lab_Equipment/Keysight_1000_X-Series_prog_guide.pdf
//
// Every command below, the arm-and-poll sequence, the +9.9E+37 sentinel and the
// preamble's field order are from that document. The rule this driver works
// under is the one the Infiniium driver states: check the manual, not another
// program's source -- and in particular not the 2000/3000 X-Series manual,
// whose command set is a superset of this one.
//
#include "hal/keysight_dsox1202g.hpp"

#include <charconv>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "hal/io/transport.hpp"

namespace hal::keysight_dsox1202g
{
    namespace
    {
        //
        // "CHANnel1", "CHANnel2" -- the source parameter every :MEASure query
        // and the :WAVeform subsystem take.
        //
        // A function rather than a table, because unlike the EDU34450A's
        // function table there is nothing per-channel to look up: the channel
        // number is the whole of the difference, and it has already been
        // checked against ValidChannel wherever a script wrote it.
        //
        [[nodiscard]]
        auto sourceOf( const unsigned channel) -> std::string
        {
            return "CHANnel" + std::to_string( channel);
        }

        //
        // One row per measurement: the :MEASure mnemonic it is queried with,
        // and whether it is one of the two that measure an edge and therefore
        // care about thresholds.
        //
        // The mnemonics are deliberately the long forms. A SCPI parser accepts
        // either, and the long form is what the manual's own index is written
        // in -- so a reader checking this file against the guide is looking up
        // the same word, and the short form saves bytes on a link that has
        // milliseconds to spare.
        //
        struct MeasurementCommand
        {
            std::string_view Mnemonic;

            //
            // Whether :MEASure:DEFine THResholds applies. Rise and fall time
            // are measured between thresholds, so a script that asked for
            // 20%/80% has asked for a different number rather than for the same
            // number differently -- see readMeasurement(). Everything else on
            // this list ignores them, and sending thresholds before, say, a
            // VBASe would be a command with no effect on the answer.
            //
            bool             UsesThresholds;

            //
            // What to call it in a message a human reads -- the front panel's
            // words, not the mnemonic.
            //
            std::string_view Name;
        };

        [[nodiscard]]
        constexpr auto commandFor( const DSOX1202G::Mode mode) -> MeasurementCommand
        {
            switch( mode)
            {
                case DSOX1202G::Mode::Vpp:           return { "VPP",        false, "peak-to-peak voltage" };
                case DSOX1202G::Mode::Vmax:          return { "VMAX",       false, "maximum voltage"      };
                case DSOX1202G::Mode::Vmin:          return { "VMIN",       false, "minimum voltage"      };
                case DSOX1202G::Mode::Vrms:          return { "VRMS",       false, "RMS voltage"          };
                case DSOX1202G::Mode::Vaverage:      return { "VAVerage",   false, "average voltage"      };
                case DSOX1202G::Mode::Vbase:         return { "VBASe",      false, "base voltage"         };
                case DSOX1202G::Mode::Vtop:          return { "VTOP",       false, "top voltage"          };
                case DSOX1202G::Mode::Vamplitude:    return { "VAMPlitude", false, "amplitude"            };
                case DSOX1202G::Mode::Frequency:     return { "FREQuency",  false, "frequency"            };
                case DSOX1202G::Mode::Period:        return { "PERiod",     false, "period"               };
                case DSOX1202G::Mode::RiseTime:      return { "RISetime",   true,  "rise time"            };
                case DSOX1202G::Mode::FallTime:      return { "FALLtime",   true,  "fall time"            };
                case DSOX1202G::Mode::PositiveWidth: return { "PWIDth",     false, "positive pulse width" };
                case DSOX1202G::Mode::NegativeWidth: return { "NWIDth",     false, "negative pulse width" };
            }

            //
            // Unreachable for any declared enumerator, and present so that
            // adding one to Mode without adding a row here is a warning this
            // build turns into an error (-Wswitch under -Werror) rather than a
            // function that falls off its end.
            //
            return { "", false, "unknown measurement" };
        }

        //
        // The enums, as the instrument spells them.
        //
        // Written out rather than reflected from the enumerator names (see
        // core/meta.hpp, which is how the *log* renders these), because the two
        // are not the same string and must not be made to be. "HighResolution"
        // is what a script wrote and what a report should say; "HRESolution" is
        // what the instrument accepts. Tying them together would mean either an
        // enum named after a SCPI mnemonic or a log full of them.
        //
        [[nodiscard]]
        constexpr auto scpiFor( const TriggerSlope value) -> std::string_view
        {
            switch( value)
            {
                case TriggerSlope::Rising:      return "POSitive";
                case TriggerSlope::Falling:     return "NEGative";
                case TriggerSlope::Either:      return "EITHer";
                case TriggerSlope::Alternating: return "ALTernate";
            }

            return "POSitive";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const TriggerSweep value) -> std::string_view
        {
            switch( value)
            {
                case TriggerSweep::Auto:   return "AUTO";
                case TriggerSweep::Normal: return "NORMal";
            }

            return "AUTO";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const TriggerCoupling value) -> std::string_view
        {
            switch( value)
            {
                case TriggerCoupling::Dc:                 return "DC";
                case TriggerCoupling::Ac:                 return "AC";
                case TriggerCoupling::LowFrequencyReject: return "LFReject";
            }

            return "DC";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const TriggerReject value) -> std::string_view
        {
            switch( value)
            {
                case TriggerReject::Off:           return "OFF";
                case TriggerReject::LowFrequency:  return "LFReject";
                case TriggerReject::HighFrequency: return "HFReject";
            }

            return "OFF";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const TimebaseReference value) -> std::string_view
        {
            switch( value)
            {
                case TimebaseReference::Left:   return "LEFT";
                case TimebaseReference::Center: return "CENTer";
                case TimebaseReference::Right:  return "RIGHt";
            }

            return "CENTer";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const AcquisitionType value) -> std::string_view
        {
            switch( value)
            {
                case AcquisitionType::Normal:         return "NORMal";
                case AcquisitionType::Averaged:       return "AVERage";
                case AcquisitionType::HighResolution: return "HRESolution";
                case AcquisitionType::PeakDetect:     return "PEAK";
            }

            return "NORMal";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const Coupling value) -> std::string_view
        {
            switch( value)
            {
                case Coupling::Dc: return "DC";
                case Coupling::Ac: return "AC";
            }

            return "DC";
        }

        //
        // ON/OFF for the two settings this driver models as two-valued enums
        // rather than as bools -- see Bandwidth and ChannelDisplay on why the
        // call site says which state it means and the wire does not care.
        //
        [[nodiscard]]
        constexpr auto scpiFor( const Bandwidth value) -> std::string_view
        {
            return value == Bandwidth::Limited ? "ON" : "OFF";
        }

        [[nodiscard]]
        constexpr auto scpiFor( const ChannelDisplay value) -> std::string_view
        {
            return value == ChannelDisplay::On ? "ON" : "OFF";
        }

        //
        // The model field out of an *IDN? reply -- the second of four
        // comma-separated fields.
        //
        // Whitespace-trimmed, because this instrument answers "DSO-X 1202G"
        // with a space in it and a leading one is not impossible; and compared
        // with the punctuation removed by the caller rather than here, since
        // what counts as the same model is that caller's business (see
        // verifyIdentity).
        //
        [[nodiscard]]
        auto modelOf( const std::string & identity) -> std::string
        {
            const auto first = identity.find( ',');

            if( first == std::string::npos)
            {
                return {};
            }

            const auto second = identity.find( ',', first + 1);
            auto       model  = identity.substr( first + 1,
                                    second == std::string::npos ? std::string::npos : second - first - 1);

            while( !model.empty() && ( model.front() == ' ' || model.front() == '\t'))
            {
                model.erase( model.begin());
            }

            while( !model.empty() && ( model.back() == ' ' || model.back() == '\t'))
            {
                model.pop_back();
            }

            return model;
        }

        //
        // "DSO-X 1202G" and "DSOX1202G" are the same instrument.
        //
        // Keysight's own front panel, data sheet and ordering system disagree
        // about the hyphen and the space, and *IDN? answers with the spelling
        // the firmware happens to carry. Comparing on the letters and digits
        // alone is what stops this driver from refusing the very instrument it
        // is for over a piece of punctuation -- and it is a narrower rule than
        // it looks, since nothing else in the family reduces to the same token.
        //
        [[nodiscard]]
        auto modelToken( const std::string & model) -> std::string
        {
            std::string token;

            for( const char character : model)
            {
                if( character != '-' && character != ' ' && character != '_')
                {
                    token += static_cast<char>( std::toupper( static_cast<unsigned char>( character)));
                }
            }

            return token;
        }

        //
        // How long to wait between polls of :AER? and :OPERegister:CONDition?.
        //
        // Keysight's own single-shot example sleeps 100 ms between queries,
        // with the comment "small wait to prevent excessive queries", and this
        // is that number. It bounds how late a capture can be noticed rather
        // than how long it takes: a script that armed, dropped a rail and is
        // waiting for 40 ms of transient learns about it within a tenth of a
        // second either way, and the alternative -- a tight loop -- spends the
        // instrument's parser on the question instead of on the acquisition.
        //
        constexpr auto kPollInterval = std::chrono::milliseconds{ 100 };

        //
        // Bit 3 of the Operation Status Condition Register: RUN, set while the
        // scope is acquiring and clear once it has stopped. After a :SINGle,
        // clear means the capture is over.
        //
        constexpr int kRunBit = 0x08;

        //
        // The samples out of an IEEE 488.2 definite-length block carrying
        // ASCii-formatted waveform data: "#800001234+5.02E+00,+5.01E+00,...".
        //
        // The header is '#', then one digit saying how many digits the length
        // is, then that many digits of length, then the payload. Read that way
        // rather than by skipping a fixed ten characters, because the digit
        // count is a property of the reply and not of this instrument -- an
        // assumption about it would be wrong silently, by one sample, on the
        // first instrument that sent a shorter one.
        //
        // The stated length is used to bound the payload rather than trusted
        // blindly to equal what arrived: a reply cut short by a transport that
        // read one line where the instrument sent two is a real failure mode,
        // and it should be an exception naming the query rather than a trace
        // that is quietly missing its tail.
        //
        [[nodiscard]]
        auto samplesFromBlock( const std::string & block, const std::string & instrument) -> std::vector<double>
        {
            const auto fault = [&]( const std::string & why)
            {
                return io::ScpiFault( instrument, ":WAVeform:DATA?", io::ScpiError{ 0, why });
            };

            if( block.size() < 2 || block.front() != '#')
            {
                throw fault( "expected a definite-length block beginning with '#'");
            }

            const auto digits = static_cast<std::size_t>( block[ 1] - '0');

            if( digits < 1 || digits > 9 || block.size() < 2 + digits)
            {
                throw fault( "the block header does not say how long the block is" );
            }

            std::size_t length = 0;

            for( std::size_t index = 0; index < digits; ++index)
            {
                const char digit = block[ 2 + index];

                if( digit < '0' || digit > '9')
                {
                    throw fault( "the block header's length is not a number");
                }

                length = length * 10 + static_cast<std::size_t>( digit - '0');
            }

            const auto payload = std::string_view{ block}.substr( 2 + digits);

            if( payload.size() < length)
            {
                throw fault( "the block says it carries " + std::to_string( length)
                             + " bytes and " + std::to_string( payload.size()) + " arrived");
            }

            //
            // Comma separated, and parsed with std::from_chars rather than
            // std::stod: this runs once per sample, and from_chars neither
            // allocates nor consults the locale -- the second of which is not a
            // micro-optimisation but a correctness point, since a comma decimal
            // separator would turn "+5.02" into 5 on a machine set to a
            // European locale.
            //
            std::vector<double> values;

            const auto data = payload.substr( 0, length);

            for( std::size_t start = 0; start < data.size(); )
            {
                const auto comma = data.find( ',', start);
                const auto field = data.substr( start, comma == std::string_view::npos ? std::string_view::npos
                                                                                       : comma - start);

                auto   first = field.data();
                auto   last  = field.data() + field.size();
                double value = 0.0;

                while( first != last && ( *first == ' ' || *first == '\t'))
                {
                    ++first;
                }

                //
                // And the sign, which std::from_chars will not take. It accepts
                // a leading '-' and rejects a leading '+' -- a deliberate
                // narrowing in the standard, and squarely in the way here,
                // because a SCPI instrument writes its positive numbers with
                // the plus on: "+5.00E+00" is what this scope sends for every
                // sample above ground. Skipping it is the whole accommodation;
                // what follows is the same unsigned form from_chars wants.
                //
                if( first != last && *first == '+')
                {
                    ++first;
                }

                if( const auto parsed = std::from_chars( first, last, value); parsed.ec != std::errc{})
                {
                    throw fault( "\"" + std::string( field) + "\" is not a sample");
                }

                values.push_back( value);

                if( comma == std::string_view::npos)
                {
                    break;
                }

                start = comma + 1;
            }

            if( values.empty())
            {
                throw fault( "the block carries no samples");
            }

            return values;
        }
    } // namespace

    auto DSOX1202G::session() -> io::ScpiSession &
    {
        if( !mSession)
        {
            //
            // Opened from the address the rig table wrote down. A Simulated
            // address reaching here is a bug in this driver rather than in the
            // table -- every caller checks isSimulated() first -- and
            // hal::io::openTransport says exactly that in the exception it
            // throws for one.
            //
            // hal::Usb routes to VISA (see hal/io/visa_transport.hpp), so a
            // machine with no VISA installed fails here with "no VISA library
            // found" rather than with a timeout, which is the difference
            // between a missing dependency and a missing instrument.
            //
            mSession = std::make_unique<io::ScpiSession>( io::openTransport( mAddress));
        }

        if( mPrepared)
        {
            return *mSession;
        }

        //
        // The once-per-session exchange, here rather than beside the
        // construction above because a session arrives two ways -- opened from
        // the address, or handed in by useTransport() -- and preparing it where
        // it is *used* is what makes both go through this.
        //
        // Whatever the last user of this scope left in its error queue is not
        // this run's, and would otherwise be reported against this run's first
        // command. Drained before the identity query rather than after, so a
        // stale entry cannot be mistaken for the identity query having failed.
        //
        mSession->clearErrors();

        static_cast<void>( verifyIdentity());

        mPrepared = true;

        return *mSession;
    }

    auto DSOX1202G::verifyIdentity() -> std::string
    {
        const std::string identity = mSession->identify();
        const auto        token    = modelToken( modelOf( identity));

        //
        // Both 1202s, and this driver is honestly for both. The DSOX1202A and
        // DSOX1202G share one command set and one channel count; the G adds a
        // waveform generator, which this driver does not model at all (see the
        // class comment). A rig that has the A is not making a mistake.
        //
        // What is refused is everything else, and the case worth catching is
        // not exotic: a USB serial number copied from the wrong row, or a VISA
        // alias re-pointed after a re-cable, gives a meter or a supply
        // answering where the scope should be -- and every reading after that
        // is wrong in a way no criterion can see. Note the four-channel
        // DSOX1204A/G is refused too, and deliberately: it is a different
        // command set version with four channels, and this driver's
        // ValidChannel would silently bound a script to half of it.
        //
        if( token != "DSOX1202G" && token != "DSOX1202A")
        {
            throw io::ScpiFault( mSession->description(), "*IDN?",
                io::ScpiError{ 0,
                    "expected a DSOX1202G or a DSOX1202A and found \"" + identity
                    + "\" -- check this instrument's address in the rig's instrument table" });
        }

        return identity;
    }

    auto DSOX1202G::identity() -> std::string
    {
        //
        // session() has already asked and already checked the answer, so this
        // asks again rather than caching it -- one round trip on a call nothing
        // takes per reading, against a cached string that would be a second
        // thing to keep true if the session were ever reopened onto a different
        // box.
        //
        return session().identify();
    }

    auto DSOX1202G::safe() -> void
    {
        //
        // The armed flag is cleared for the reason the header gives: it is a
        // pending expectation rather than a setting, and a scope left armed
        // after a script died would answer the next script's Await.
        //
        mArmed = false;

        //
        // And on an attached scope the flag alone would be a lie, because the
        // instrument is still armed and will happily trigger on whatever the
        // next script does to the DUT. So :STOP -- but only down a session that
        // is *already open*.
        //
        // Never opening one here is the whole of the rule, and it is not
        // frugality. Safing runs when a script has already failed, quite
        // possibly because this instrument is unreachable; opening a session at
        // that moment would replace the run's real failure with a transport
        // error from the cleanup path. For the same reason the send is
        // swallowed rather than propagated: hal::safeRig() does not catch (see
        // hal/src/verbs/safing.cpp), so an exception from here would abandon
        // the safing of every instrument after this one -- and this scope
        // sources nothing, so it is the least important thing in that sequence.
        //
        // The session is deliberately *not* closed: its error queue is the best
        // evidence of what went wrong, and dropping it is closeSession()'s job
        // and the caller's call. Same argument
        // hal::keysight_edu34450a::EDU34450A::safe() makes for doing nothing at
        // all.
        //
        if( !mSession)
        {
            return;
        }

        try
        {
            mSession->write( ":STOP");
        }
        catch( const io::TransportError &)
        {
        }
    }

    auto DSOX1202G::configureTrigger( const TriggerConfig & config) -> void
    {
        if( config.EdgeSource) { mTriggerSource   = config.EdgeSource; }
        if( config.Slope)      { mTriggerSlope    = config.Slope;      }
        if( config.Level)      { mTriggerLevel    = config.Level;      }
        if( config.Sweep)      { mTriggerSweep    = config.Sweep;      }
        if( config.Coupling)   { mTriggerCoupling = config.Coupling;   }
        if( config.Reject)     { mTriggerReject   = config.Reject;     }
        if( config.Holdoff)    { mTriggerHoldoff  = config.Holdoff;    }

        if( isSimulated())
        {
            return;
        }

        auto & scpi = session();

        //
        // :TRIGger:MODE EDGE first, and unconditionally, whenever a trigger
        // Setup names anything at all.
        //
        // Because the settings below belong to the edge trigger and are simply
        // stored while some other kind is selected. A scope left in GLITch mode
        // by a previous script -- or by an operator at the front panel, which
        // is the likelier one -- accepts every command in this function without
        // complaint and then triggers on a pulse width. This is one command
        // against an entire class of "it worked yesterday".
        //
        scpi.checked( ":TRIGger:MODE EDGE");

        if( config.EdgeSource)
        {
            scpi.checked( ":TRIGger:EDGE:SOURce " + sourceOf( *config.EdgeSource));
        }

        if( config.Slope)
        {
            scpi.checked( ":TRIGger:EDGE:SLOPe " + std::string( scpiFor( *config.Slope)));
        }

        //
        // After the source, always. The trigger level on this instrument is
        // held per source, so a level sent while the previous source was
        // selected sets the level on *that* channel and leaves this one at
        // whatever it was. The config carries both together for this reason
        // (see TriggerConfig), and this is the ordering that makes carrying
        // them together mean something.
        //
        if( config.Level)
        {
            scpi.checked( ":TRIGger:EDGE:LEVel " + io::ScpiSession::number( config.Level->value()));
        }

        if( config.Sweep)
        {
            scpi.checked( ":TRIGger:SWEep " + std::string( scpiFor( *config.Sweep)));
        }

        if( config.Coupling)
        {
            scpi.checked( ":TRIGger:EDGE:COUPling " + std::string( scpiFor( *config.Coupling)));
        }

        if( config.Reject)
        {
            scpi.checked( ":TRIGger:EDGE:REJect " + std::string( scpiFor( *config.Reject)));
        }

        if( config.Holdoff)
        {
            scpi.checked( ":TRIGger:HOLDoff " + io::ScpiSession::number( config.Holdoff->value()));
        }
    }

    auto DSOX1202G::configureTimebase( const TimebaseConfig & config) -> void
    {
        if( config.TimePerDivision) { mTimePerDivision   = config.TimePerDivision; }
        if( config.Position)        { mTimebasePosition  = config.Position;        }
        if( config.Reference)       { mTimebaseReference = config.Reference;       }

        if( isSimulated())
        {
            return;
        }

        auto & scpi = session();

        //
        // Scale before position, and position before nothing in particular --
        // but the pair is worth an ordering note. :TIMebase:POSition is a time
        // and the instrument clamps it against the current window, so setting a
        // 50 ms delay while the window is still 1 us/div gets it clamped and
        // then left there when the scale arrives. Widening first is what makes
        // the two commands add up to what the script asked for.
        //
        if( config.TimePerDivision)
        {
            scpi.checked( ":TIMebase:SCALe " + io::ScpiSession::number( config.TimePerDivision->value()));
        }

        if( config.Reference)
        {
            scpi.checked( ":TIMebase:REFerence " + std::string( scpiFor( *config.Reference)));
        }

        if( config.Position)
        {
            scpi.checked( ":TIMebase:POSition " + io::ScpiSession::number( config.Position->value()));
        }
    }

    auto DSOX1202G::configureAcquisition( const AcquisitionConfig & config) -> void
    {
        if( config.Type)         { mAcquisitionType = config.Type;         }
        if( config.AverageCount) { mAverageCount    = config.AverageCount; }

        if( isSimulated())
        {
            return;
        }

        auto & scpi = session();

        if( config.Type)
        {
            scpi.checked( ":ACQuire:TYPE " + std::string( scpiFor( *config.Type)));
        }

        //
        // The count after the type, because it is only meaningful once AVERage
        // is selected -- and the builder is what guarantees the two arrive
        // together (see AcquisitionBuilder::averagedOver, which sets both).
        //
        if( config.AverageCount)
        {
            scpi.checked( ":ACQuire:COUNt " + std::to_string( *config.AverageCount));
        }
    }

    auto DSOX1202G::configureChannel( const ChannelConfig & config) -> void
    {
        auto & data = atChannel( config.Channel);

        if( config.InputCoupling)    { data.InputCoupling    = config.InputCoupling;    }
        if( config.VoltsPerDivision) { data.VoltsPerDivision = config.VoltsPerDivision; }
        if( config.VerticalOffset)   { data.VerticalOffset   = config.VerticalOffset;   }
        if( config.BandwidthLimit)   { data.BandwidthLimit   = config.BandwidthLimit;   }
        if( config.ProbeAttenuation) { data.ProbeAttenuation = config.ProbeAttenuation; }
        if( config.Display)          { data.Display          = config.Display;          }

        if( isSimulated())
        {
            return;
        }

        auto &            scpi    = session();
        const std::string channel = ":CHANnel" + std::to_string( config.Channel);

        //
        // The probe ratio first, and this one is not arbitrary either. This
        // instrument's vertical scale, offset and trigger level are all
        // expressed at the probe tip, so the attenuation factor is what they
        // are scaled by -- and a scale sent before the ratio is a scale for the
        // wrong divider, corrected only when the next Setup happens to send
        // both again. Setting it first makes a single Setup call mean the same
        // thing whether or not the probe was already configured.
        //
        if( config.ProbeAttenuation)
        {
            scpi.checked( channel + ":PROBe " + io::ScpiSession::number( *config.ProbeAttenuation));
        }

        if( config.InputCoupling)
        {
            scpi.checked( channel + ":COUPling " + std::string( scpiFor( *config.InputCoupling)));
        }

        if( config.VoltsPerDivision)
        {
            scpi.checked( channel + ":SCALe " + io::ScpiSession::number( config.VoltsPerDivision->value()));
        }

        if( config.VerticalOffset)
        {
            scpi.checked( channel + ":OFFSet " + io::ScpiSession::number( config.VerticalOffset->value()));
        }

        if( config.BandwidthLimit)
        {
            scpi.checked( channel + ":BWLimit " + std::string( scpiFor( *config.BandwidthLimit)));
        }

        if( config.Display)
        {
            scpi.checked( channel + ":DISPlay " + std::string( scpiFor( *config.Display)));
        }
    }

    auto DSOX1202G::armSingle( const SingleConfig & config) -> void
    {
        mCompleted = false;

        if( isSimulated())
        {
            mArmed = true;

            return;
        }

        //
        // Not armed until the instrument says it is, which is why this is
        // cleared here rather than set. Everything below can throw -- an
        // unreachable scope, a refused :SINGle, an :AER? that never goes to 1
        // -- and a driver left claiming to be armed after one of those would
        // hand the next Await a capture that was never taken.
        //
        mArmed = false;

        auto & scpi = session();

        //
        // Keysight's own single-shot sequence, in its own order, and every step
        // of it earns its place:
        //
        //   :STOP     so that what follows is not racing an acquisition that is
        //             already running.
        //   *OPC?     which blocks until the stop has actually taken effect --
        //             the point of the query is the waiting, not the "1".
        //   :SINGle   which arms.
        //   :AER?     polled until it answers 1, which is the instrument saying
        //             it is armed and ready to trigger.
        //
        // That last poll is the whole reason Arm is a verb of its own. The
        // guide puts the instruction to enable the DUT immediately after this
        // loop, in a comment -- "Oscilloscope is armed and ready, enable DUT
        // here" -- and a script that caused its event any earlier would be
        // capturing nothing, silently and only sometimes (see
        // core/verbs/acquire.hpp).
        //
        // :DIGitize is not used, and the guide says why: it blocks the
        // instrument against every further command until the acquisition
        // completes, so a single-shot DUT that has not been enabled yet could
        // never be enabled.
        //
        scpi.checked( ":STOP");
        scpi.waitForComplete();
        scpi.checked( ":SINGle");

        const auto timeout  = config.ArmTimeout.value_or( kDefaultArmTimeout);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>( timeout.value()));

        for( ;;)
        {
            if( scpi.queryNumber( ":AER?") != 0.0)
            {
                mArmed = true;

                return;
            }

            if( std::chrono::steady_clock::now() >= deadline)
            {
                //
                // Throwing rather than returning, and this is the one place in
                // this driver where that is right. Await answers false for a
                // capture that did not complete, because "no transient" is a
                // result a script must be able to check; a scope that never
                // armed has produced no result at all, and the script is about
                // to cause the event it cannot capture. Letting it proceed
                // would record a reading of the previous acquisition against a
                // criterion, which is the worst outcome available.
                //
                throw io::TransportTimeout(
                    session().description() + " did not report itself armed within "
                    + io::ScpiSession::number( timeout.value()) + " s of :SINGle");
            }

            std::this_thread::sleep_for( kPollInterval);
        }
    }

    auto DSOX1202G::awaitAcquisition( const SingleConfig & config) -> bool
    {
        if( !mArmed)
        {
            //
            // Awaiting something that was never armed answers false rather than
            // throwing -- see the header for why the script bug this catches is
            // one the run itself should report.
            //
            return false;
        }

        mArmed = false;

        if( isSimulated())
        {
            mCompleted = mCaptureCompletes;

            return mCompleted;
        }

        auto & scpi = session();

        //
        // The RUN bit in :OPERegister:CONDition?, polled until it clears. A
        // cleared RUN bit after a :SINGle means the scope has stopped, which
        // means the acquisition finished.
        //
        // Not :TER? (the trigger event register), which would answer sooner and
        // wrongly: it says a trigger happened, and a triggered scope may still
        // be filling the rest of its record. It is also cleared by being read,
        // so a second Await would find nothing there.
        //
        const auto timeout  = config.Timeout.value_or( kDefaultCaptureTimeout);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                  std::chrono::duration<double>( timeout.value()));

        for( ;;)
        {
            const auto condition = static_cast<int>( scpi.queryNumber( ":OPERegister:CONDition?"));

            if( ( condition & kRunBit) == 0)
            {
                mCompleted = true;

                return true;
            }

            if( std::chrono::steady_clock::now() >= deadline)
            {
                //
                // A capture that never triggered is a *result*, not a fault:
                // the event the script went looking for did not happen, or did
                // not cross the trigger level. So this returns false and lets
                // the script's own Verify record it -- exactly as the simulated
                // path does, and exactly as
                // suite/scripts/ac_dropout_script.cpp's FS_Transient_Captured
                // criterion expects.
                //
                // The scope is stopped on the way out. Left running it would
                // still be armed, and the next script's stimulus would trigger
                // this capture -- the same stale-expectation problem safe()
                // exists for, arriving through the door marked "timeout".
                //
                scpi.write( ":STOP");

                mCompleted = false;

                return false;
            }

            std::this_thread::sleep_for( kPollInterval);
        }
    }

    auto DSOX1202G::fetchWaveform( const WaveformConfig & config) -> core::Waveform
    {
        if( isSimulated())
        {
            return atChannel( config.Channel).Trace;
        }

        auto & scpi = session();

        //
        // :WAVeform:FORMat ASCii, which is the decision worth explaining here
        // because the alternative is faster and this one is still right.
        //
        // The instrument offers BYTE, WORD and ASCii. The first two hand back
        // raw integers to be scaled against the preamble -- two bytes a point,
        // and the way anyone transfers deep memory. ASCii hands back the
        // Y-axis values already converted to volts, in floating point, comma
        // separated, at as many as 13 bytes a point.
        //
        // ASCii is used because the binary forms need something hal/io/ does
        // not have. A definite-length block of counted bytes has to be read as
        // *bytes*, and hal::io::ITransport reads a line -- its own header says
        // so, and hal/io/scpi.hpp names binary block transfer as the one real
        // gap in it. An ASCii block has no bytes in it that a line-oriented
        // read would choke on, so it comes back through the existing transport
        // intact.
        //
        // What it costs is transfer time on a big record, and what this rig
        // captures is a screenful -- 1000 points by default, about 13 kB, which
        // is nothing. The day this driver models :WAVeform:POINts:MODE RAW and
        // starts asking for the whole acquisition memory, it wants WORD and
        // hal/io/ wants a counted read. That is the order those two changes go
        // in, and neither is needed yet.
        //
        scpi.checked( ":WAVeform:SOURce " + sourceOf( config.Channel));
        scpi.checked( ":WAVeform:FORMat ASCii");

        //
        // The preamble, for the timebase. Ten comma-separated fields, of which
        // this driver needs two: xincrement (index 4) is the time between
        // samples and xorigin (index 5) is the time of the first one relative
        // to the trigger.
        //
        // The y fields are deliberately unused, and that is not an oversight:
        // in ASCii format the instrument has already applied them, so scaling
        // the values again here would square the vertical scale. In BYTE or
        // WORD they are what turns a raw level into volts, which is the arithmetic
        // this driver would gain along with the counted read.
        //
        const auto preamble = scpi.queryNumbers( ":WAVeform:PREamble?");

        if( preamble.size() < 6)
        {
            throw io::ScpiFault( scpi.description(), ":WAVeform:PREamble?",
                io::ScpiError{ 0, "expected ten preamble fields and got "
                                  + std::to_string( preamble.size()) });
        }

        const auto increment = core::quantities::Time{ preamble[ 4] };
        const auto origin    = core::quantities::Time{ preamble[ 5] };

        //
        // ":WAVeform:DATA?" answers with an IEEE 488.2 definite-length block --
        // "#8<eight digits><values>" -- even in ASCii format, which the guide
        // is explicit about. hal::io::ScpiSession::queryNumbers parses a
        // comma-separated list and would take the "#800001234+5.02E+00" first
        // field as one number, so the header comes off here first.
        //
        // Parsed rather than skipped by a fixed width: the digit count is the
        // character after the '#', and while this instrument always sends 8, a
        // reader who assumed it would be wrong on the first instrument that
        // does not -- and would be wrong silently, by one sample.
        //
        const auto block  = scpi.query( ":WAVeform:DATA?");
        const auto values = samplesFromBlock( block, scpi.description());

        return core::Waveform{
            core::quantityKindOf<core::quantities::Voltage>(),
            core::Waveform::Timing{ origin, increment },
            values };
    }

    auto DSOX1202G::readMeasurement( const Mode mode, const unsigned channel,
                                     const std::optional<double> lowThreshold,
                                     const std::optional<double> highThreshold) -> double
    {
        auto &     scpi    = session();
        const auto command = commandFor( mode);
        const auto source  = sourceOf( channel);

        //
        // Thresholds before the measurement that uses them, and only for the
        // two that do.
        //
        // 10/50/90 is what :MEASure:DEFine THResholds,STANdard means and is
        // what this driver's riseTime()/fallTime() seed, so the ordinary case
        // sends the instrument's own default back to it -- one command, and it
        // makes the reading independent of what the last script left behind,
        // which is the whole point of sending it at all.
        //
        // A script that named its own pair gets PERCent with the middle
        // threshold left at 50: this driver's ports carry an upper and a lower
        // and nothing in between, because the middle one only affects delay and
        // phase measurements, which are not modelled. The instrument accepts
        // 5..95 and answers -222 for anything outside it, so a script asking
        // for 2% gets hal::io::ScpiFault naming the command rather than a
        // silently clamped reading.
        //
        if( command.UsesThresholds)
        {
            const auto lower = lowThreshold.value_or( 0.1) * 100.0;
            const auto upper = highThreshold.value_or( 0.9) * 100.0;

            if( lower == 10.0 && upper == 90.0)
            {
                scpi.checked( ":MEASure:DEFine THResholds,STANdard," + source);
            }
            else
            {
                scpi.checked( ":MEASure:DEFine THResholds,PERCent,"
                              + io::ScpiSession::number( upper) + ",50,"
                              + io::ScpiSession::number( lower) + "," + source);
            }
        }

        //
        // The query form with the source parameter, so which channel is being
        // measured travels with the question rather than being a mode the
        // instrument was left in. ":MEASure:VBASe? CHANnel2" both selects the
        // source and answers about it, which is one round trip instead of two
        // and, more importantly, one fewer piece of instrument state for a
        // later reading to inherit.
        //
        const double reading = scpi.queryNumber( ":MEASure:" + std::string( command.Mnemonic) + "? " + source);

        //
        // +9.9E+37, the one thing this instrument says when it cannot make a
        // measurement -- and all it says. There is no :MEASure:SENDvalid on
        // this family and no result-state code behind the number, so the reason
        // carried here is the same sentence every time (see kUnmeasurable).
        //
        // What that costs a script is real and is documented where a script can
        // see it: a handler that wanted to tell "no edge on the trace" from
        // "the trace is clipped" cannot, on this scope. The measurement's own
        // name is added because it is the one thing that *is* known -- "rise
        // time: measurement could not be made" at least says which question
        // went unanswered.
        //
        if( io::ScpiSession::isOverload( reading))
        {
            throw core::UnmeasurableReading( std::string( command.Name) + ": " + std::string( kUnmeasurable));
        }

        return reading;
    }
} // namespace hal::keysight_dsox1202g
