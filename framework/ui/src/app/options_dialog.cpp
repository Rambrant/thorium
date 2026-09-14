#include "app/options_dialog.hpp"

#include <algorithm>
#include <ranges>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace ui
{
    namespace
    {
        //
        // What a setting the operator arrived with holds for this flag, so
        // reopening the dialog shows what it will actually run rather than a
        // blank form. Nothing when they have not set it.
        //
        auto settingFor( const RunRequest & request, const std::string & flag) -> const OptionSetting *
        {
            for( const auto & setting : request.Settings)
            {
                if( setting.Flag == flag)
                {
                    return &setting;
                }
            }

            return nullptr;
        }

        auto toWx( const std::string & text) -> wxString
        {
            return wxString::FromUTF8( text);
        }
    } // namespace

    OptionsDialog::OptionsDialog( wxWindow * parent,
                                  const std::vector<OptionInfo> & model,
                                  const std::vector<std::string> & handledElsewhere,
                                  const RunRequest & current) :
        wxDialog( parent, wxID_ANY, "Run options",
                  wxDefaultPosition, wxSize( 620, 560),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        auto * outer = new wxBoxSizer( wxVERTICAL);

        //
        // Scrolled, because the number of rows is not this file's to know. It
        // is however many flags run_scripts declares, today and after the next
        // one is added, and a dialog that needed resizing when that happened
        // would be a dialog that knew the count.
        //
        auto * scroller = new wxScrolledWindow( this, wxID_ANY);

        scroller->SetScrollRate( 0, 12);

        auto * grid = new wxFlexGridSizer( 2, wxSize( 12, 8));

        grid->AddGrowableCol( 1, 1);

        for( const auto & option : model)
        {
            //
            // The two exclusions, both of them decided by the model rather than
            // by a name: a flag that describes the binary, and a kind this
            // build cannot render.
            //
            if( option.Query || option.Kind == OptionKind::Unknown)
            {
                continue;
            }

            //
            // Checked against every spelling, not just the canonical one: a
            // window offering --no-colour as a checkbox has taken --no-color
            // too, since the parser accepts both for one member.
            //
            if( std::ranges::any_of( option.Spellings,
                                     [ &handledElsewhere]( const std::string & spelling)
                                     {
                                         return std::ranges::find( handledElsewhere, spelling) != handledElsewhere.end();
                                     }))
            {
                continue;
            }

            addRow( scroller, grid, option, current);
        }

        auto * scrollSizer = new wxBoxSizer( wxVERTICAL);

        scrollSizer->Add( grid, 1, wxEXPAND | wxALL, 12);

        scroller->SetSizer( scrollSizer);

        outer->Add( scroller, 1, wxEXPAND);

        //
        // The escape hatch. Not a fallback for a missing feature -- it is how a
        // console that shipped last year passes a flag added to the runner last
        // week, which on a bench where suites are deployed and consoles are not
        // is the ordinary case rather than the exotic one.
        //
        outer->Add( new wxStaticText( this, wxID_ANY,
                                      "Extra arguments (passed through as typed, one per line):"),
                    0, wxLEFT | wxRIGHT | wxTOP, 12);

        mExtra = new wxTextCtrl( this, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxSize( -1, 70), wxTE_MULTILINE);

        for( const auto & extra : current.Extra)
        {
            mExtra->AppendText( toWx( extra) + "\n");
        }

        outer->Add( mExtra, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

        outer->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);

        SetSizer( outer);
    }

    auto OptionsDialog::addRow( wxWindow * panel, wxSizer * grid,
                                const OptionInfo & option, const RunRequest & current) -> void
    {
        Row          row{ .Option = option };
        const auto * setting = settingFor( current, option.flag());

        //
        // The label is the flag itself, not a prettified version of it.
        //
        // An operator who has read the README, or who is being talked through a
        // problem over the phone, is thinking in flags -- and a run's own
        // traceability header records the command line, so the thing on screen
        // and the thing in the log say the same word. "Repeat count" would be
        // friendlier and would break that.
        //
        auto * label = new wxStaticText( panel, wxID_ANY, toWx( option.flag()));

        label->SetToolTip( toWx( option.Help));

        grid->Add( label, 0, wxALIGN_CENTER_VERTICAL);

        switch( option.Kind)
        {
            case OptionKind::Switch:
            {
                //
                // A clearing flag starts ticked: --no-logs turns logging off,
                // so logging is on, and the box represents the thing rather
                // than the flag. Passing the flag is then what *unticking* it
                // does -- see settings() below, where the inversion happens
                // once.
                //
                row.InitialCheck = option.Clears;
                row.Check        = new wxCheckBox( panel, wxID_ANY, toWx( option.Help));

                row.Check->SetValue( setting ? ( option.Clears ? !setting->Present : setting->Present)
                                             : row.InitialCheck);

                grid->Add( row.Check, 0, wxEXPAND);

                break;
            }

            case OptionKind::Number:
            {
                //
                // Floored at 1 for a positive flag, so the value the runner
                // refuses cannot be typed. The runner still refuses it -- this
                // is a courtesy, not the check, and the diagnostic naming the
                // Noun stays where it was.
                //
                const int minimum = option.Positive ? 1 : 0;

                row.InitialSpin = minimum;
                row.Spin        = new wxSpinCtrl( panel, wxID_ANY, wxEmptyString,
                                                  wxDefaultPosition, wxDefaultSize,
                                                  wxSP_ARROW_KEYS, minimum, 1000000, minimum);

                if( setting && !setting->Value.empty())
                {
                    row.Spin->SetValue( toWx( setting->Value));
                }

                row.Spin->SetToolTip( toWx( option.Help));

                grid->Add( row.Spin, 0);

                break;
            }

            case OptionKind::List:
            case OptionKind::Text:
            {
                row.Text = new wxTextCtrl( panel, wxID_ANY,
                                           setting ? toWx( setting->Value) : wxString{});

                //
                // The placeholder is the runner's own -- "ID[,ID...]",
                // "PATH", "NAME" -- so the hint in the field is the same word
                // --help uses for it.
                //
                if( !option.Placeholder.empty())
                {
                    row.Text->SetHint( toWx( option.Placeholder));
                }

                row.Text->SetToolTip( toWx( option.Help));

                row.InitialText = row.Text->GetValue();

                grid->Add( row.Text, 1, wxEXPAND);

                break;
            }

            case OptionKind::Unknown:
                //
                // Filtered out by the caller. Listed here so that adding a kind
                // to ui::OptionKind without deciding what it looks like is a
                // -Wswitch error rather than a control that silently never
                // appears.
                //
                break;
        }

        mRows.push_back( row);
    }

    auto OptionsDialog::settings() const -> RunRequest
    {
        RunRequest request;

        for( const auto & row : mRows)
        {
            if( row.Check)
            {
                //
                // Untouched contributes nothing, which for a switch means "the
                // box is as I built it".
                //
                if( row.Check->GetValue() == row.InitialCheck)
                {
                    continue;
                }

                //
                // The one inversion, done here and nowhere else: for a clearing
                // flag the box represents the thing and the flag turns it off,
                // so an unticked box is a flag that must be passed.
                //
                request.Settings.push_back( OptionSetting{
                    .Flag    = row.Option.flag(),
                    .Value   = {},
                    .Present = true
                });

                continue;
            }

            if( row.Spin)
            {
                if( row.Spin->GetValue() == row.InitialSpin)
                {
                    continue;
                }

                request.Settings.push_back( OptionSetting{
                    .Flag    = row.Option.flag(),
                    .Value   = std::to_string( row.Spin->GetValue()),
                    .Present = true
                });

                continue;
            }

            if( row.Text)
            {
                const auto value = row.Text->GetValue();

                if( value == row.InitialText)
                {
                    continue;
                }

                request.Settings.push_back( OptionSetting{
                    .Flag    = row.Option.flag(),
                    .Value   = value.utf8_string(),
                    .Present = true
                });
            }
        }

        //
        // One argument per line, empty lines dropped. Not split on spaces: an
        // argument can contain one (--operator=Ada Lovelace), and a field that
        // silently turned that into two would be the same defect wxExecute's
        // array form exists to avoid.
        //
        if( mExtra)
        {
            for( std::size_t line = 0; line < static_cast<std::size_t>( mExtra->GetNumberOfLines()); ++line)
            {
                const auto text = mExtra->GetLineText( static_cast<long>( line)).Trim().Trim( false);

                if( !text.IsEmpty())
                {
                    request.Extra.push_back( text.utf8_string());
                }
            }
        }

        return request;
    }
} // namespace ui
