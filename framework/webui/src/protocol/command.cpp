#include "protocol/command.hpp"

namespace webui
{
    namespace
    {
        //
        // The flags the window owns rather than the operator. See the header on
        // why each is here and why --no-logs is not.
        //
        auto appendStreamFlags( std::vector<std::string> & argv) -> void
        {
            argv.emplace_back( "--quiet");
            argv.emplace_back( "--no-color");
            argv.emplace_back( "--events=-");
        }

        auto joined( const std::vector<std::string> & values, const char separator) -> std::string
        {
            std::string text;

            for( const auto & value : values)
            {
                if( !text.empty())
                {
                    text += separator;
                }

                text += value;
            }

            return text;
        }
    } // namespace

    auto buildRunCommand( const Suite & suite, const RunRequest & request) -> std::vector<std::string>
    {
        std::vector<std::string> argv{ suite.Binary.string() };

        appendStreamFlags( argv);

        if( !request.Selection.empty())
        {
            //
            // One comma-separated --select, which is what the flag takes: it is
            // not Repeatable, because a test id cannot contain a comma and so
            // the list spelling is the natural one (see cli::Repeatable).
            //
            argv.push_back( "--select=" + joined( request.Selection, ','));
        }

        for( const auto & setting : request.Settings)
        {
            if( !setting.Present)
            {
                continue;
            }

            //
            // A switch is its flag; everything else is flag=value. The form has
            // already resolved which of the two a setting is -- an empty Value
            // on a switch, a filled one otherwise -- so there is no second
            // lookup into the option model here and no way for the two to
            // disagree about a flag's kind.
            //
            argv.push_back( setting.Value.empty()
                                ? setting.Flag
                                : setting.Flag + "=" + setting.Value);
        }

        //
        // Last, so a hand-typed flag lands after the generated ones. That is
        // not a precedence claim -- run_scripts has no last-wins rule for most
        // flags, and --address deliberately takes the *first* override -- it is
        // so that what the operator typed is what the log's command line ends
        // with, where they will look for it.
        //
        for( const auto & extra : request.Extra)
        {
            argv.push_back( extra);
        }

        return argv;
    }

    auto buildSafeCommand( const Suite & suite) -> std::vector<std::string>
    {
        //
        // --safe alone. No --events: the mode writes no log and posts its Safe
        // event into a journal with no sinks (see main.cpp), so there would be
        // nothing on the stream to read. The window reports the exit code,
        // which for this mode is always 0.
        //
        return { suite.Binary.string(), "--safe" };
    }

    auto buildListTestsCommand( const Suite & suite) -> std::vector<std::string>
    {
        return { suite.Binary.string(), "--list-tests" };
    }

    auto buildDescribeCommand( const Suite & suite) -> std::vector<std::string>
    {
        return { suite.Binary.string(), "--describe-options" };
    }
} // namespace webui
