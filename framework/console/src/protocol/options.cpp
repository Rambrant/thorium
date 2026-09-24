#include "protocol/options.hpp"

#include "protocol/json.hpp"

namespace console
{
    auto optionKindFrom( const std::string_view name) -> OptionKind
    {
        if( name == "switch") return OptionKind::Switch;
        if( name == "text")   return OptionKind::Text;
        if( name == "number") return OptionKind::Number;
        if( name == "list")   return OptionKind::List;

        return OptionKind::Unknown;
    }

    auto parseOptionModel( const std::string_view json) -> std::optional<std::vector<OptionInfo>>
    {
        const auto document = Json::parse( json);

        if( !document)
        {
            return std::nullopt;
        }

        const auto * options = document->at( "options");

        if( !options)
        {
            return std::nullopt;
        }

        std::vector<OptionInfo> model;

        for( const auto & entry : options->items())
        {
            const auto * flags = entry->at( "flags");

            //
            // A flag with no spelling is not a flag, and OptionInfo::flag()
            // would be reading an empty vector. cli::optionsModel cannot
            // produce one -- it skips members without a Flag annotation -- so
            // this is a document that did not come from where it claims to.
            //
            if( !flags || flags->items().empty())
            {
                return std::nullopt;
            }

            OptionInfo info;

            for( const auto & spelling : flags->items())
            {
                info.Spellings.push_back( spelling->text());
            }

            info.Help        = entry->textAt( "help");
            info.Placeholder = entry->textAt( "placeholder");
            info.Noun        = entry->textAt( "noun");
            info.Kind        = optionKindFrom( entry->textAt( "kind"));
            info.Clears      = entry->boolAt( "clears");
            info.Repeatable  = entry->boolAt( "repeatable");
            info.Positive    = entry->boolAt( "positive");
            info.Query       = entry->boolAt( "query");

            model.push_back( std::move( info));
        }

        return model;
    }
} // namespace console
