#pragma once

#include <optional>
#include <string_view>

#include "core/criteria/criteria_variants.hpp"
#include "core/criteria/criterion.hpp"
#include "core/criteria/predicate_text.hpp"
#include "core/journal/journal.hpp"
#include "core/quantities/format.hpp"
#include "core/quantities/quantity.hpp"

namespace core
{
    using quantities::Quantity;

    //
    // The verdict verbs -- so a script writes Verify( ...) next to Measure( ...),
    // rather than core::Verify( ...) next to a bare Measure. That asymmetry was
    // an accident of which header happened to bring what into scope, and the two
    // scripts in this suite had already drifted to opposite sides of it.
    //
    // Fail comes with it, and has to: it is what a script writes where a check
    // could not be made at all, so it appears in the same bodies and on the same
    // footing as Verify (see VerifyEngine own comment on why).
    //
    class VerifyEngine
    {
        public:
            template< typename Predicate, typename T>
                requires core::PredicateFor< Predicate, T>
            auto operator()( const Criterion<Predicate> & criterion, const T & value) const -> bool
            {
                const bool passed = criterion.predicate( value);

                reportResult( criterion.group, criterion.id, criterion.description,
                              describeValue( value), numericOf( value), unitOf<T>(),
                              describeCriterion( criterion.predicate), passed);

                return passed;
            }

            template< typename Predicate, typename T>
                requires core::PredicateFor< Predicate, T>
            auto operator()( std::string_view  description,
                              const Predicate & predicate,
                              const T &         value ) const -> bool
            {
                return operator()( Criterion{ std::string_view{}, std::string_view{}, description, predicate }, value);
            }

            template< typename Predicate, typename T>
                requires core::PredicateFor< Predicate, T>
            auto operator()( std::string_view  group,
                              std::string_view  id,
                              std::string_view  description,
                              const Predicate & predicate,
                              const T &         value ) const -> bool
            {
                return operator()( Criterion{ group, id, description, predicate }, value);
            }

            template< typename Predicate, typename Unit>
                requires core::PredicateFor< Predicate, double> || core::PredicateFor< Predicate, Quantity<Unit>>
            auto operator()( const Criterion<Predicate> & criterion, const std::optional< Quantity<Unit> > & reading) const -> bool
            {
                if( ! reading)
                {
                    reportResult( criterion.group, criterion.id, criterion.description,
                                  "<no reading>", std::nullopt, unitOf< Quantity<Unit> >(),
                                  describeCriterion( criterion.predicate), false);

                    return false;
                }

                if constexpr( core::PredicateFor< Predicate, Quantity<Unit>>)
                {
                    return operator()( criterion, *reading);
                }
                else
                {
                    return operator()( criterion, reading->value());
                }
            }

            template<typename... Variants, typename T>
                requires ( CriterionFor<Variants, T> && ...)
            auto operator()( const MultiCriterion<Variants...> & criterion, const T & value) const -> bool
            {
                return verifySelectedVariant( criterion, value, activeCriteriaVariant(),
                                              std::make_index_sequence<sizeof...( Variants)>{});
            }

            template<typename... Variants, typename Unit>
                requires ( ( CriterionFor<Variants, double> || CriterionFor<Variants, Quantity<Unit>>) && ...)
            auto operator()( const MultiCriterion<Variants...> & criterion, const std::optional< Quantity<Unit> > & reading) const -> bool
            {
                return verifySelectedVariant( criterion, reading, activeCriteriaVariant(),
                                              std::make_index_sequence<sizeof...( Variants)>{});
            }

            auto operator()( std::string_view reason) const -> void
            {
                reportNotChecked( {}, {}, reason, {});
            }

            template<typename Predicate>
            auto operator()( const Criterion<Predicate> & criterion, const std::string_view reason) const -> void
            {
                reportNotChecked( criterion.group, criterion.id, reason,
                                  describeCriterion( criterion.predicate));
            }

            template<typename... Variants>
            auto operator()( const MultiCriterion<Variants...> & criterion, const std::string_view reason) const -> void
            {
                const bool matched = failSelectedVariant(
                    criterion, reason, activeCriteriaVariant(),
                    std::make_index_sequence<sizeof...( Variants)>{});

                if( ! matched)
                {
                    reportNotChecked( criterion.group, criterion.id, reason, {});
                }
            }

        private:
            template<typename Multi, typename T, std::size_t... I>
            auto verifySelectedVariant( const Multi & multi,
                                        const T &     value,
                                        std::size_t   wanted,
                                        std::index_sequence<I...>) const -> bool
            {
                bool result = false;

                const bool matched = ( ( I == wanted && ( result = operator()( std::get<I>( multi.variants), value), true)) || ...);

                return matched && result;
            }

            template<typename Multi, std::size_t... I>
            auto failSelectedVariant( const Multi &          multi,
                                      const std::string_view reason,
                                      const std::size_t      wanted,
                                      std::index_sequence<I...>) const -> bool
            {
                return ( ( I == wanted && ( operator()( std::get<I>( multi.variants), reason), true)) || ...);
            }

            void reportResult( std::string_view      group,
                               std::string_view      id,
                               std::string_view      description,
                               std::string_view      valueText,
                               std::optional<double> numericValue,
                               std::string_view      unit,
                               std::string_view      criterionText,
                               bool                  passed) const
            {
                journal().post( JournalRecord{
                    .Method        = Verb::Verify,
                    .Subject       = std::string( id),
                    .SubjectGroup  = std::string( group),
                    .Detail        = std::string( description),
                    .Value         = std::string( valueText),
                    .Numeric       = numericValue,
                    .Unit          = std::string( unit),
                    .CriterionText = std::string( criterionText),
                    .Passed        = passed
                });
            }

            void reportNotChecked( std::string_view group,
                                   std::string_view id,
                                   std::string_view description,
                                   std::string_view criterionText) const
            {
                reportResult( group, id, description, kUncheckedValue,
                              std::nullopt, {}, criterionText, false);
            }
    };

    //
    // The single points every script verifies through:
    //
    //   Verify( FS_Supply_1::FS_Supply_5V0, voltage);
    //   Fail( "Console never responded");
    //
    // Defined once in core/src/verbs/verify.cpp.
    //
    extern const VerifyEngine Verify;
    extern const VerifyEngine Fail;
} // namespace core
