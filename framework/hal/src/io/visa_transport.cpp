#include "hal/io/visa_transport.hpp"

#include <array>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace hal::io
{
    namespace
    {
        //
        // ---------------------------------------------------------------------
        // The slice of the VISA ABI this file uses
        // ---------------------------------------------------------------------
        //
        // Declared by hand rather than included from visa.h, because a build
        // must not require vendor software to be installed -- see
        // hal/io/visa_transport.hpp on loaded-not-linked. The vendor's own
        // typedefs are named in the comments so each line can be checked
        // against visatype.h.
        //
        // The two that are easy to get wrong, and are wrong in opposite
        // directions from what you would guess:
        //
        //   ViSession is a 32-bit unsigned integer on every platform. It looks
        //   like a handle and is not one -- it is not widened on 64-bit, and
        //   declaring it as a pointer or a size_t corrupts the stack on the
        //   very first call.
        //
        //   ViAttrState *is* widened: 64 bits in a 64-bit VISA environment,
        //   32 in a 32-bit one. It is the one type here whose width follows the
        //   process, which is what the conditional below says.
        //
        using ViStatus  = std::int32_t;   // ViInt32
        using ViSession = std::uint32_t;  // ViUInt32, and see above
        using ViObject  = std::uint32_t;  // ViUInt32
        using ViUInt32  = std::uint32_t;
        using ViAttr    = std::uint32_t;  // ViUInt32

        using ViAttrState = std::conditional_t<sizeof( void *) == 8, std::uint64_t, std::uint32_t>;

        //
        // 32-bit Windows VISA is __stdcall; 64-bit Windows and every Unix have
        // one calling convention and need no annotation. Getting this wrong on
        // 32-bit Windows is a stack imbalance rather than a link error, which
        // is why it is spelled out rather than omitted as probably-fine.
        //
#if defined( _WIN32) && !defined( _WIN64)
    #define THORIUM_VISA_CALL __stdcall
#else
    #define THORIUM_VISA_CALL
#endif

        //
        // VI_SUCCESS is zero, *warnings are positive*, and only negative
        // values are errors. That is the single most important thing to know
        // about this ABI: a successful viRead that stopped on the termination
        // character returns VI_SUCCESS_TERM_CHAR (0x3FFF0005), which is
        // emphatically not VI_SUCCESS -- so code written as
        // `if( status != VI_SUCCESS) fail()` rejects most of its own successful
        // reads. Everything below tests `status < 0`.
        //
        constexpr ViStatus kSuccess         = 0;
        constexpr ViStatus kErrorTimeout    = static_cast<ViStatus>( 0xBFFF0015);  // VI_ERROR_TMO
        constexpr ViStatus kErrorNotFound   = static_cast<ViStatus>( 0xBFFF0011);  // VI_ERROR_RSRC_NFOUND

        constexpr ViAttr   kAttrTimeout     = 0x3FFF001A;  // VI_ATTR_TMO_VALUE, milliseconds
        constexpr ViAttr   kAttrTermChar    = 0x3FFF0018;  // VI_ATTR_TERMCHAR
        constexpr ViAttr   kAttrTermCharEn  = 0x3FFF0038;  // VI_ATTR_TERMCHAR_EN

        // VI_FIND_BUFLEN, and the size viStatusDesc's buffer must be.
        constexpr std::size_t kBufferLength = 256;

        struct VisaApi
        {
            ViStatus ( THORIUM_VISA_CALL * OpenDefaultRM)( ViSession *);
            ViStatus ( THORIUM_VISA_CALL * Open)( ViSession, const char *, ViUInt32, ViUInt32, ViSession *);
            ViStatus ( THORIUM_VISA_CALL * Close)( ViObject);
            ViStatus ( THORIUM_VISA_CALL * Write)( ViSession, const unsigned char *, ViUInt32, ViUInt32 *);
            ViStatus ( THORIUM_VISA_CALL * Read)( ViSession, unsigned char *, ViUInt32, ViUInt32 *);
            ViStatus ( THORIUM_VISA_CALL * SetAttribute)( ViObject, ViAttr, ViAttrState);
            ViStatus ( THORIUM_VISA_CALL * StatusDesc)( ViObject, ViStatus, char *);
            ViStatus ( THORIUM_VISA_CALL * FindRsrc)( ViSession, const char *, ViObject *, ViUInt32 *, char *);
            ViStatus ( THORIUM_VISA_CALL * FindNext)( ViObject, char *);

            std::string Library;
            ViSession   Manager{ 0 };
        };

        //
        // ---------------------------------------------------------------------
        // Loading it
        // ---------------------------------------------------------------------
        //

#ifdef _WIN32
        using LibraryHandle = HMODULE;

        [[nodiscard]]
        auto loadLibraryNamed( const char * name) -> LibraryHandle
        {
            return ::LoadLibraryA( name);
        }

        [[nodiscard]]
        auto symbolIn( const LibraryHandle library, const char * name) -> void *
        {
            return reinterpret_cast<void *>( ::GetProcAddress( library, name));
        }

        //
        // visa64.dll first, then visa32.dll -- and the second is not a
        // fallback to a 32-bit library, which could not be loaded into a
        // 64-bit process anyway. Windows redirects System32 per process
        // bitness, so a 64-bit process opening "visa32.dll" gets the 64-bit
        // one; both names are present on a 64-bit install and which is
        // canonical differs between Keysight's and NI's packaging. Trying both
        // costs one failed LoadLibrary.
        //
        constexpr std::array kCandidates{ "visa64.dll", "visa32.dll" };
#else
        using LibraryHandle = void *;

        [[nodiscard]]
        auto loadLibraryNamed( const char * name) -> LibraryHandle
        {
            //
            // RTLD_LOCAL rather than RTLD_GLOBAL: VISA brings a large
            // dependency tree of its own, and this process has no business
            // exporting any of it into the global symbol namespace where it
            // could satisfy some unrelated lookup.
            //
            return ::dlopen( name, RTLD_NOW | RTLD_LOCAL);
        }

        [[nodiscard]]
        auto symbolIn( const LibraryHandle library, const char * name) -> void *
        {
            return ::dlsym( library, name);
        }

        //
        // The sonames the two vendors install on Linux, plus the macOS
        // framework path for completeness. Left to the dynamic loader's own
        // search rather than absolute paths, so that a distribution's
        // ldconfig layout is respected -- except the framework, which is not on
        // any search path and has only ever lived in one place.
        //
        constexpr std::array kCandidates{
            "libvisa.so.0",
            "libvisa.so",
            "librsvisa.so",                                   // Rohde & Schwarz
            "/Library/Frameworks/VISA.framework/VISA" };      // macOS, if anyone still ships one
#endif

        //
        // The library, loaded once and kept for the life of the process.
        //
        // A function-local static rather than a global, so it is initialised on
        // first use (thread-safely, and after main() has started) rather than
        // during static initialisation -- the same reasoning as
        // hal::keysight_edu34450a::EDU34450A's lazily-opened session, and for
        // the same reason: a rig's instruments are globals, and nothing that
        // reaches for vendor software may run before main().
        //
        // Never unloaded. A VISA library holds driver handles, threads and
        // device claims, and dlclose'ing it while a session is open is a class
        // of crash nobody needs; a process that has finished with its
        // instruments is a process that is about to exit.
        //
        [[nodiscard]]
        auto visa() -> const VisaApi *
        {
            static const VisaApi * const loaded = []() -> const VisaApi *
            {
                static VisaApi api{};

                //
                // An explicit override first, for an installation in a place
                // no loader looks -- a container image, a vendor's own
                // /opt tree, or a second VISA deliberately kept out of the
                // way. Worth having: "which VISA" is a real question on a
                // bench with both Keysight's and NI's stacks installed, and an
                // environment variable is the answer an operator can give
                // without a rebuild.
                //
                std::vector<const char *> candidates;

                if( const char * override_ = std::getenv( "THORIUM_VISA_LIBRARY");
                    override_ != nullptr && *override_ != '\0')
                {
                    candidates.push_back( override_);
                }

                candidates.insert( candidates.end(), kCandidates.begin(), kCandidates.end());

                for( const char * candidate : candidates)
                {
                    const LibraryHandle library = loadLibraryNamed( candidate);

                    if( library == nullptr)
                    {
                        continue;
                    }

                    //
                    // Every entry point, or none. A library that loaded but is
                    // missing viFindRsrc is not a VISA this code can use, and
                    // discovering that at the moment a USB instrument is
                    // opened -- inside a run -- would be far worse than
                    // discovering it here.
                    //
                    const auto bind = [ library]( auto & slot, const char * name)
                    {
                        slot = reinterpret_cast<std::remove_reference_t<decltype( slot)>>(
                            symbolIn( library, name));

                        return slot != nullptr;
                    };

                    if( bind( api.OpenDefaultRM, "viOpenDefaultRM")
                     && bind( api.Open,          "viOpen")
                     && bind( api.Close,         "viClose")
                     && bind( api.Write,         "viWrite")
                     && bind( api.Read,          "viRead")
                     && bind( api.SetAttribute,  "viSetAttribute")
                     && bind( api.StatusDesc,    "viStatusDesc")
                     && bind( api.FindRsrc,      "viFindRsrc")
                     && bind( api.FindNext,      "viFindNext"))
                    {
                        //
                        // The resource manager, opened once alongside the
                        // library. Every other call needs its session, and a
                        // VISA that loads but cannot open one is not usable
                        // either -- an IO Libraries install whose service is
                        // not running reaches exactly this state.
                        //
                        if( api.OpenDefaultRM( &api.Manager) >= kSuccess)
                        {
                            api.Library = candidate;

                            return &api;
                        }
                    }

                    api = VisaApi{};
                }

                return nullptr;
            }();

            return loaded;
        }

        //
        // VISA's own words for a status code -- "VI_ERROR_RSRC_NFOUND: Insufficient
        // location information or the requested device or resource is not
        // present in the system."
        //
        // Asked of the library rather than looked up in a table here, which is
        // the whole reason this file names only two error constants. There are
        // well over a hundred, they differ between vendors at the edges, and
        // the vendor's own text is better than anything this file could write
        // -- it is what the operator will find in the vendor's documentation
        // when they search for it.
        //
        [[nodiscard]]
        auto describe( const VisaApi & api, const ViObject object, const ViStatus status) -> std::string
        {
            std::array<char, kBufferLength> text{};

            if( api.StatusDesc( object, status, text.data()) < kSuccess)
            {
                return "VISA status " + std::to_string( status);
            }

            return std::string( text.data());
        }
    } // namespace

    auto visaLibrary() -> std::optional<std::string>
    {
        if( const VisaApi * api = visa(); api != nullptr)
        {
            return api->Library;
        }

        return std::nullopt;
    }

    auto visaResourceFor( const Address & address) -> std::optional<std::string>
    {
        return std::visit( []( const auto & kind) -> std::optional<std::string>
        {
            using KindT = std::decay_t<decltype( kind)>;

            if constexpr( std::same_as<KindT, Gpib>)
            {
                std::string resource = "GPIB" + std::to_string( kind.board) + "::"
                                     + std::to_string( kind.primary);

                //
                // The secondary address is a field in the resource name and not
                // a separate concept -- GPIB0::14::3::INSTR. Omitted when the
                // instrument has none, which is the usual case; a plug-in card
                // cage that subdivides one primary address is where it appears.
                //
                if( kind.secondary)
                {
                    resource += "::" + std::to_string( *kind.secondary);
                }

                return resource + "::INSTR";
            }
            else if constexpr( std::same_as<KindT, Lan>)
            {
                //
                // ::SOCKET, not ::INSTR, and the two are different instruments
                // as far as VISA is concerned. A TCPIP...::INSTR resource is
                // VXI-11 or HiSLIP -- a protocol with a session handshake --
                // where ::SOCKET is the raw port this codebase's hal::Lan
                // means (its default port is 5025, the raw SCPI socket). Naming
                // the port explicitly rather than relying on a default keeps
                // the two from being confused.
                //
                // Note that openTransport() does not normally route LAN here at
                // all: hal::io::SocketTransport reaches the same port with no
                // vendor software and is tested against a real listener. This
                // exists for a rig that wants VISA to own every instrument, or
                // that needs HiSLIP -- which would spell the resource itself.
                //
                return "TCPIP0::" + std::string( kind.host) + "::"
                     + std::to_string( kind.port) + "::SOCKET";
            }
            else if constexpr( std::same_as<KindT, Serial>)
            {
                const std::string_view device = kind.device;

                //
                // Already a resource name: passed through untouched. The escape
                // hatch for a rig that knows its own VISA aliases, and the
                // only way a Unix device path can be expressed here at all.
                //
                if( device.starts_with( "ASRL"))
                {
                    return std::string( device);
                }

                //
                // "COM3" -> ASRL3::INSTR. Windows numbers its ports and VISA
                // numbers its ASRL resources the same way, so this one
                // translates exactly.
                //
                if( device.starts_with( "COM") || device.starts_with( "com"))
                {
                    const std::string_view digits = device.substr( 3);

                    int port = 0;

                    if( const auto [ end, error] =
                            std::from_chars( digits.data(), digits.data() + digits.size(), port);
                        error == std::errc{} && end == digits.data() + digits.size() && port > 0)
                    {
                        return "ASRL" + std::to_string( port) + "::INSTR";
                    }
                }

                //
                // Anything else -- "/dev/ttyUSB0" -- is not translatable, and
                // guessing would be worse than refusing. VISA's mapping from a
                // Unix device to an ASRL index lives in that installation's own
                // configuration, so the rig has to say which it is. See this
                // function's declaration.
                //
                return std::nullopt;
            }
            else
            {
                //
                // Usb and Simulated, for two unrelated reasons -- a USB
                // instrument is found by enumeration (openVisa below) and a
                // simulated one is not an instrument. Both spelled as nullopt
                // here and told apart by the caller, which is the only place
                // the difference matters.
                //
                static_assert( std::same_as<KindT, Usb> || std::same_as<KindT, Simulated>,
                    "visaResourceFor() has no answer for this address kind -- add one");

                return std::nullopt;
            }
        }, address);
    }

    auto usbSerialOf( const std::string_view resource) -> std::string_view
    {
        //
        // USB0::0x2A8D::0x1401::MY60012345::INSTR -- and it is field four
        // whether or not the optional interface-number field is present
        // (USB0::0x2A8D::0x1401::MY60012345::0::INSTR), which is why this
        // counts separators from the left rather than from the right.
        //
        if( !resource.starts_with( "USB"))
        {
            return {};
        }

        std::size_t start = 0;

        for( int field = 0; field < 3; ++field)
        {
            const auto separator = resource.find( "::", start);

            if( separator == std::string_view::npos)
            {
                return {};
            }

            start = separator + 2;
        }

        const auto end = resource.find( "::", start);

        return resource.substr( start, end == std::string_view::npos ? std::string_view::npos : end - start);
    }

    auto openVisa( const Address & address, const TransportOptions & options) -> std::unique_ptr<ITransport>
    {
        const VisaApi * api = visa();

        if( api == nullptr)
        {
            throw UnsupportedTransport(
                to_string( address) + ": no VISA library could be loaded, and this bus kind needs one. "
                "Install Keysight IO Libraries Suite or NI-VISA (both cover Windows and Linux), or set "
                "THORIUM_VISA_LIBRARY to the library's path if it is installed somewhere the dynamic "
                "loader does not look. A LAN instrument needs none of this -- see "
                "hal/io/socket_transport.hpp.");
        }

        if( std::holds_alternative<Simulated>( address))
        {
            throw UnsupportedTransport(
                to_string( address) + ": a Simulated address has nothing at the other end -- that is what "
                "the type means. Check for hal::Simulated before opening a session.");
        }

        std::optional<std::string> resource = visaResourceFor( address);

        //
        // The USB case: the address carries a serial number and a resource
        // needs vendor and product ids, so the instrument is found rather than
        // addressed. See visaResourceFor's declaration for why the address
        // carries the field it does.
        //
        if( const auto * usb = std::get_if<Usb>( &address); usb != nullptr)
        {
            std::array<char, kBufferLength> found{};

            ViObject list  = 0;
            ViUInt32 count = 0;

            const ViStatus search = api->FindRsrc( api->Manager, "USB?*INSTR", &list, &count, found.data());

            std::vector<std::string> enumerated;

            if( search >= kSuccess)
            {
                for( ViUInt32 index = 0; index < count; ++index)
                {
                    //
                    // viFindRsrc hands back the first match itself and
                    // viFindNext the rest, which is why this loop reads the
                    // buffer *before* advancing rather than after.
                    //
                    enumerated.emplace_back( found.data());

                    if( index + 1 < count && api->FindNext( list, found.data()) < kSuccess)
                    {
                        break;
                    }
                }

                api->Close( list);
            }

            for( const auto & candidate : enumerated)
            {
                if( usbSerialOf( candidate) == usb->serialNumber)
                {
                    resource = candidate;

                    break;
                }
            }

            if( !resource)
            {
                //
                // What VISA did see, listed. The difference between "no meter
                // is plugged in" and "the serial number in the rig table is
                // some other meter's" is the whole of the diagnosis here, and
                // it is invisible without this list -- a technician holding a
                // meter whose label reads MY60099999 can then fix the table in
                // one edit.
                //
                std::string sawInstead;

                for( const auto & candidate : enumerated)
                {
                    sawInstead += sawInstead.empty() ? " " : ", ";
                    sawInstead += usbSerialOf( candidate);
                }

                throw TransportError(
                    "no USB instrument with serial number " + std::string( usb->serialNumber)
                    + " -- " + ( enumerated.empty()
                        ? std::string( "VISA (") + api->Library + ") enumerated no USB instruments at all"
                        : "VISA enumerated" + sawInstead));
            }
        }

        if( !resource)
        {
            throw UnsupportedTransport(
                to_string( address) + ": this address cannot be turned into a VISA resource name. A serial "
                "port must be named the way VISA names it -- \"COM3\", or an \"ASRL<n>::INSTR\" resource "
                "spelled out -- since the mapping from a Unix device path to an ASRL index is part of the "
                "VISA installation's own configuration.");
        }

        return std::make_unique<VisaTransport>( *resource, options);
    }

    VisaTransport::VisaTransport( const std::string_view resource, const TransportOptions & options) :
        mResource( resource)
    {
        const VisaApi * api = visa();

        if( api == nullptr)
        {
            throw UnsupportedTransport( "no VISA library could be loaded");
        }

        ViSession session = 0;

        //
        // The third argument is the access mode (VI_NO_LOCK) and the fourth is
        // the *open* timeout, which is a different number from the I/O timeout
        // set below -- the same distinction hal::io::TransportOptions draws,
        // and one of the few places VISA and this codebase already agree.
        //
        const ViStatus opened = api->Open( api->Manager, mResource.c_str(), 0,
            static_cast<ViUInt32>( options.ConnectTimeout.count()), &session);

        if( opened < kSuccess)
        {
            const std::string reason = describe( *api, api->Manager, opened);

            if( opened == kErrorNotFound)
            {
                throw TransportError( "cannot reach VISA " + mResource + ": " + reason);
            }

            throw TransportError( "cannot open VISA " + mResource + ": " + reason);
        }

        mSession = session;

        api->SetAttribute( mSession, kAttrTimeout, static_cast<ViAttrState>( options.IoTimeout.count()));

        //
        // Stop a read at a newline as well as at the end of the message.
        //
        // Needed for a serial instrument, which has no end-of-message signal at
        // all -- without it every read waits out the full timeout. Harmless and
        // slightly useful for USB and GPIB, which do assert EOM: a read then
        // returns on whichever comes first, and for line-based SCPI those are
        // the same byte.
        //
        // Two attributes rather than one, because the character and the
        // enabling of it are separate settings, and setting only the first does
        // nothing at all.
        //
        api->SetAttribute( mSession, kAttrTermChar,   static_cast<ViAttrState>( '\n'));
        api->SetAttribute( mSession, kAttrTermCharEn, 1);
    }

    VisaTransport::~VisaTransport()
    {
        if( mSession != 0)
        {
            if( const VisaApi * api = visa(); api != nullptr)
            {
                api->Close( mSession);
            }
        }
    }

    auto VisaTransport::send( const std::string_view command) -> void
    {
        const VisaApi * api = visa();

        //
        // The newline this transport adds. Not needed by USB or GPIB, which
        // frame their messages -- USBTMC sets an EOM bit in its bulk-out header
        // and 488 asserts EOI on the last byte, and VISA does both from the
        // fact that this is one viWrite call. It *is* needed by a serial
        // instrument, whose parser has nothing else to go on.
        //
        // Sent unconditionally rather than per bus kind, because a SCPI parser
        // that has already seen an end-of-message treats a trailing newline as
        // an empty command and ignores it. One rule, no bus-dependent
        // behaviour, and it keeps ITransport::send's contract identical across
        // every implementation.
        //
        const std::string wire = std::string( command) + "\n";

        ViUInt32 written = 0;

        const ViStatus status = api->Write( mSession,
            reinterpret_cast<const unsigned char *>( wire.data()),
            static_cast<ViUInt32>( wire.size()), &written);

        if( status < kSuccess)
        {
            const std::string reason = describe( *api, mSession, status);

            if( status == kErrorTimeout)
            {
                throw TransportTimeout( "timed out sending to " + description() + ": " + std::string( command));
            }

            throw TransportError( "send to " + description() + " failed: " + reason);
        }

        if( written != wire.size())
        {
            //
            // A short write, which VISA reports as success with a smaller
            // count. Refused rather than retried: a half-sent SCPI command has
            // left the instrument's parser mid-token, and the recovery for that
            // is a device clear rather than sending the rest.
            //
            throw TransportError( "send to " + description() + " was truncated by VISA");
        }
    }

    auto VisaTransport::receive() -> std::string
    {
        const VisaApi * api = visa();

        //
        // Line reassembly, identical in shape to the socket transport's and
        // needed for the same reason even though the mechanism differs: a read
        // is bounded by a buffer, so a reply longer than one can arrive in
        // pieces, and a read that stopped on end-of-message can carry more
        // than one line when an instrument answered a compound query. See
        // hal::io::ITransport::receive on why the remainder is buffered rather
        // than handed over.
        //
        for( ;;)
        {
            if( const auto newline = mPending.find( '\n'); newline != std::string::npos)
            {
                std::string line = mPending.substr( 0, newline);

                mPending.erase( 0, newline + 1);

                if( !line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                return line;
            }

            std::array<unsigned char, 1024> buffer{};

            ViUInt32 read = 0;

            const ViStatus status = api->Read( mSession, buffer.data(),
                static_cast<ViUInt32>( buffer.size()), &read);

            //
            // Positive is a warning and a success -- VI_SUCCESS_TERM_CHAR when
            // the read stopped on the newline, VI_SUCCESS_MAX_CNT when it
            // filled the buffer. Both mean bytes arrived. See kSuccess above:
            // this is the trap in VISA's ABI.
            //
            if( status >= kSuccess)
            {
                if( read == 0)
                {
                    //
                    // Success with nothing in it, which should not happen and
                    // would spin this loop forever if it did. Reported rather
                    // than retried.
                    //
                    throw TransportError( description() + " returned an empty read");
                }

                mPending.append( reinterpret_cast<const char *>( buffer.data()), read);

                continue;
            }

            //
            // A timeout can still have delivered bytes -- VISA sets the count
            // even on the error path -- and those bytes are part of the reply.
            // Kept rather than discarded, so that a reply which needed one more
            // read than the timeout allowed is diagnosable from the log rather
            // than vanishing.
            //
            if( read > 0)
            {
                mPending.append( reinterpret_cast<const char *>( buffer.data()), read);
            }

            const std::string reason = describe( *api, mSession, status);

            if( status == kErrorTimeout)
            {
                throw TransportTimeout( "timed out waiting for a reply from " + description());
            }

            throw TransportError( "receive from " + description() + " failed: " + reason);
        }
    }

    auto VisaTransport::description() const -> std::string
    {
        return "Visa " + mResource;
    }
} // namespace hal::io
