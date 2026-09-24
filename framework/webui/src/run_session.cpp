#include "run_session.hpp"

#include <chrono>
#include <cstdio>

namespace webui
{
    namespace
    {
        //
        // Wraps one stderr line as a JSON object of its own kind, so it can
        // travel the same SSE stream as run_scripts's real --events=- lines
        // without being mistaken for one -- see child_stream.hpp's Handlers
        // comment. Not core::jsonEscape: this server deliberately links
        // neither core nor hal (see framework/webui/README.md), and the
        // input is one line of a diagnostic message, not the wider range of
        // text that escaper's own tests cover.
        //
        auto stderrEnvelope( const std::string & line) -> std::string
        {
            std::string  escaped;
            escaped.reserve( line.size());

            for ( const auto ch : line)
            {
                switch ( ch)
                {
                    case '"':   escaped += "\\\""; break;
                    case '\\':  escaped += "\\\\"; break;
                    default:
                        if ( static_cast<unsigned char>( ch) < 0x20)
                        {
                            char  buffer[ 8];
                            std::snprintf( buffer, sizeof( buffer), "\\u%04x", ch);
                            escaped += buffer;
                        }
                        else
                        {
                            escaped += ch;
                        }
                        break;
                }
            }

            return R"({"kind":"stderr","text":")" + escaped + "\"}";
        }
    }

    auto RunSession::start( std::vector<std::string> argv) -> bool
    {
        std::lock_guard  lock( mMutex);

        if ( mActive)
        {
            return false;
        }

        mLines.clear();
        mActive = true;

        // Replaces (and, via its destructor, joins) whatever ChildStream the
        // previous run left behind. Safe to do here rather than only once the
        // previous run's OnExit has been observed elsewhere: OnExit fires on
        // the reader thread itself, and this line runs on the caller's
        // thread, so join() below is always joining a thread other than the
        // one calling it.
        mChild = std::make_unique<ChildStream>( ChildStream::Handlers{
            .OnLine = [ this]( const std::string & line)
            {
                std::lock_guard  lineLock( mMutex);
                mLines.push_back( line);
                mCv.notify_all();
            },
            .OnStderrLine = [ this]( const std::string & line)
            {
                std::lock_guard  lineLock( mMutex);
                mLines.push_back( stderrEnvelope( line));
                mCv.notify_all();
            },
            .OnExit = [ this]( int)
            {
                std::lock_guard  exitLock( mMutex);
                mActive = false;
                mCv.notify_all();
            },
        });

        if ( !mChild->start( std::move( argv)))
        {
            mActive = false;
            mChild.reset();
            return false;
        }

        return true;
    }

    auto RunSession::active() const -> bool
    {
        std::lock_guard  lock( mMutex);
        return mActive;
    }

    auto RunSession::waitForUpdate( std::size_t from) -> Update
    {
        std::unique_lock  lock( mMutex);

        // Bounded, not unbounded: a wait that never wakes up on its own holds
        // an SSE connection open with nothing crossing the wire for as long
        // as a test campaign takes between events, which is exactly the shape
        // of connection a reverse proxy or an idle-timeout in front of this
        // server (there is none today, but see framework/webui/README.md's
        // "History" on why "today" is doing the work in that sentence) would decide is
        // dead. Waking up with nothing new lets the caller send an SSE
        // comment ping instead.
        mCv.wait_for( lock, std::chrono::seconds( 15), [ this, from]
        {
            return mLines.size() > from || !mActive;
        });

        Update  result;
        if ( from < mLines.size())
        {
            result.Lines.assign( mLines.begin() + static_cast<std::ptrdiff_t>( from), mLines.end());
        }
        result.Next = mLines.size();
        result.StillRunning = mActive;
        return result;
    }
}
