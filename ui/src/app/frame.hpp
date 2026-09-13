#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <wx/frame.h>
#include <wx/treelist.h>

#include "app/process.hpp"
#include "protocol/command.hpp"
#include "protocol/options.hpp"
#include "protocol/suite.hpp"

class wxButton;
class wxChoice;
class wxGauge;
class wxListCtrl;
class wxStaticText;
class wxTextCtrl;

namespace ui
{
    //
    // The window. Three areas and a bar, as laid out in ui/README.md §3.
    //
    // It holds no state about a run beyond what is on screen, and computes no
    // verdict. Every PASS and FAIL it shows arrived in the event stream with
    // `passed` already decided by core::Journal, and the counters in the status
    // bar count events rather than deriving outcomes. That restraint is the
    // point: a window that tallied its own result would be a second
    // implementation of Journal::endTest, in another process, that nothing
    // compares against the first.
    //
    class MainFrame : public wxFrame
    {
        public:
            explicit MainFrame( const std::filesystem::path & prefix);

        private:
            // --- construction ---
            auto buildLayout() -> void;

            // --- the suite the window is pointed at ---
            auto reloadSuites( const std::filesystem::path & prefix) -> void;
            auto selectSuite( std::size_t index) -> void;
            auto currentSuite() const -> const Suite *;

            //
            // Asks the selected binary about itself: --list-tests for the tree,
            // then --describe-options for the dialog.
            //
            // Asynchronous, like a run, and chained -- the second starts when
            // the first ends. Not a preference: wxExecute's capturing overloads
            // take a command *string*, so the only way to run these with their
            // arguments already separated is the same argv path a run uses (see
            // ChildProcess::start). An install prefix this program did not
            // choose can contain a space, and a re-split command line is how
            // that becomes two arguments.
            //
            // The window stays usable throughout, which is the right behaviour
            // anyway: the suite picker and the safe button have nothing to wait
            // for.
            //
            auto askListTests( const Suite & suite) -> void;
            auto askDescribeOptions( const Suite & suite) -> void;

            //
            // Whichever interrogation or safing call is in flight. One slot,
            // because they are chained rather than concurrent, and holding it
            // is what keeps the handler alive long enough to be called.
            //
            // Assigning it from inside its own ChildProcess's handler destroys
            // the object that owns the running lambda. Anything that chains one
            // call to the next must go through CallAfter -- see askListTests,
            // where getting this wrong produced a window that silently skipped
            // a step rather than crashing.
            //
            std::unique_ptr<ChildProcess>  mQuery;
            std::string                    mQueryOutput;

            // --- the catalog tree ---
            auto warnAboutMissingControls() -> void;

            // --- the catalog tree ---
            auto fillTree( const std::vector<TestEntry> & tests) -> void;
            auto selectedTests() const -> std::vector<std::string>;

            // --- running ---
            auto onRun( wxCommandEvent &) -> void;
            auto onStop( wxCommandEvent &) -> void;
            auto onSafe( wxCommandEvent &) -> void;

            //
            // The action behind the button, callable without an event -- see
            // runFinished, which safes a crashed run without anybody pressing
            // anything.
            //
            auto safeTheRig() -> void;
            auto onOptions( wxCommandEvent &) -> void;
            auto onSuiteChanged( wxCommandEvent &) -> void;
            auto onItemChecked( wxTreeListEvent & event) -> void;
            auto onClose( wxCloseEvent & event) -> void;

            auto applyEvent( const RunEvent & event) -> void;
            auto runFinished( int exitCode, bool crashed) -> void;
            auto setRunning( bool running) -> void;

            auto appendRow( const wxString & test,
                            const wxString & what,
                            const wxString & value,
                            const wxString & verdict,
                            const wxString & detail,
                            const std::optional<bool> & passed) -> void;

            // --- widgets ---
            wxChoice *      mSuitePicker{ nullptr };
            wxChoice *      mCriteriaPicker{ nullptr };
            wxStaticText *  mMasterLabel{ nullptr };
            wxTextCtrl *    mDutSerial{ nullptr };
            wxTextCtrl *    mOperator{ nullptr };

            wxTreeListCtrl * mTree{ nullptr };
            wxListCtrl *     mResults{ nullptr };

            wxButton *      mRunButton{ nullptr };
            wxButton *      mStopButton{ nullptr };
            wxButton *      mSafeButton{ nullptr };
            wxButton *      mOptionsButton{ nullptr };
            wxGauge *       mProgress{ nullptr };
            wxStaticText *  mStatus{ nullptr };

            // --- model ---
            std::vector<Suite>        mSuites;
            std::size_t               mSuite{ 0 };
            std::vector<OptionInfo>   mOptionModel;
            RunRequest                mSettings;

            std::unique_ptr<ChildProcess>  mRun;

            //
            // How many tests the run was asked for, and how many have ended.
            // Counted, not derived: the gauge is the only thing in the window
            // that needs a total, and the total is known before the run from
            // the selection rather than from anything the stream says.
            //
            //
            // Whether the run ever opened its journal. What tells "it refused
            // to start" from "it died half way", which are reported very
            // differently -- see runFinished.
            //
            bool         mSawRunStart{ false };

            // Everything the run wrote to stderr, for the same reason.
            std::string  mRunErrors;

            //
            // The run's own verdict, as runEnd reported it. Unset until then --
            // and not the same as "the process exited 0", which is what a
            // --skeleton run exits regardless of what its checks did.
            //
            std::optional<bool>  mRunPassed;

            std::size_t  mExpected{ 0 };
            std::size_t  mFinished{ 0 };
            std::size_t  mFailed{ 0 };

            wxString     mCurrentTest;
    };
} // namespace ui
