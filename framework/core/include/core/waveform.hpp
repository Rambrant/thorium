#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

namespace core
{
    //
    // A captured trace: the whole record an instrument holds after a triggered
    // acquisition, rather than one number taken off it.
    //
    // Deliberately NOT a core::Bytes, which is the type it would be easiest to
    // reach for since :WAVeform:DATA? hands back a block of octets. A payload
    // is octets whose meaning belongs to a protocol this framework does not
    // know; a trace is a sequence of *readings in a unit*, taken at a known
    // rate, and every question anybody asks of one -- how deep the dip went,
    // how long it lasted, what the level was before it -- is arithmetic in that
    // unit. Storing it as bytes would put the scaling somewhere else and leave
    // this type unable to answer any of them.
    //
    // Deliberately NOT a core::QuantityVariant either, for the reason
    // core::Bytes is not one: a QuantityVariant is one number in one unit, and
    // the whole of what a trace adds is that it has a length, its elements have
    // positions, and the positions are times.
    //
    // ---------------------------------------------------------------------
    // Values in units, not raw counts
    // ---------------------------------------------------------------------
    //
    // The samples are stored as magnitudes in the unit named by kind(), already
    // scaled -- not as the instrument's raw ADC counts with a preamble beside
    // them to convert with. That is a decision with consequences, so: the raw
    // form is what an Infiniium actually sends (:WAVeform:PREamble? carries the
    // y-origin, y-increment and y-reference to undo it), and keeping it would
    // make a recording byte-faithful to the transfer.
    //
    // It would also make the recording meaningless without the instrument that
    // wrote it. A raw count is a fact about one vendor's digitiser at one
    // vertical setting; scaling is the driver's job, and the driver is the only
    // thing that knows the encoding. This is the same choice core::MeasureEngine
    // already makes one number at a time -- a recording holds "5.02 V", never
    // the DMM's count and the range it was on -- and a framework that decided
    // the question one way for a scalar and the other way for a trace would
    // have two answers to "what is in a recording".
    //
    // ---------------------------------------------------------------------
    // What a script does with one
    // ---------------------------------------------------------------------
    //
    // Reduces it to a number and checks that, which is why the reductions below
    // are members and why there is no criterion that takes a trace. A criterion
    // is a stated limit a test specification carries (see core/criterion.hpp);
    // "the waveform is correct" is not one, and the shapes that could be --
    // masks, templates, envelopes -- are a specification language of their own
    // that nothing on this rig has asked for. A lambda over the samples covers
    // the cases a reduction does not:
    //
    //     const auto trace = Fetch( Osc1.channel<3>().waveform());
    //
    //     Verify( FS_Transient_1::FS_Dip_Depth, trace.minimum<Voltage>());
    //
    //     const auto settled = std::ranges::all_of(
    //         trace.samples() | std::views::drop( 500),
    //         []( const auto v) { return v > 4.9; });
    //
    class Waveform
    {
        public:
            //
            // The two numbers that turn a sample index into a time: when
            // sample zero was taken, and how long apart the samples are.
            //
            // Relative to the trigger, so Origin is normally negative -- the
            // record starts before the event that caused it. That is the
            // instrument's own convention (:WAVeform:XORigin) and the useful
            // one: "40 ms after the trigger" is what a test talks about, and
            // an absolute wall-clock start would have to be corrected by it
            // anyway.
            //
            struct Timing
            {
                quantities::Time  Origin{};
                quantities::Time  Increment{};

                [[nodiscard]] auto operator==( const Timing &) const -> bool = default;
            };

            Waveform() = default;

            Waveform( const QuantityKind kind, const Timing timing, std::vector<double> samples)
                : mKind( kind), mTiming( timing), mSamples( std::move( samples))
            {}

            [[nodiscard]] auto kind()    const -> QuantityKind   { return mKind;           }
            [[nodiscard]] auto timing()  const -> Timing         { return mTiming;         }
            [[nodiscard]] auto size()    const -> std::size_t    { return mSamples.size(); }
            [[nodiscard]] auto empty()   const -> bool           { return mSamples.empty();}

            //
            // The samples themselves, for the checks no reduction covers -- see
            // this class's own comment on why a lambda is the answer there.
            //
            [[nodiscard]] auto samples() const -> std::span<const double> { return mSamples; }

            //
            // Bounds-checked, and the only element accessor -- there is no
            // unchecked operator[], for the reason core::Bytes::at gives. A
            // record that came back shorter than the script assumed is exactly
            // the failure this type exists around.
            //
            [[nodiscard]] auto at( std::size_t index) const -> double;

            // When the sample at that index was taken, relative to the trigger.
            [[nodiscard]] auto timeAt( std::size_t index) const -> quantities::Time;

            //
            // The reductions, as the caller's own unit:
            //
            //     trace.minimum<Voltage>()
            //
            // Explicit rather than inferred, and checked against kind() the way
            // every other crossing from the runtime-tagged world into a
            // concrete Quantity<Unit> is (see core::asQuantity) -- so asking a
            // current trace for a voltage is an error naming both, not a number
            // in the wrong unit.
            //
            // Each throws std::out_of_range on an empty trace rather than
            // answering zero or a NaN. There is no minimum of nothing, and a
            // capture that came back empty is a bench fault worth stopping on
            // rather than a reading worth checking against a limit.
            //
            template<quantities::QuantityType Q> [[nodiscard]] auto minimum()    const -> Q { return asQuantity<Q>( minimumValue());    }
            template<quantities::QuantityType Q> [[nodiscard]] auto maximum()    const -> Q { return asQuantity<Q>( maximumValue());    }
            template<quantities::QuantityType Q> [[nodiscard]] auto peakToPeak() const -> Q { return asQuantity<Q>( peakToPeakValue()); }
            template<quantities::QuantityType Q> [[nodiscard]] auto mean()       const -> Q { return asQuantity<Q>( meanValue());       }

            //
            // The same four, still tagged with the trace's own kind rather than
            // unwrapped -- what a caller that does not know the unit at compile
            // time uses, and what the four above are built from.
            //
            [[nodiscard]] auto minimumValue()    const -> QuantityVariant;
            [[nodiscard]] auto maximumValue()    const -> QuantityVariant;
            [[nodiscard]] auto peakToPeakValue() const -> QuantityVariant;
            [[nodiscard]] auto meanValue()       const -> QuantityVariant;

            [[nodiscard]] auto operator==( const Waveform &) const -> bool = default;

        private:
            QuantityKind         mKind{};
            Timing               mTiming{};
            std::vector<double>  mSamples;
    };

    //
    // How a trace is written down in both logs, and the rule is that it is
    // never written down: a summary stands in for it.
    //
    //     4096 pts @ 1 us, 5.02 V pk-pk
    //
    // Not a bound on an otherwise-full rendering, the way core::describeValue
    // for a payload abridges a long one (see kMaxDescribedBody in
    // core/bytes.hpp) -- there is no length at which spelling out four thousand
    // numbers is what a reader wanted. The three facts here are the ones that
    // tell a reader whether the capture is the one they were expecting; the
    // trace itself is in the recording, at full precision, which is what
    // --replay reads.
    //
    [[nodiscard]]
    auto describeValue( const Waveform & value) -> std::string;
} // namespace core
