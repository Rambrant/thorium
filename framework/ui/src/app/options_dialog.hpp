#pragma once

#include <string>
#include <vector>

#include <wx/dialog.h>

#include "protocol/command.hpp"
#include "protocol/options.hpp"

class wxCheckBox;
class wxSpinCtrl;
class wxTextCtrl;

namespace ui
{
    //
    // Every flag run_scripts accepts, as a form -- built from
    // --describe-options and from nothing else.
    //
    // There is no list of flags in this file and there must never be one. The
    // mapping from an option to a control is by *kind*, four cases, and that is
    // the entire body of knowledge here:
    //
    //   switch  -> a checkbox, pre-ticked when the flag clears (a --no-... flag
    //              exists to turn something off, so the thing is on)
    //   number  -> a spinner, floored at 1 when the flag is positive
    //   list    -> a text field; the value is passed through as typed, since
    //              the flag's own spelling already says whether a comma
    //              separates items
    //   text    -> a text field, with the placeholder as its hint
    //
    // A kind this build has never heard of is skipped rather than guessed at.
    // The operator can still pass such a flag by hand -- see the extra-
    // arguments field -- which is what makes skipping it safe rather than a
    // dead end.
    //
    // Flags marked `query` are absent: they are how this dialog was built (see
    // cli::Query), and a "print the flags and exit" checkbox on a run dialog is
    // nonsense.
    //
    class OptionsDialog : public wxDialog
    {
        public:
            //
            // `handledElsewhere` names the flags the window presents with a
            // control of its own -- the test tree is --select, the picker is
            // --criteria, the header fields are --dut-serial and --operator,
            // the button is --safe. They are left out of this form.
            //
            // A list of flag names in the UI, which everything else here goes
            // to some length to avoid. It is a different kind of list and the
            // distinction is the whole argument: this does not say what the
            // flags *are* -- --describe-options still says that, and this
            // dialog still cannot show a flag the parser would reject. It says
            // which of them this window has chosen to surface specially, which
            // is a fact about the window and lives nowhere else.
            //
            // Without it an operator can set a flag twice, from two controls,
            // and see no sign of the contradiction: a test ticked in the tree
            // and a different one typed into a --select field produce one run,
            // and only one of the two answers is the one that happens.
            //
            // MainFrame checks every name here against the model at load time,
            // so a flag renamed in run_scripts fails loudly rather than
            // quietly reappearing in two places at once.
            //
            OptionsDialog( wxWindow * parent,
                           const std::vector<OptionInfo> & model,
                           const std::vector<std::string> & handledElsewhere,
                           const RunRequest & current);

            //
            // The settings the operator has actually touched, plus whatever
            // they typed by hand.
            //
            // "Touched" is load-bearing and is why every control is compared
            // against the state it was built in rather than simply read. See
            // framework/ui/README.md §3: main.cpp depends on telling "the caller said
            // nothing" from "the caller named the default", and a dialog that
            // emitted every control would destroy that distinction on every
            // run.
            //
            [[nodiscard]]
            auto settings() const -> RunRequest;

        private:
            //
            // One row of the form, remembering both the control and what it
            // started as.
            //
            struct Row
            {
                OptionInfo   Option;

                wxCheckBox * Check{ nullptr };
                wxSpinCtrl * Spin{ nullptr };
                wxTextCtrl * Text{ nullptr };

                bool         InitialCheck{ false };
                int          InitialSpin{ 0 };
                wxString     InitialText{};
            };

            auto addRow( wxWindow * panel, wxSizer * grid, const OptionInfo & option, const RunRequest & current) -> void;

            std::vector<Row>  mRows;
            wxTextCtrl *      mExtra{ nullptr };
    };
} // namespace ui
