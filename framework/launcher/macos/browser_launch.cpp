#include "browser_launch.hpp"

#include "appkit.hpp"

namespace launcher
{
    auto findChrome() -> std::optional<std::string>
    {
        const auto  app = appkit::kUrlForBundleId(
            appkit::kSharedWorkspace.onClass(), appkit::string( "com.google.Chrome"));
        if ( app == nullptr)
        {
            return std::nullopt;
        }

        const auto  bundle = appkit::kBundleWithUrl.onClass( app);
        const auto  executable = bundle != nullptr ? appkit::kExecutablePath( bundle) : nullptr;
        if ( executable == nullptr)
        {
            return std::nullopt;
        }
        return std::string( appkit::kFileSystemRepresentation( executable));
    }

    auto buildAppModeArgv(
        const std::string & chromePath,
        const std::string & url,
        const std::string & userDataDir) -> std::vector<std::string>
    {
        return {
            chromePath,
            "--app=" + url,
            "--user-data-dir=" + userDataDir,
            "--window-size=1024,768",
            "--no-first-run",
            "--no-default-browser-check",
        };
    }
}
