#include "app/process.hpp"

#include <wx/utils.h>
#include <wx/wfstream.h>

#include <string>
#include <vector>

namespace ui
{
    namespace
    {
        //
        // How often the output pump runs. Fast enough that a reading appears
        // while the operator is still looking at the rig it came from, slow
        // enough not to be a busy loop on an idle bench.
        //
        constexpr int kPollMilliseconds = 50;
    }

    ChildProcess::ChildProcess( Handlers handlers) :
        mHandlers( std::move( handlers)),
        mTimer( this)
    {
        Bind( wxEVT_TIMER,       &ChildProcess::onTick,  this);
        Bind( wxEVT_END_PROCESS, &ChildProcess::onEnded, this);
    }

    ChildProcess::~ChildProcess()
    {
        mTimer.Stop();

        //
        // Detached rather than killed. If this object is going away while a run
        // is still live -- the window closing mid-run -- killing the child
        // would skip its safing guard and leave a powered rig. The window asks
        // first (see MainFrame::onClose) and this is only the backstop; the
        // child outliving us and safing itself is strictly better than the
        // alternative.
        //
        if( mProcess)
        {
            mProcess->Detach();
            mProcess = nullptr;
        }
    }

    auto ChildProcess::start( const std::vector<std::string> & argv) -> bool
    {
        if( running() || argv.empty())
        {
            return false;
        }

        mSawRunEnd = false;
        mStream    = EventStream{};

        mProcess = new wxProcess( this);

        mProcess->Redirect();

        //
        // The argv overload, never a joined command string.
        //
        // wxExecute's array form is the ONLY one that takes arguments already
        // separated -- the overloads that capture output take a single
        // wxString, which is a command line a shell has to re-split. This
        // program is handed paths it did not choose (an install prefix, a
        // recording an operator browsed to), and "C:/Program Files/..." or a
        // macOS volume name would come apart in exactly that re-split. That
        // constraint is why the two interrogations in MainFrame are
        // asynchronous like a run rather than using the capturing overloads:
        // there is no array form of those to use.
        //
        // The pointer array must outlive the call and be null-terminated.
        // wxExecute copies the arguments before returning, so locals are
        // enough -- but the strings have to be the ones the pointers point at,
        // which is why argv is not re-formed into temporaries here.
        //
        std::vector<const char *> pointers;

        pointers.reserve( argv.size() + 1);

        for( const auto & argument : argv)
        {
            pointers.push_back( argument.c_str());
        }

        pointers.push_back( nullptr);

        mPid = wxExecute( pointers.data(), wxEXEC_ASYNC, mProcess);

        if( mPid == 0)
        {
            delete mProcess;

            mProcess = nullptr;

            return false;
        }

        mTimer.Start( kPollMilliseconds);

        return true;
    }

    auto ChildProcess::requestStop() -> void
    {
        if( running())
        {
            wxProcess::Kill( mPid, wxSIGTERM);
        }
    }

    auto ChildProcess::onTick( wxTimerEvent &) -> void
    {
        drain();
    }

    namespace
    {
        //
        // Everything that can be read from `stream` right now, without waiting.
        //
        // Byte at a time, guarded by IsInputAvailable(), and that is NOT a
        // naive first draft -- it is the only form that does not block.
        //
        // wxInputStream::Read( buffer, size) tries to fill the whole buffer: it
        // keeps reading until it has `size` bytes, hits EOF, or errors. Against
        // a pipe that is open but idle -- which is what a run's stdout is
        // between one reading and the next -- it therefore waits, on whatever
        // thread called it. In this program that is the GUI thread, so a run
        // that paused two seconds while a supply settled would be a window that
        // froze for two seconds, and a run that paused for a soak would be a
        // window that never came back. It got as far as a test hanging
        // outright, which is the lucky version of that bug.
        //
        // IsInputAvailable() means at least one byte is ready, so a single-byte
        // read after it cannot wait. The loop drains whatever has accumulated
        // and stops the moment the pipe is empty, which is the behaviour a
        // poll-driven reader needs and the one the batching overload cannot
        // give.
        //
        auto readAvailable( wxProcess & process, const bool errorStream) -> std::string
        {
            auto * stream = errorStream ? process.GetErrorStream() : process.GetInputStream();

            if( !stream)
            {
                return {};
            }

            std::string text;

            while( errorStream ? process.IsErrorAvailable() : process.IsInputAvailable())
            {
                const auto byte = stream->GetC();

                if( byte == wxEOF)
                {
                    break;
                }

                text += static_cast<char>( byte);
            }

            return text;
        }
    } // namespace

    auto ChildProcess::drain() -> void
    {
        if( !mProcess)
        {
            return;
        }

        //
        // Both streams, every tick. stderr matters as much as stdout here: the
        // runner reports a refused flag combination, an unopenable log and a
        // failed preflight there, and every one of those is a run that never
        // started -- which the operator has to be told about in words, since
        // there will be no events at all to show them.
        //
        const auto output = readAvailable( *mProcess, false);

        if( !output.empty())
        {
            if( mHandlers.OnOutput)
            {
                mHandlers.OnOutput( output);
            }

            //
            // Only fed through the event reader when somebody is listening for
            // events. A --describe-options body is not a stream of JSON lines
            // and would have every line of it discarded as unparseable, which
            // is harmless but pointless work on every chunk.
            //
            if( mHandlers.OnEvent)
            {
                for( auto & event : mStream.consume( output))
                {
                    if( event.Which == RunEvent::Kind::RunEnd)
                    {
                        mSawRunEnd = true;
                    }

                    mHandlers.OnEvent( event);
                }
            }
        }

        const auto errors = readAvailable( *mProcess, true);

        if( !errors.empty() && mHandlers.OnStderr)
        {
            mHandlers.OnStderr( errors);
        }
    }

    auto ChildProcess::onEnded( wxProcessEvent & event) -> void
    {
        mTimer.Stop();

        //
        // One last drain before reporting the exit.
        //
        // wxEVT_END_PROCESS can arrive with bytes still buffered in the pipe,
        // and those bytes are the end of the run -- the last verdicts and the
        // runEnd itself. Reporting the exit first would show an operator a run
        // that crashed three events before it finished.
        //
        drain();

        mPid     = 0;
        mProcess = nullptr;   // wxProcess deletes itself once the event is handled

        if( mHandlers.OnEnded)
        {
            //
            // "Crashed" means a run that opened a journal and never closed it.
            // A caller that asked for no events -- --list-tests, --safe -- has
            // no runEnd to miss, so the answer for those is always false rather
            // than always true.
            //
            // Decided here rather than left to each handler, because leaving it
            // to them is exactly what went wrong: the safing handler tested
            // this flag, got "crashed" for every successful --safe, and told an
            // operator the rig could not be dropped to idle while it was being
            // dropped to idle. A flag whose meaning depends on how the call was
            // configured has to be narrowed where the configuration is known.
            //
            const bool crashed = mHandlers.OnEvent && !mSawRunEnd;

            mHandlers.OnEnded( event.GetExitCode(), crashed);
        }
    }
} // namespace ui
