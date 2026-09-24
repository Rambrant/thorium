#pragma once

#include <string>

#include <objc/message.h>
#include <objc/runtime.h>

//
// How a GCC-built program talks to AppKit without AppKit's headers.
//
// AppKit's headers are written in Clang's block syntax, which GCC does not
// implement -- the wall that ended framework/ui. The Objective-C runtime's
// own C API (<objc/runtime.h>, <objc/message.h>) has no blocks in it, and is
// all a message send really is: look up a selector, cast objc_msgSend to the
// method's real C signature, call it. So this program sends every AppKit
// message that way, and GCC builds it like everything else.
//
// What the compiler used to check and now cannot: that the class exists
// (a misspelt one is nil, and a message to nil silently does nothing), that
// the selector exists (a misspelt one aborts with "unrecognized selector"),
// and that the cast matches the method's real signature (a wrong one is
// undefined behaviour). All three are moved to a test instead:
//
// - Every message this program sends is a Msg<> entry in appkit.hpp, and
//   nothing else calls objc_msgSend -- tests/check_raw_runtime_calls.cmake
//   fails the build's tests if anything outside this file does.
// - Every Msg<> knows its class, its selector and, from its C++ signature,
//   the Objective-C type encoding that signature implies.
// - tests/test_signatures.cpp asks the runtime for each entry's real method
//   and compares encodings. A wrong class, a wrong selector or a wrong cast
//   each fails there, naming the entry, before anything runs.
//
// No ARC: GCC has none. See status_item.hpp on why nothing here is ever
// released rather than being released carefully.
//
namespace launcher::objc
{
    //
    // NSPoint / CGPoint, for the one message that takes one
    // (appkit.hpp's kOtherEvent). Laid out and encoded exactly as the SDK
    // declares it -- two doubles -- which the signature test checks.
    //
    struct Point
    {
        double  X;
        double  Y;
    };

    //
    // Objective-C's BOOL as the ABI has it: bool on arm64, signed char on
    // x86_64. Not <objc/objc.h>'s own BOOL, which picks between the two on a
    // macro only Clang predefines (__OBJC_BOOL_IS_BOOL) -- under GCC it is
    // signed char on every architecture, which is wrong on arm64. The
    // signature test found that on its first run: every BOOL message came out
    // "c" where the runtime says "B".
    //
#if defined( __aarch64__)
    using Bool = bool;
#else
    using Bool = signed char;
#endif

    inline constexpr Bool  kYes = 1;
    inline constexpr Bool  kNo = 0;

    enum class Kind
    {
        Class,
        Instance,
    };

    //
    // The Objective-C type code for each C++ type a message may carry. No
    // primary template on purpose: a signature using a type not listed here
    // does not compile, rather than compiling to an encoding nobody checked.
    //
    // The integer mapping is the LP64 one the runtime uses on macOS: long is
    // 'q' (NSInteger), unsigned long is 'Q' (NSUInteger). BOOL is Bool above,
    // so it lands on 'B' or 'c' by architecture.
    //
    template <typename T> struct Encoding;

    template <> struct Encoding<void>          { static constexpr const char * Value = "v"; };
    template <> struct Encoding<id>            { static constexpr const char * Value = "@"; };
    template <> struct Encoding<Class>         { static constexpr const char * Value = "#"; };
    template <> struct Encoding<SEL>           { static constexpr const char * Value = ":"; };
    template <> struct Encoding<bool>          { static constexpr const char * Value = "B"; };
    template <> struct Encoding<signed char>   { static constexpr const char * Value = "c"; };
    template <> struct Encoding<short>         { static constexpr const char * Value = "s"; };
    template <> struct Encoding<long>          { static constexpr const char * Value = "q"; };
    template <> struct Encoding<unsigned long> { static constexpr const char * Value = "Q"; };
    template <> struct Encoding<double>        { static constexpr const char * Value = "d"; };
    template <> struct Encoding<const char *>  { static constexpr const char * Value = "*"; };
    template <> struct Encoding<void *>        { static constexpr const char * Value = "^v"; };
    template <> struct Encoding<Point>         { static constexpr const char * Value = "{CGPoint=dd}"; };

    //
    // The encoding a method with this C++ signature has: return type, then
    // the two hidden arguments every method takes (self, _cmd), then the
    // declared ones.
    //
    template <typename R, typename... Args>
    auto methodEncoding() -> std::string
    {
        std::string  encoding = Encoding<R>::Value;
        encoding += "@:";
        ( ( encoding += Encoding<Args>::Value), ...);
        return encoding;
    }

    //
    // The runtime's encoding with what methodEncoding() does not produce
    // taken out: the stack offsets between types ("v24@0:8@16" -> "v@:@"),
    // and the const qualifier 'r' in front of a pointer ("r*" -> "*").
    // Anything else unexpected is left in, so it shows up as a mismatch
    // rather than being normalised away.
    //
    auto normaliseEncoding( const char * runtimeEncoding) -> std::string;

    //
    // One message this program sends. Declared only in appkit.hpp.
    //
    template <typename Sig> struct Msg;

    template <typename R, typename... Args>
    struct Msg<R( Args...)>
    {
        const char *  ClassName;
        const char *  Selector;
        Kind          Receiver;

        //
        // Sends to an object. For a Kind::Instance entry the object should
        // be a ClassName (or subclass) -- the test can only check the
        // method on the class the entry names.
        //
        auto operator()( id receiver, Args... args) const -> R
        {
            using Fn = R ( *)( id, SEL, Args...);
            return reinterpret_cast<Fn>( objc_msgSend)(
                receiver, sel_registerName( Selector), args...);
        }

        //
        // Sends to the class ClassName names -- the only way this program
        // gets hold of a class object, so every class name it uses is one
        // the test has checked.
        //
        auto onClass( Args... args) const -> R
        {
            return ( *this)( reinterpret_cast<id>( objc_getClass( ClassName)), args...);
        }

        static auto expectedEncoding() -> std::string
        {
            return methodEncoding<R, Args...>();
        }
    };

    //
    // A Msg<> with its signature erased, for the test to walk.
    //
    struct MessageInfo
    {
        const char *  Name;
        const char *  ClassName;
        const char *  Selector;
        Kind          Receiver;
        std::string ( *ExpectedEncoding)();
    };

    //
    // A selector by name, for status_item.cpp's own target class -- whose
    // selectors come from its tested method table, never a literal at a
    // call site.
    //
    auto selector( const char * name) -> SEL;
}
