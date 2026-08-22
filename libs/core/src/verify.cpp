#include "core/verify.hpp"

#include <string>

#include "core/journal.hpp"

namespace core::detail
{
    //
    // Posts to the run journal (core/journal.hpp) rather than printing. This
    // used to write a line straight to stdout through core::Logger, which meant
    // the operator's live view and the test record were the same single
    // hard-coded format: there was no way to add a second, differently-shaped
    // log without either duplicating this call or reformatting stdout after the
    // fact.
    //
    // Nothing is printed here any more -- core::ConsoleSink renders the same
    // event to the terminal (see core/console_sink.hpp), so what an operator
    // sees is now provably the same content the RTF file gets rather than a
    // separately-worded near-copy of it. A run with no sinks registered
    // (a unit-test binary, for instance) logs nothing at all, which is correct:
    // Verify's contract is its return value, not its output.
    //
    void reportResult( const std::string_view      group,
                        const std::string_view      id,
                        const std::string_view      description,
                        const std::string_view      valueText,
                        const std::optional<double> numericValue,
                        const std::string_view      unit,
                        const std::string_view      criterionText,
                        const bool                  passed )
    {
        //
        // group and id stay separate fields rather than being pasted into one
        // "FS_Supply_1::FS_Supply_5V0" subject: the machine log recombines them
        // into its ruleId (see core::SarifSink::ruleIdFor), and a consumer
        // querying "every result for this criterion, across runs" needs the id
        // as its own value, not as a substring. Note this is the criterion's
        // own CRITERIA group -- distinct from the running test's group, which
        // the journal stamps on separately; see JournalRecord::SubjectGroup.
        //
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

    //
    // "<unchecked>" -- eleven characters, which fits the human report's value
    // column (kValueWidth in core/src/report.cpp) exactly the way "<no
    // reading>" does in the absent-reading Verify overload. A longer
    // placeholder would push the limit and the verdict rightwards on this one
    // row and stagger it against the checks either side of it.
    //
    // A placeholder rather than an empty column, for the same reason that
    // overload gives: a [FAIL] beside an empty value reads like a formatting
    // bug, when the absence of the value IS the finding.
    //
    void reportNotChecked( const std::string_view group,
                            const std::string_view id,
                            const std::string_view description,
                            const std::string_view criterionText )
    {
        constexpr std::string_view UncheckedValue = "<unchecked>";

        reportResult( group, id, description, UncheckedValue,
                      std::nullopt, {}, criterionText, false);
    }
} // namespace core::detail

namespace core
{
    auto Fail( const std::string_view reason) -> bool
    {
        detail::reportNotChecked( {}, {}, reason, {});

        return false;
    }
} // namespace core
