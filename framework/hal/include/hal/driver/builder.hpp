#pragma once

#include <utility>

namespace hal
{
    //
    // The fluent chain a script builds before handing a config to a verb:
    //
    //     Apply( DcP1.dc().voltage( 24_V).currentLimit( 7_A));
    //     Setup( Osc1.trigger().slope( TriggerSlope::Rising).level( 2.5_V));
    //     Setup( Ser1.rs232().baudRate( 9600).parity( Parity::None));
    //
    // Every driver in instruments/ has one, and before this they each wrote
    // the same four lines per setting:
    //
    //     auto slope( const TriggerSlope value) const -> DSO8064ATriggerBuilder
    //     {
    //         auto copy = *this;
    //         copy.mConfig.Slope = value;
    //         return copy;
    //     }
    //
    // Thirty-six of those across five drivers, differing in the field named
    // and nothing else. What is left with this is the line that carries the
    // information:
    //
    //     auto slope( TriggerSlope value) const { return with( &Config::Slope, value); }
    //
    // ---------------------------------------------------------------------
    // Why a base class rather than a macro
    // ---------------------------------------------------------------------
    //
    // A DECLARE_SETTING( slope, TriggerSlope, Slope) macro would be shorter
    // still, and it is the wrong trade here. The declarative macros in this
    // framework -- CRITERIA/CRIT, GROUP/TEST, WIRE_INSTRUMENT -- exist so that
    // a *table of data* reads as a table to a test engineer. A driver's
    // setters are not a table: each one carries a name chosen for the
    // instrument's own vocabulary, a type, and very often a paragraph
    // explaining what the setting means on that instrument (see
    // hal::keysight_dsox1202g::AcquisitionBuilder::averagedOver, which is mostly comment).
    // A macro would put all of that inside an argument list and take the
    // declarations out of reach of every tool that reads C++.
    //
    // ---------------------------------------------------------------------
    // Why CRTP
    // ---------------------------------------------------------------------
    //
    // Because a setter has to return the *derived* builder for chaining, and a
    // base class does not otherwise know what that is. DerivedT is the type
    // with() and changed() hand back, so `.slope( ...).level( ...)` keeps
    // working exactly as it did.
    //
    // The two class templates among the builders -- hal::keysight_edu36311a::DcBuilder and
    // hal::keysight_ac6834b::AcBuilder -- inherit from a *dependent* base, so unqualified
    // lookup does not find mConfig inside them and they spell it this->mConfig.
    // That is ordinary C++ rather than a wrinkle of this design, and it is
    // confined to the handful of places those two touch the config directly.
    //
    template<typename DerivedT, typename ConfigT>
    class ConfigBuilder
    {
        public:
            using Config = ConfigT;

            //
            // What the verb reads. Every driver's applyDriver/setupDriver/
            // describeConfig takes this, by const reference, and a builder
            // exists only to produce it.
            //
            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        protected:
            //
            // Built from a whole config rather than field by field, so that a
            // driver whose config has a non-optional member (a scope channel,
            // see hal::keysight_dsox1202g::ChannelConfig) passes it and every other driver
            // passes nothing at all: `Config{ instrument }` leaves an aggregate
            // of optionals empty, which is what "the builder was never told"
            // means everywhere in this framework.
            //
            explicit ConfigBuilder( Config config) : mConfig( std::move( config)) {}

            //
            // One field, named by pointer-to-member: the whole of an ordinary
            // setter.
            //
            // The field is a pointer-to-member rather than a string or an
            // index, so a typo is "no member named Slop in ..." at the setter's
            // own line, and so the value has to be assignable to that field's
            // real type. Nothing here is looser than the four lines it
            // replaces.
            //
            template<typename FieldT, typename ValueT>
            [[nodiscard]]
            auto with( FieldT Config::* field, ValueT && value) const -> DerivedT
            {
                auto copy = derived();

                copy.mConfig.*field = std::forward<ValueT>( value);

                return copy;
            }

            //
            // Anything a single field does not cover: two settings that are one
            // instruction (hal::keysight_dsox1202g::AcquisitionBuilder::points, which also
            // clears the automatic choice), or a value the setter computes
            // before storing (hal::keysight_ac6834b::AcBuilder::range, which stores the
            // range the instrument would actually select rather than the
            // voltage it was handed).
            //
            // Deliberately offered rather than left to those setters to write
            // out by hand: the copy-modify-return shape is the part worth
            // stating once, and a setter that needs two fields should not have
            // to drop out of the vocabulary to get them.
            //
            template<typename ChangeT>
            [[nodiscard]]
            auto changed( ChangeT change) const -> DerivedT
            {
                auto copy = derived();

                change( copy.mConfig);

                return copy;
            }

            //
            // Protected rather than private: a builder that rebuilds its whole
            // config reaches it directly. hal::keysight_ac6834b::AcBuilder is the one that
            // does -- its per-phase setters widen a balanced config into a
            // per-phase one, which is a change of the config's *type* and so
            // not something with() or changed() can express.
            //
            Config mConfig;

        private:
            [[nodiscard]]
            auto derived() const -> const DerivedT &
            {
                return static_cast<const DerivedT &>( *this);
            }
    };
} // namespace hal
