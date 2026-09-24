#include "protocol/suite.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include "protocol/json.hpp"

namespace webui
{
    namespace
    {
        constexpr std::string_view kManifestName = "manifest.json";
    }

    auto Suite::label() const -> std::string
    {
        auto directory = Manifest.parent_path();

        //
        // <prefix>/bin is where CMAKE_INSTALL_BINDIR puts every suite, so the
        // immediate parent is the same word for all of them. What differs is
        // the prefix each was installed under.
        //
        if( directory.filename() == "bin")
        {
            directory = directory.parent_path();
        }

        //
        // Relative to the scanned prefix, so a root holding several suites
        // shows what separates them rather than the path they share. An
        // error_code overload: a manifest on a different volume from the
        // prefix has no relative path, and that is a labelling problem rather
        // than something to throw out of a picker.
        //
        std::error_code       failed;
        const auto            relative = std::filesystem::relative( directory, Root, failed);

        if( !failed && !relative.empty() && relative != ".")
        {
            return relative.generic_string();
        }

        //
        // The suite is the prefix -- the ordinary single-install case. Named
        // after the prefix itself, which is what the operator chose.
        //
        if( directory.has_filename())
        {
            return directory.filename().string();
        }

        //
        // Nothing usable anywhere. The binary's own path is long and ugly and
        // is still better than a blank entry nobody can identify.
        //
        return Binary.string();
    }

    auto parseTestList( const std::string_view text) -> std::vector<TestEntry>
    {
        std::vector<TestEntry>  tests;
        std::size_t             start = 0;

        while( start <= text.size())
        {
            const auto newline = text.find( '\n', start);
            const auto end     = ( newline == std::string_view::npos) ? text.size() : newline;

            auto line = text.substr( start, end - start);

            if( !line.empty() && line.back() == '\r')
            {
                line.remove_suffix( 1);
            }

            const auto firstPipe = line.find( '|');

            if( firstPipe != std::string_view::npos)
            {
                const auto rest       = line.substr( firstPipe + 1);
                const auto secondPipe = rest.find( '|');

                if( secondPipe != std::string_view::npos)
                {
                    tests.push_back( TestEntry{
                        .Group       = std::string( line.substr( 0, firstPipe)),
                        .Id          = std::string( rest.substr( 0, secondPipe)),
                        .Description = std::string( rest.substr( secondPipe + 1))
                    });
                }
            }

            if( newline == std::string_view::npos)
            {
                break;
            }

            start = newline + 1;
        }

        return tests;
    }

    auto parseManifest( const std::string_view json,
                        const std::filesystem::path & manifestPath,
                        const std::filesystem::path & root) -> std::optional<Suite>
    {
        const auto document = Json::parse( json);

        if( !document)
        {
            return std::nullopt;
        }

        Suite suite;

        suite.Manifest = manifestPath;
        suite.Root     = root;

        //
        // "binary" is a bare filename in the manifest ("run_scripts"), resolved
        // against the manifest's own directory. That is what makes an install
        // tree relocatable: copying a prefix to another machine, or mounting it
        // at a different path, must not need the manifest rewritten.
        //
        const auto binary = document->textAt( "binary");

        if( binary.empty())
        {
            return std::nullopt;
        }

        suite.Binary          = manifestPath.parent_path() / binary;
        suite.DefaultCriteria = document->textAt( "defaultCriteriaVariant");
        suite.MasterCriteria  = document->textAt( "masterCriteriaVariant");

        if( const auto * variants = document->at( "criteriaVariants"))
        {
            for( const auto & variant : variants->items())
            {
                suite.CriteriaVariants.push_back( variant->text());
            }
        }

        if( const auto * tests = document->at( "tests"))
        {
            for( const auto & test : tests->items())
            {
                suite.Tests.push_back( TestEntry{
                    .Group       = test->textAt( "group"),
                    .Id          = test->textAt( "id"),
                    .Description = test->textAt( "description")
                });
            }
        }

        return suite;
    }

    auto discoverSuites( const std::filesystem::path & prefix) -> std::vector<Suite>
    {
        std::vector<Suite>  suites;
        std::error_code     failed;

        //
        // The non-throwing overloads throughout. A prefix that does not exist,
        // a directory that cannot be read, a dangling symlink on a network
        // mount -- every one of those is a thing a bench PC does on a Monday
        // morning, and none of them should be an exception escaping into a UI
        // event handler. An unreadable prefix yields no suites, which is what
        // the window then says.
        //
        auto walk = std::filesystem::recursive_directory_iterator(
                        prefix,
                        std::filesystem::directory_options::skip_permission_denied,
                        failed);

        if( failed)
        {
            return suites;
        }

        for( const auto & entry : walk)
        {
            if( entry.path().filename() != kManifestName)
            {
                continue;
            }

            std::ifstream file( entry.path());

            if( !file)
            {
                continue;
            }

            std::ostringstream text;

            text << file.rdbuf();

            //
            // A malformed manifest is skipped, not reported. One broken install
            // under a shared prefix must not take the others with it, and the
            // operator's recourse is the same either way: pick another suite,
            // or tell whoever owns the prefix.
            //
            if( auto suite = parseManifest( text.str(), entry.path(), prefix))
            {
                suites.push_back( std::move( *suite));
            }
        }

        return suites;
    }
} // namespace webui
