#include <filesystem>

#include <wx/app.h>
#include <wx/cmdline.h>

#include "app/frame.hpp"

namespace
{
    //
    // Where to look for installed suites when nothing says otherwise.
    //
    // Baked in by framework/ui/CMakeLists.txt from THORIUM_SUITE_PREFIX, and overridable
    // on the command line -- a default, not a binding. A bench PC is configured
    // once and a developer's machine has suites wherever they were built, and
    // neither should have to be the other.
    //
#ifndef THORIUM_SUITE_PREFIX
#define THORIUM_SUITE_PREFIX "/opt/thorium"
#endif
} // namespace

namespace ui
{
    class App : public wxApp
    {
        public:
            auto OnInit() -> bool override
            {
                if( !wxApp::OnInit())
                {
                    return false;
                }

                auto * frame = new MainFrame( mPrefix);

                frame->Show();

                return true;
            }

            auto OnInitCmdLine( wxCmdLineParser & parser) -> void override
            {
                wxApp::OnInitCmdLine( parser);

                parser.AddOption( "p", "prefix",
                                  "where installed suites are looked for",
                                  wxCMD_LINE_VAL_STRING);
            }

            auto OnCmdLineParsed( wxCmdLineParser & parser) -> bool override
            {
                wxString prefix;

                mPrefix = parser.Found( "prefix", &prefix)
                              ? std::filesystem::path( prefix.utf8_string())
                              : std::filesystem::path( THORIUM_SUITE_PREFIX);

                return wxApp::OnCmdLineParsed( parser);
            }

        private:
            std::filesystem::path  mPrefix;
    };
} // namespace ui

wxIMPLEMENT_APP( ui::App);
