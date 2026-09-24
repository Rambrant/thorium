#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include <httplib.h>

#include "protocol/command.hpp"
#include "protocol/json.hpp"
#include "protocol/suite.hpp"

#include "child_stream.hpp"
#include "run_session.hpp"
#include "static_content.hpp"

namespace
{
    struct Config
    {
        std::string  RunScripts;
        int          Port{ 8420 };
    };

    auto startsWith( const std::string & value, const char * prefix) -> bool
    {
        return value.rfind( prefix, 0) == 0;
    }

    auto parseArgs( int argc, char ** argv, std::string & error) -> std::optional<Config>
    {
        Config  config;

        for ( int i = 1; i < argc; ++i)
        {
            const std::string  arg = argv[ i];

            if ( startsWith( arg, "--run-scripts="))
            {
                config.RunScripts = arg.substr( 14);
            }
            else if ( startsWith( arg, "--port="))
            {
                config.Port = std::atoi( arg.c_str() + 7);
            }
            else
            {
                error = "Unrecognised argument: " + arg;
                return std::nullopt;
            }
        }

        if ( config.RunScripts.empty())
        {
            error = "--run-scripts=<path to run_scripts> is required";
            return std::nullopt;
        }

        return config;
    }

    //
    // The POST /api/run body, in the shape webui::RunRequest already
    // defines (protocol/command.hpp) -- the browser sends exactly what the
    // old wxWidgets form used to build in memory, as JSON instead of as C++
    // objects. Nothing here re-decides what a setting means; it decodes the
    // same three arrays buildRunCommand already knows how to turn into argv.
    //
    auto buildRunRequest( const webui::Json & body) -> webui::RunRequest
    {
        webui::RunRequest  request;

        if ( const auto * selection = body.at( "selection"))
        {
            for ( const auto & item : selection->items())
            {
                request.Selection.push_back( item->text());
            }
        }

        if ( const auto * settings = body.at( "settings"))
        {
            for ( const auto & item : settings->items())
            {
                webui::OptionSetting  setting;
                setting.Flag = item->textAt( "flag");
                setting.Value = item->textAt( "value");
                setting.Present = item->boolAt( "present", true);
                request.Settings.push_back( setting);
            }
        }

        if ( const auto * extra = body.at( "extra"))
        {
            for ( const auto & item : extra->items())
            {
                request.Extra.push_back( item->text());
            }
        }

        return request;
    }

    auto sseFrame( const std::string & line) -> std::string
    {
        // A run_scripts event line is already one JSON object -- see
        // core/journal/event_sink.hpp -- and SSE's own framing asks for
        // nothing more than "data: " before it and a blank line after.
        // Nothing here parses or re-encodes the payload.
        return "data: " + line + "\n\n";
    }
}

auto main( int argc, char ** argv) -> int
{
    std::string  error;
    const auto   config = parseArgs( argc, argv, error);
    if ( !config)
    {
        std::fprintf( stderr, "%s\n", error.c_str());
        return 1;
    }

    webui::Suite  suite;
    suite.Binary = config->RunScripts;

    webui::RunSession  session;
    httplib::Server      server;

    // The loopback-only access seam README.md's "History" section calls for
    // before any routable bind address: reject anything whose Host header
    // does not name this exact loopback address and port. Not a defence
    // against a determined local user -- nothing on a shared machine is --
    // but it is the entire difference between "reachable from this PC" and
    // "reachable from the network" today, and the one seam a real auth check
    // slots into later without touching every handler below.
    const auto  expectedHost1 = "127.0.0.1:" + std::to_string( config->Port);
    const auto  expectedHost2 = "localhost:" + std::to_string( config->Port);

    server.set_pre_routing_handler(
        [ &expectedHost1, &expectedHost2]( const httplib::Request & req, httplib::Response & res)
        {
            const auto  host = req.get_header_value( "Host");
            if ( host != expectedHost1 && host != expectedHost2)
            {
                res.status = 403;
                res.set_content( "forbidden\n", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    server.Get( "/", []( const httplib::Request &, httplib::Response & res)
    {
        res.set_content( webui::kIndexHtml, "text/html");
    });

    server.Get( "/api/options", [ &suite]( const httplib::Request &, httplib::Response & res)
    {
        const auto  result = webui::runBlocking( webui::buildDescribeCommand( suite));
        res.status = result.Started ? 200 : 500;
        res.set_content( result.Output, "application/json");
    });

    server.Get( "/api/tests", [ &suite]( const httplib::Request &, httplib::Response & res)
    {
        const auto  result = webui::runBlocking( webui::buildListTestsCommand( suite));
        res.status = result.Started ? 200 : 500;
        res.set_content( result.Output, "text/plain");
    });

    // Never gated on session.active(): the whole point of this endpoint,
    // per README.md's "What a run means", is that it is never unavailable,
    // including while a run is in progress and including after one has died.
    // framework/launcher's tray icon calls this exact path -- see
    // framework/launcher/src/rig_client.hpp -- so its spelling ("/safe", not
    // "/api/safe") is fixed by what has already shipped there.
    server.Post( "/safe", [ &suite]( const httplib::Request &, httplib::Response & res)
    {
        const auto  result = webui::runBlocking( webui::buildSafeCommand( suite));
        res.status = result.Started ? 200 : 500;
        res.set_content( result.Started ? R"({"ok":true})" : R"({"ok":false})", "application/json");
    });

    server.Post( "/api/run", [ &suite, &session]( const httplib::Request & req, httplib::Response & res)
    {
        const auto      body = webui::Json::parse( req.body);
        webui::RunRequest  request = body ? buildRunRequest( *body) : webui::RunRequest{};

        if ( !session.start( webui::buildRunCommand( suite, request)))
        {
            res.status = 409;
            res.set_content( R"({"error":"a run is already active"})", "application/json");
            return;
        }

        res.set_content( R"({"ok":true})", "application/json");
    });

    server.Get( "/api/events", [ &session]( const httplib::Request &, httplib::Response & res)
    {
        res.set_header( "Cache-Control", "no-cache");
        res.set_chunked_content_provider( "text/event-stream",
            [ &session, from = std::size_t{ 0 }]( std::size_t, httplib::DataSink & sink) mutable -> bool
            {
                const auto  update = session.waitForUpdate( from);
                from = update.Next;

                if ( update.Lines.empty())
                {
                    if ( !sink.write( ": keep-alive\n\n", 14))
                    {
                        return false;
                    }
                }
                else
                {
                    for ( const auto & line : update.Lines)
                    {
                        const auto  frame = sseFrame( line);
                        if ( !sink.write( frame.data(), frame.size()))
                        {
                            return false;
                        }
                    }
                }

                if ( !update.StillRunning)
                {
                    sink.done();
                    return false;
                }
                return true;
            });
    });

    std::printf( "thorium_webui listening on 127.0.0.1:%d (run_scripts: %s)\n",
        config->Port, suite.Binary.string().c_str());
    std::fflush( stdout);

    server.listen( "127.0.0.1", config->Port);
    return 0;
}
