#include "app/frame.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "app/options_dialog.hpp"

namespace ui
{
    namespace
    {
        enum
        {
            ID_Run = wxID_HIGHEST + 1,
            ID_Stop,
            ID_Safe,
            ID_Options,
            ID_Suite
        };

        //
        // The result columns. Deliberately the same facts the human log's
        // columns carry (see core/src/journal/report.cpp) and in the same
        // order: an operator who has read one should recognise the other, and
        // the RTF is what gets attached to the report afterwards.
        //
        enum Column { Col_Test, Col_What, Col_Value, Col_Verdict, Col_Detail };

        auto toWx( const std::string & text) -> wxString
        {
            return wxString::FromUTF8( text);
        }

        //
        // "1 test", "9 tests". English-only and knowingly so -- every other
        // string in this window is a literal in the same language, and a
        // pluralisation library for the four call sites here would be a
        // dependency pretending to be an answer. It exists because "1 tests" on
        // a bench console reads as a program that is not paying attention, and
        // a reader who notices that will wonder what else it is not paying
        // attention to.
        //
        auto describeCount( const std::size_t count, const wxString & noun) -> wxString
        {
            return wxString::Format( "%zu %s%s", count, noun, count == 1 ? "" : "s");
        }

        //
        // The colours a verdict is painted in. Named for meaning rather than
        // for hue, the way core::Emphasis is, so the window and the console
        // agree about what is worth noticing even though neither shares code
        // with the other.
        //
        const wxColour kPass{ 0, 128, 0 };
        const wxColour kFail{ 192, 0, 0 };
        const wxColour kQuiet{ 110, 110, 110 };

        //
        // The flags this window presents with a control of its own, and so
        // leaves out of the generated options dialog. See OptionsDialog's
        // constructor for why a list of names is the right thing here and is
        // not the duplication the rest of this design avoids.
        //
        // --quiet, --no-color and --events are absent because the operator
        // never sets those at all -- ui::buildRunCommand adds them and they are
        // not the window's to offer either. They would belong here the day
        // somebody decides an operator should be able to see them greyed out.
        //
        const std::vector<std::string> kHandledElsewhere{
            "--select",       // the test tree
            "--criteria",     // the picker, next to the master it inherits from
            "--dut-serial",   // the header field
            "--operator",     // the header field
            "--safe"          // the button, which is live at every moment
        };
    } // namespace

    MainFrame::MainFrame( const std::filesystem::path & prefix) :
        wxFrame( nullptr, wxID_ANY, "Thorium", wxDefaultPosition, wxSize( 1100, 720))
    {
        buildLayout();

        Bind( wxEVT_BUTTON, &MainFrame::onRun,     this, ID_Run);
        Bind( wxEVT_BUTTON, &MainFrame::onStop,    this, ID_Stop);
        Bind( wxEVT_BUTTON, &MainFrame::onSafe,    this, ID_Safe);
        Bind( wxEVT_BUTTON, &MainFrame::onOptions, this, ID_Options);
        Bind( wxEVT_CHOICE, &MainFrame::onSuiteChanged, this, ID_Suite);
        Bind( wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
        Bind( wxEVT_TREELIST_ITEM_CHECKED, &MainFrame::onItemChecked, this);

        reloadSuites( prefix);
    }

    auto MainFrame::buildLayout() -> void
    {
        auto * root  = new wxPanel( this);
        auto * outer = new wxBoxSizer( wxVERTICAL);

        // --- the header: which suite, which tolerances, which unit -----------
        auto * header = new wxFlexGridSizer( 4, wxSize( 8, 6));

        header->AddGrowableCol( 1, 2);
        header->AddGrowableCol( 3, 1);

        mSuitePicker    = new wxChoice( root, ID_Suite);
        mCriteriaPicker = new wxChoice( root, wxID_ANY);
        mDutSerial      = new wxTextCtrl( root, wxID_ANY);
        mOperator       = new wxTextCtrl( root, wxID_ANY);

        mDutSerial->SetHint( "SN-000123");

        //
        // Left empty rather than pre-filled from the environment. The runner
        // already falls back to $USER when --operator is absent (see
        // core::defaultRunInfo), and a field this window filled in would be
        // this window making a traceability claim on somebody's behalf.
        //
        mOperator->SetHint( "recorded in both logs");

        header->Add( new wxStaticText( root, wxID_ANY, "Suite:"), 0, wxALIGN_CENTER_VERTICAL);
        header->Add( mSuitePicker, 1, wxEXPAND);
        header->Add( new wxStaticText( root, wxID_ANY, "DUT serial:"), 0, wxALIGN_CENTER_VERTICAL);
        header->Add( mDutSerial, 1, wxEXPAND);

        auto * criteriaRow = new wxBoxSizer( wxHORIZONTAL);

        mMasterLabel = new wxStaticText( root, wxID_ANY, wxEmptyString);

        //
        // The master variant, quiet and beside the picker rather than in it.
        // It is not selectable -- see Suite::MasterCriteria -- but the applied
        // variant alone does not say which table the unchanged tolerances came
        // from, and an operator reading a result needs both halves.
        //
        mMasterLabel->SetForegroundColour( kQuiet);

        criteriaRow->Add( mCriteriaPicker, 1, wxEXPAND);
        criteriaRow->Add( mMasterLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

        header->Add( new wxStaticText( root, wxID_ANY, "Criteria:"), 0, wxALIGN_CENTER_VERTICAL);
        header->Add( criteriaRow, 1, wxEXPAND);
        header->Add( new wxStaticText( root, wxID_ANY, "Operator:"), 0, wxALIGN_CENTER_VERTICAL);
        header->Add( mOperator, 1, wxEXPAND);

        outer->Add( header, 0, wxEXPAND | wxALL, 10);

        // --- the two panes ----------------------------------------------------
        auto * splitter = new wxSplitterWindow( root, wxID_ANY);

        splitter->SetMinimumPaneSize( 220);

        //
        // A tree with checkboxes, because the thing being chosen is a two-level
        // catalog and ticking a group has to mean ticking its tests. The ids in
        // it come from --list-tests, so the class of error unmatchedSelection()
        // exists to catch cannot be committed here at all.
        //
        mTree = new wxTreeListCtrl( splitter, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTL_CHECKBOX | wxTL_3STATE | wxTL_DEFAULT_STYLE);

        //
        // A fixed first column and an autosized second, rather than two fixed
        // widths adding up to more than the pane they sit in -- which is what
        // the first version did, and it elided every description down to "The
        // meter r...hin tolerance". The descriptions come from the catalog and
        // are the reason the column is there at all.
        //
        mTree->AppendColumn( "Test", 150);
        mTree->AppendColumn( "Description", wxCOL_WIDTH_AUTOSIZE);

        mResults = new wxListCtrl( splitter, wxID_ANY,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxLC_REPORT | wxLC_SINGLE_SEL);

        //
        // Sized so that Detail -- the widest thing here and the one an operator
        // reads when something is wrong -- starts inside the pane rather than
        // past its right edge. A stderr row puts the runner's own diagnostic
        // there, and a diagnostic that needs a horizontal scroll to begin is a
        // diagnostic nobody reads.
        //
        mResults->AppendColumn( "Test",      wxLIST_FORMAT_LEFT,  120);
        mResults->AppendColumn( "Subject",   wxLIST_FORMAT_LEFT,  170);
        mResults->AppendColumn( "Value",     wxLIST_FORMAT_RIGHT,  90);
        mResults->AppendColumn( "Verdict",   wxLIST_FORMAT_LEFT,   70);
        mResults->AppendColumn( "Detail",    wxLIST_FORMAT_LEFT,  320);

        splitter->SplitVertically( mTree, mResults, 500);

        outer->Add( splitter, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

        // --- the bar ----------------------------------------------------------
        auto * bar = new wxBoxSizer( wxHORIZONTAL);

        mRunButton     = new wxButton( root, ID_Run,     "Run");
        mStopButton    = new wxButton( root, ID_Stop,    "Stop");
        mSafeButton    = new wxButton( root, ID_Safe,    "Safe the rig");
        mOptionsButton = new wxButton( root, ID_Options, "Options...");
        mProgress      = new wxGauge( root, wxID_ANY, 1, wxDefaultPosition, wxSize( 180, -1));
        mStatus        = new wxStaticText( root, wxID_ANY, "No suite selected");

        mStopButton->Enable( false);

        bar->Add( mRunButton, 0);
        bar->Add( mStopButton, 0, wxLEFT, 6);
        bar->Add( mOptionsButton, 0, wxLEFT, 6);
        bar->Add( mSafeButton, 0, wxLEFT, 24);
        bar->AddStretchSpacer( 1);
        bar->Add( mProgress, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        bar->Add( mStatus, 0, wxALIGN_CENTER_VERTICAL);

        outer->Add( bar, 0, wxEXPAND | wxALL, 10);

        root->SetSizer( outer);
    }

    auto MainFrame::currentSuite() const -> const Suite *
    {
        return mSuite < mSuites.size() ? &mSuites[ mSuite] : nullptr;
    }

    auto MainFrame::reloadSuites( const std::filesystem::path & prefix) -> void
    {
        mSuites = discoverSuites( prefix);

        mSuitePicker->Clear();

        for( const auto & suite : mSuites)
        {
            mSuitePicker->Append( toWx( suite.label()));
        }

        if( mSuites.empty())
        {
            mStatus->SetLabel( "No installed suite found under " + toWx( prefix.string()));

            return;
        }

        mSuitePicker->SetSelection( 0);

        selectSuite( 0);
    }

    auto MainFrame::selectSuite( const std::size_t index) -> void
    {
        mSuite = index;

        const auto * suite = currentSuite();

        if( !suite)
        {
            return;
        }

        //
        // The settings are dropped, not carried across. A --criteria or a
        // --replay path that made sense for one suite is not a claim this
        // window should silently re-make about another one.
        //
        mSettings = RunRequest{};

        mCriteriaPicker->Clear();

        for( const auto & variant : suite->CriteriaVariants)
        {
            mCriteriaPicker->Append( toWx( variant));
        }

        const auto fallback = mCriteriaPicker->FindString( toWx( suite->DefaultCriteria));

        mCriteriaPicker->SetSelection( fallback == wxNOT_FOUND ? 0 : fallback);

        mMasterLabel->SetLabel( suite->MasterCriteria.empty()
                                    ? wxString{}
                                    : "unchanged rows from " + toWx( suite->MasterCriteria));

        //
        // The manifest's catalog populates the tree immediately, so the window
        // is usable the moment a suite is picked; the binary is then asked and
        // the tree refilled from what it says. The two normally agree -- the
        // manifest was generated by asking this same binary at install time --
        // and when they do not, the binary is right.
        //
        fillTree( suite->Tests);

        askListTests( *suite);
    }

    auto MainFrame::askListTests( const Suite & suite) -> void
    {
        mQueryOutput.clear();

        //
        // The suite is copied into the handlers, not captured by reference.
        // mSuites is rebuilt by reloadSuites and reordered by nothing, but the
        // handler outlives this call by the length of a process -- and a
        // reference into a vector that a later suite change could reallocate is
        // the kind of bug that only shows up on the bench.
        //
        const auto binary = suite;

        mQuery = std::make_unique<ChildProcess>( ChildProcess::Handlers{
            .OnOutput = [ this]( const std::string & text) { mQueryOutput += text; },
            .OnEnded  = [ this, binary]( const int code, bool)
                        {
                            if( code != 0)
                            {
                                mStatus->SetLabel( "Could not run " + toWx( binary.Binary.string()));

                                return;
                            }

                            //
                            // The live catalog replaces the manifest's
                            // snapshot. They normally agree -- the manifest was
                            // generated by asking this same binary at install
                            // time -- and when they do not, the binary is the
                            // one that cannot be stale.
                            //
                            mSuites[ mSuite].Tests = parseTestList( mQueryOutput);

                            fillTree( mSuites[ mSuite].Tests);

                            mStatus->SetLabel( describeCount( mSuites[ mSuite].Tests.size(), "test"));

                            //
                            // CallAfter, not a direct call, and this is not
                            // defensive style -- it is required.
                            //
                            // askDescribeOptions assigns mQuery, which destroys
                            // the ChildProcess whose handler is running right
                            // now: this lambda is owned by that object. It does
                            // not crash, which is worse than if it did -- the
                            // first version of this simply never ran the second
                            // interrogation and said nothing about it.
                            //
                            // Deferring moves the assignment into the frame's
                            // own event dispatch, by which time the child
                            // process's handler has returned and its object is
                            // no longer in use.
                            //
                            CallAfter( [ this, binary] { askDescribeOptions( binary); });
                        }
        });

        if( !mQuery->start( buildListTestsCommand( suite)))
        {
            mStatus->SetLabel( "Could not start " + toWx( suite.Binary.string()));
        }
    }

    auto MainFrame::askDescribeOptions( const Suite & suite) -> void
    {
        mQueryOutput.clear();

        mQuery = std::make_unique<ChildProcess>( ChildProcess::Handlers{
            .OnOutput = [ this]( const std::string & text) { mQueryOutput += text; },
            .OnEnded  = [ this]( const int code, bool)
                        {
                            auto model = code == 0 ? parseOptionModel( mQueryOutput)
                                                   : std::nullopt;

                            if( model)
                            {
                                mOptionModel = std::move( *model);

                                mOptionsButton->Enable( true);

                                warnAboutMissingControls();

                                return;
                            }

                            //
                            // Survivable, and worth surviving rather than
                            // refusing the suite. A run_scripts predating
                            // --describe-options can still be run with a
                            // selection and flags typed by hand; what is lost
                            // is the generated dialog, which is a degraded
                            // console rather than no console. Said plainly,
                            // because an operator must know why the button is
                            // greyed.
                            //
                            mOptionModel.clear();
                            mOptionsButton->Enable( false);
                            mStatus->SetLabel( "This suite does not describe its options; the dialog is unavailable");
                        }
        });

        mQuery->start( buildDescribeCommand( suite));
    }

    //
    // Checks that every flag this window drives with a control of its own is
    // still a flag this binary accepts.
    //
    // The one seam in the design that a compiler cannot watch. Everything else
    // the window knows about the flags is generated from --describe-options;
    // kHandledElsewhere is the exception, and a name that falls out of step
    // with the runner would fail in the worst possible way -- the dedicated
    // control would go on sending a flag the parser now rejects, and the flag
    // would quietly reappear in the options dialog as well.
    //
    // Reported rather than fatal: the window is still usable, the operator can
    // still type the flag by hand, and a bench console that refuses to open
    // because a flag was renamed is worse than one that says so.
    //
    auto MainFrame::warnAboutMissingControls() -> void
    {
        std::vector<std::string> missing;

        for( const auto & flag : kHandledElsewhere)
        {
            const auto known = std::ranges::any_of( mOptionModel,
                                                    [ &flag]( const OptionInfo & option)
                                                    {
                                                        return std::ranges::find( option.Spellings, flag) != option.Spellings.end();
                                                    });

            if( !known)
            {
                missing.push_back( flag);
            }
        }

        if( missing.empty())
        {
            return;
        }

        wxString names;

        for( const auto & flag : missing)
        {
            names += ( names.IsEmpty() ? "" : ", ") + toWx( flag);
        }

        mStatus->SetForegroundColour( kFail);
        mStatus->SetLabel( "This suite does not accept " + names + " -- that control will not work");
    }

    auto MainFrame::fillTree( const std::vector<TestEntry> & tests) -> void
    {
        mTree->DeleteAllItems();

        const auto root = mTree->GetRootItem();

        wxTreeListItem  group;
        std::string     groupName;

        for( const auto & test : tests)
        {
            if( test.Group != groupName || !group.IsOk())
            {
                groupName = test.Group;
                group     = mTree->AppendItem( root, toWx( test.Group));

                mTree->CheckItem( group, wxCHK_CHECKED);
            }

            const auto item = mTree->AppendItem( group, toWx( test.Id));

            mTree->SetItemText( item, 1, toWx( test.Description));
            mTree->CheckItem( item, wxCHK_CHECKED);
        }

        //
        // wxTreeListCtrl has Expand( item) and no ExpandAll(), so the groups
        // are expanded one at a time. Only the groups: the tree is two levels
        // deep, matching the catalog's own shape, so there is nothing below
        // them to expand.
        //
        for( auto group = mTree->GetFirstChild( mTree->GetRootItem()); group.IsOk();
             group = mTree->GetNextSibling( group))
        {
            mTree->Expand( group);
        }
    }

    auto MainFrame::onItemChecked( wxTreeListEvent & event) -> void
    {
        const auto item = event.GetItem();

        //
        // wxTreeListCtrl does not propagate a check on its own -- wxTL_3STATE
        // only makes the *drawing* of a partially-checked parent possible. So a
        // group ticks its tests here, and a test updates its group's third
        // state, which is what makes "tick the group" mean what an operator
        // reading the tree assumes it means.
        //
        if( mTree->GetFirstChild( item).IsOk())
        {
            mTree->CheckItemRecursively( item, mTree->GetCheckedState( item));
        }
        else
        {
            mTree->UpdateItemParentStateRecursively( item);
        }
    }

    auto MainFrame::selectedTests() const -> std::vector<std::string>
    {
        std::vector<std::string>  selection;
        std::size_t               total = 0;

        for( auto group = mTree->GetFirstChild( mTree->GetRootItem()); group.IsOk();
             group = mTree->GetNextSibling( group))
        {
            for( auto test = mTree->GetFirstChild( group); test.IsOk();
                 test = mTree->GetNextSibling( test))
            {
                ++total;

                if( mTree->GetCheckedState( test) == wxCHK_CHECKED)
                {
                    selection.push_back( mTree->GetItemText( test).utf8_string());
                }
            }
        }

        //
        // Everything ticked is an empty selection, which is an absent --select.
        //
        // Not the same as passing every id, and the difference matters the day
        // a test is added to the catalog: an operator who ticked "all" means
        // all, and a window that had frozen today's list into a --select would
        // silently keep running yesterday's suite. See RunRequest::Selection.
        //
        if( selection.size() == total)
        {
            return {};
        }

        return selection;
    }

    auto MainFrame::onOptions( wxCommandEvent &) -> void
    {
        OptionsDialog dialog( this, mOptionModel, kHandledElsewhere, mSettings);

        if( dialog.ShowModal() == wxID_OK)
        {
            mSettings = dialog.settings();
        }
    }

    auto MainFrame::onSuiteChanged( wxCommandEvent &) -> void
    {
        selectSuite( static_cast<std::size_t>( mSuitePicker->GetSelection()));
    }

    auto MainFrame::onRun( wxCommandEvent &) -> void
    {
        const auto * suite = currentSuite();

        if( !suite || ( mRun && mRun->running()))
        {
            return;
        }

        auto request = mSettings;

        request.Selection = selectedTests();

        //
        // The three header fields are folded in here rather than living in the
        // options dialog, because they are not run options -- they are what
        // makes the resulting report traceable to a unit and a person, which is
        // why they are on the window itself and not behind a button.
        //
        const auto push = [ &request]( const std::string & flag, const wxString & value)
        {
            if( !value.IsEmpty())
            {
                request.Settings.push_back( OptionSetting{
                    .Flag = flag, .Value = value.utf8_string(), .Present = true
                });
            }
        };

        push( "--dut-serial", mDutSerial->GetValue());
        push( "--operator",   mOperator->GetValue());

        //
        // --criteria only when the picker has been moved off the build's own
        // default, which is the same "untouched emits nothing" rule the options
        // dialog follows and for the sharper version of the same reason.
        // Options::CriteriaVariant is deliberately left unresolved when absent
        // so that "the caller said nothing" and "the caller named the default"
        // stay distinguishable -- and an operator who never touched the picker
        // should follow the build's default the day it changes, not yesterday's
        // name frozen into a command line by this window.
        //
        if( mCriteriaPicker->GetSelection() != wxNOT_FOUND &&
            mCriteriaPicker->GetStringSelection() != toWx( suite->DefaultCriteria))
        {
            push( "--criteria", mCriteriaPicker->GetStringSelection());
        }

        mResults->DeleteAllItems();

        mExpected    = request.Selection.empty() ? suite->Tests.size() : request.Selection.size();
        mFinished    = 0;
        mFailed      = 0;
        mSawRunStart = false;
        mRunPassed.reset();
        mRunErrors.clear();

        mProgress->SetRange( static_cast<int>( std::max<std::size_t>( mExpected, 1)));
        mProgress->SetValue( 0);

        mRun = std::make_unique<ChildProcess>( ChildProcess::Handlers{
            .OnEvent  = [ this]( const RunEvent & event) { applyEvent( event); },
            .OnStderr = [ this]( const std::string & text)
                        {
                            //
                            // stderr is shown in the results list rather than
                            // swallowed. Everything the runner writes there is
                            // a reason a run did not happen -- a refused flag
                            // combination, an unopenable log, a failed
                            // preflight -- and in every one of those cases
                            // there will be no events at all, so this is the
                            // only thing the operator would see.
                            //
                            mRunErrors += text;

                            appendRow( "", "", "", "ERROR", toWx( text), false);
                        },
            .OnEnded  = [ this]( int code, bool crashed) { runFinished( code, crashed); }
        });

        if( !mRun->start( buildRunCommand( *suite, request)))
        {
            mStatus->SetLabel( "Could not start " + toWx( suite->Binary.string()));
            mRun.reset();

            return;
        }

        setRunning( true);
    }

    auto MainFrame::onStop( wxCommandEvent &) -> void
    {
        if( mRun && mRun->running())
        {
            mStatus->SetLabel( "Stopping...");

            mRun->requestStop();
        }
    }

    auto MainFrame::onSafe( wxCommandEvent &) -> void
    {
        safeTheRig();
    }

    auto MainFrame::safeTheRig() -> void
    {
        const auto * suite = currentSuite();

        if( !suite)
        {
            return;
        }

        //
        // Asynchronous, like everything else, and for the reason given in
        // MainFrame::askListTests: wxExecute has no capturing overload that
        // takes arguments already separated, and this program must not
        // re-split a path it did not choose.
        //
        // The button is disabled for the duration instead of the window
        // blocking. That is what stops a second press landing on a safing pass
        // already in flight, which is the only thing a blocking call was
        // buying.
        //
        mSafeButton->Enable( false);
        mStatus->SetForegroundColour( kQuiet);
        mStatus->SetLabel( "Safing the rig...");

        mQuery = std::make_unique<ChildProcess>( ChildProcess::Handlers{
            .OnEnded = [ this]( const int code, bool)
                       {
                           mSafeButton->Enable( true);

                           //
                           // The exit code, and only the exit code. --safe
                           // exits 0 when it completes (see main.cpp: safing is
                           // unconditional and has nothing to report a failure
                           // about), so anything else is a rig that was not
                           // safed -- the one thing in this window an operator
                           // must not read as success.
                           //
                           // `crashed` is deliberately ignored: this invocation
                           // asks for no event stream, so there is no runEnd
                           // for its absence to mean anything. Testing it here
                           // is what made every successful safing report
                           // "SAFING FAILED -- do not approach the fixture".
                           // ChildProcess now narrows the flag at the source as
                           // well, so this is belt and braces.
                           //
                           const bool safed = code == 0;

                           mStatus->SetForegroundColour( safed ? kQuiet : kFail);
                           mStatus->SetLabel( safed
                                                  ? "Rig safed: all outputs off, all relays open."
                                                  : "SAFING FAILED -- do not approach the fixture");

                           if( !safed)
                           {
                               wxMessageBox( "The rig could not be dropped to idle.\n\n"
                                             "Outputs may still be energised and relays may still be closed. "
                                             "Do not approach the fixture until the bench has been made safe "
                                             "by other means.",
                                             "Safing failed", wxOK | wxICON_ERROR, this);
                           }
                       }
        });

        if( !mQuery->start( buildSafeCommand( *suite)))
        {
            mSafeButton->Enable( true);
            mStatus->SetForegroundColour( kFail);
            mStatus->SetLabel( "SAFING FAILED -- could not start " + toWx( suite->Binary.string()));
        }
    }

    auto MainFrame::applyEvent( const RunEvent & event) -> void
    {
        switch( event.Which)
        {
            case RunEvent::Kind::RunStart:
                mSawRunStart = true;

                //
                // The one fact from the header that is promoted to the status
                // bar. core/journal/journal.hpp argues it belongs in a report
                // rather than being derivable from the command line, and the
                // same argument applies twice over to a live view: this is the
                // difference between watching a DUT and watching a file.
                //
                if( !event.Header.BenchAttached)
                {
                    mStatus->SetForegroundColour( kFail);
                    mStatus->SetLabel( "NO BENCH ATTACHED -- readings are from a file, not the rig");
                }
                break;

            case RunEvent::Kind::TestStart:
                mCurrentTest = toWx( event.Test);
                break;

            case RunEvent::Kind::PhaseStart:
                //
                // A hook's own bracket. Shown, because a run that fails in
                // RUN_SETUP never reaches a test and would otherwise produce a
                // window that sat empty and then said "failed".
                //
                mCurrentTest = toWx( event.Group.empty() ? event.Phase
                                                         : event.Group + " " + event.Phase);
                break;

            case RunEvent::Kind::Event:
                if( event.Verb == "Verify")
                {
                    appendRow( mCurrentTest,
                               toWx( event.SubjectGroup.empty() ? event.Subject
                                                                : event.SubjectGroup + "::" + event.Subject),
                               toWx( event.Value),
                               event.Passed.value_or( false) ? "PASS" : "FAIL",
                               toWx( event.CriterionText.empty() ? event.Detail : event.CriterionText),
                               event.Passed);
                }
                else if( event.Verb == "Measure" || event.Verb == "Read" || event.Verb == "Fetch")
                {
                    //
                    // The observation verbs only. The window shows what was
                    // read and what was concluded, which is the human log's
                    // rule (core/journal/report.hpp) rather than the machine
                    // log's -- an operator watching a rig is not
                    // reconstructing a routing sequence, and the SARIF file
                    // still has every Connect and Apply for whoever is.
                    //
                    //
                    // Value alone -- never Value plus Unit. The stream carries
                    // the same quantity three ways (see core::JournalRecord):
                    // Value is core::describeValue's printable form and ALREADY
                    // has the unit in it, while Numeric and Unit are there so a
                    // machine consumer can compare against a limit without
                    // re-parsing the text. Appending one to the other produced
                    // "0 V V" and "0 F F" on screen.
                    //
                    appendRow( mCurrentTest,
                               toWx( event.Subject),
                               toWx( event.Value),
                               wxEmptyString,
                               toWx( event.Detail),
                               std::nullopt);
                }
                break;

            case RunEvent::Kind::RunEnd:
                //
                // Kept rather than acted on here: runFinished needs it, and it
                // arrives before the process exit that calls runFinished.
                //
                mRunPassed = event.Passed;
                break;

            case RunEvent::Kind::TestEnd:
                ++mFinished;

                if( !event.Passed.value_or( false))
                {
                    ++mFailed;
                }

                mProgress->SetValue( static_cast<int>( std::min( mFinished, std::max<std::size_t>( mExpected, 1))));

                mStatus->SetLabel( wxString::Format( "%zu/%zu -- %zu failed", mFinished, mExpected, mFailed));
                break;

            default:
                break;
        }
    }

    auto MainFrame::appendRow( const wxString & test,
                               const wxString & what,
                               const wxString & value,
                               const wxString & verdict,
                               const wxString & detail,
                               const std::optional<bool> & passed) -> void
    {
        const auto row = mResults->InsertItem( mResults->GetItemCount(), test);

        mResults->SetItem( row, Col_What,    what);
        mResults->SetItem( row, Col_Value,   value);
        mResults->SetItem( row, Col_Verdict, verdict);
        mResults->SetItem( row, Col_Detail,  detail);

        //
        // Colour only where there is a verdict. An unset `passed` is not
        // "false" -- it is an event with no pass/fail notion at all, and
        // painting one red or green is exactly how an Apply comes to look like
        // a check that succeeded (see core::JournalRecord::Passed).
        //
        if( passed)
        {
            mResults->SetItemTextColour( row, *passed ? kPass : kFail);
        }

        mResults->EnsureVisible( row);
    }

    auto MainFrame::runFinished( const int exitCode, const bool crashed) -> void
    {
        setRunning( false);

        //
        // A run that never opened its journal never started, and that is a
        // different thing from one that stopped half way.
        //
        // run_scripts refuses before journal().begin() for every reason it can
        // know about up front -- a contradictory pair of flags, an unwritable
        // log, a recording with no rows for the selection, a preflight that
        // could not reach an instrument. None of those energises anything, so
        // there is nothing to safe and nothing unknown about the rig. What the
        // operator needs is the sentence run_scripts already wrote to stderr.
        //
        // Told apart by runStart rather than by the exit code, because both
        // cases exit non-zero and only the stream knows which happened. Getting
        // this wrong was not harmless: a desk with no meter attached produced
        // "the rig's state is unknown, do not approach the fixture" for a run
        // that had not touched a single relay.
        //
        if( !mSawRunStart)
        {
            mStatus->SetForegroundColour( kFail);
            mStatus->SetLabel( "The run did not start");

            wxMessageBox( mRunErrors.empty()
                              ? wxString( "The run did not start, and said nothing about why.")
                              : "The run did not start:\n\n" + toWx( mRunErrors),
                          "Run did not start", wxOK | wxICON_ERROR, this);

            return;
        }

        if( crashed)
        {
            //
            // Started and never finished: the process died with its journal
            // open, so whether hal::RigSafingGuard ran is unknown and the rig
            // may still be energised. The window says so and safes
            // unconditionally -- main.cpp put --safe in this binary for exactly
            // this caller.
            //
            mStatus->SetForegroundColour( kFail);
            mStatus->SetLabel( "Run ended without finishing -- safing the rig");

            //
            // Safed first, told second, and deliberately in that order: the
            // message box is modal and the safing pass is asynchronous, so
            // safing starts while the operator is still reading. Doing it the
            // other way round would leave a rig energised behind a dialog
            // nobody has clicked yet.
            //
            safeTheRig();

            wxMessageBox( "The run ended before it reported a result.\n\n"
                          "The rig is being dropped to idle, but its state during the failure is "
                          "unknown. The logs it had written up to that point are still on disk.",
                          "Run did not finish", wxOK | wxICON_ERROR, this);

            return;
        }

        //
        // The verdict comes from the stream, and falls back to the exit code
        // only if runEnd somehow carried none.
        //
        // Not the same thing, and a --skeleton run is where they come apart: it
        // exits 0 because its status is about the file it wrote rather than
        // about the checks (see README.md's note on the mode), while every
        // check in it failed against placeholder readings. Keying the banner to
        // the exit code put "PASSED -- 5 tests" in green above a list of red
        // FAILs.
        //
        // This is the window obeying its own rule -- it computes no verdict,
        // core::Journal already decided this one and put it in runEnd (see
        // MainFrame's class comment).
        //
        const bool passed = mRunPassed.value_or( exitCode == 0);

        mStatus->SetForegroundColour( passed ? kPass : kFail);
        mStatus->SetLabel( passed
                               ? "PASSED -- " + describeCount( mFinished, "test")
                               : wxString::Format( "FAILED -- %zu of %zu", mFailed, mFinished));
    }

    auto MainFrame::setRunning( const bool running) -> void
    {
        mRunButton->Enable( !running);
        mOptionsButton->Enable( !running && !mOptionModel.empty());
        mSuitePicker->Enable( !running);
        mCriteriaPicker->Enable( !running);
        mTree->Enable( !running);
        mStopButton->Enable( running);

        //
        // mSafeButton is untouched, deliberately. It is the one control that is
        // live at every moment -- during a run, after a crash, before anything
        // has started. See ui/README.md §4.
        //
    }

    auto MainFrame::onClose( wxCloseEvent & event) -> void
    {
        if( mRun && mRun->running())
        {
            //
            // Refused rather than asked-about-and-then-killed. Closing the
            // window would destroy the ChildProcess, and a run losing its
            // supervisor mid-test is the situation this whole program exists to
            // make visible rather than to cause.
            //
            if( wxMessageBox( "A run is in progress.\n\nStop it and close?",
                              "Run in progress", wxYES_NO | wxICON_QUESTION, this) != wxYES)
            {
                event.Veto();

                return;
            }

            mRun->requestStop();
        }

        event.Skip();
    }
} // namespace ui
