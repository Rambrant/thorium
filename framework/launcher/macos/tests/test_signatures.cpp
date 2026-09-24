//
// Checks every Objective-C message the launcher sends against the running
// system's own idea of that method -- the test objc.hpp promises in place of
// the compiler checks GCC cannot make. Headless: it asks the runtime about
// classes and methods, and never creates a window or a menu bar item, so it
// runs anywhere the launcher could be built.
//
// For each appkit.hpp entry: the class exists, the method exists on it (as a
// class or an instance method, whichever the entry says), and the method's
// type encoding is the one the entry's C++ signature implies. The same for
// the launcher's own runtime-built class, against the signature its callers
// use.
//
// And, first, that the checker can fail at all: a deliberately wrong class,
// selector and signature must each be rejected, or a checker that passed
// everything would be indistinguishable from one that found nothing wrong.
//

#include <cstdio>
#include <optional>
#include <string>

#include "appkit.hpp"
#include "check.hpp"
#include "status_item.hpp"

namespace
{
    using launcher::objc::Kind;
    using launcher::objc::MessageInfo;

    //
    // Empty if the entry matches the runtime; otherwise why not.
    //
    auto checkMessage( const MessageInfo & message) -> std::optional<std::string>
    {
        const auto  cls = objc_lookUpClass( message.ClassName);
        if ( cls == nullptr)
        {
            return std::string( "no class named ") + message.ClassName;
        }

        const auto  sel = launcher::objc::selector( message.Selector);
        const auto  method = message.Receiver == Kind::Class
                           ? class_getClassMethod( cls, sel)
                           : class_getInstanceMethod( cls, sel);
        if ( method == nullptr)
        {
            return std::string( message.ClassName) + " has no "
                 + ( message.Receiver == Kind::Class ? "class" : "instance")
                 + " method " + message.Selector;
        }

        const auto  actual = launcher::objc::normaliseEncoding( method_getTypeEncoding( method));
        const auto  expected = message.ExpectedEncoding();
        if ( actual != expected)
        {
            return std::string( "signature mismatch: the C++ side says ") + expected
                 + ", the runtime says " + actual
                 + " (raw: " + method_getTypeEncoding( method) + ")";
        }
        return std::nullopt;
    }

    auto checkerCanFail() -> void
    {
        using launcher::objc::Msg;

        // Misspelt class: the silent one, where every message would go to nil.
        constexpr Msg<id()>  wrongClass{ "NSStatusBarr", "systemStatusBar", Kind::Class };
        // Misspelt selector: the one that aborts at runtime.
        constexpr Msg<id()>  wrongSelector{ "NSStatusBar", "systemStatusbar", Kind::Class };
        // Right method, wrong cast: CGFloat passed as NSInteger.
        constexpr Msg<id( long)>  wrongSignature{ "NSStatusBar", "statusItemWithLength:", Kind::Instance };
        // Right method, looked up as the wrong kind.
        constexpr Msg<id()>  wrongKind{ "NSStatusBar", "systemStatusBar", Kind::Instance };

        const MessageInfo  bad[] = {
            { "wrongClass", wrongClass.ClassName, wrongClass.Selector, wrongClass.Receiver, &Msg<id()>::expectedEncoding },
            { "wrongSelector", wrongSelector.ClassName, wrongSelector.Selector, wrongSelector.Receiver, &Msg<id()>::expectedEncoding },
            { "wrongSignature", wrongSignature.ClassName, wrongSignature.Selector, wrongSignature.Receiver, &Msg<id( long)>::expectedEncoding },
            { "wrongKind", wrongKind.ClassName, wrongKind.Selector, wrongKind.Receiver, &Msg<id()>::expectedEncoding },
        };

        for ( const auto & message : bad)
        {
            const auto  problem = checkMessage( message);
            if ( !problem)
            {
                std::fprintf( stderr, "checker accepted the deliberately wrong entry %s\n", message.Name);
            }
            LAUNCHER_CHECK( problem.has_value());
        }
    }

    auto everyAppKitMessage() -> void
    {
        for ( const auto & message : launcher::appkit::kAllMessages)
        {
            if ( const auto problem = checkMessage( message))
            {
                std::fprintf( stderr, "appkit.hpp %s: %s\n", message.Name, problem->c_str());
                ++launcher::tests::gFailures;
            }
        }
        std::printf( "%zu AppKit messages checked\n", launcher::appkit::kAllMessages.size());
    }

    auto theMenuTargetClass() -> void
    {
        const auto  cls = launcher::menu_target::registerClass();
        LAUNCHER_CHECK( cls != nullptr);
        if ( cls == nullptr)
        {
            return;
        }

        // What every caller of these methods sends: NSMenuItem's action,
        // performSelectorOnMainThread:withObject: and
        // detachNewThreadSelector:toTarget:withObject: all call
        // `void (id self, SEL _cmd, id argument)`. Written out here rather
        // than derived, because it is the callers' fixed expectation that
        // the implementations are being held to.
        const std::string  callersSend = "v@:@";

        LAUNCHER_CHECK( launcher::menu_target::implementationEncoding() == callersSend);

        for ( const auto * selector : launcher::menu_target::selectors())
        {
            const auto  method = class_getClassMethod( cls, launcher::objc::selector( selector));
            if ( method == nullptr)
            {
                std::fprintf( stderr, "%s has no class method %s\n",
                              launcher::menu_target::kClassName, selector);
                ++launcher::tests::gFailures;
                continue;
            }

            const auto  actual = launcher::objc::normaliseEncoding( method_getTypeEncoding( method));
            if ( actual != callersSend)
            {
                std::fprintf( stderr, "%s %s is registered as %s, callers send %s\n",
                              launcher::menu_target::kClassName, selector,
                              actual.c_str(), callersSend.c_str());
                ++launcher::tests::gFailures;
            }
        }
    }

    auto normalisation() -> void
    {
        using launcher::objc::normaliseEncoding;

        LAUNCHER_CHECK( normaliseEncoding( "v24@0:8@16") == "v@:@");
        LAUNCHER_CHECK( normaliseEncoding( "r*16@0:8") == "*@:");
        LAUNCHER_CHECK( normaliseEncoding( "@24@0:8r^v16") == "@@:^v");
        LAUNCHER_CHECK( normaliseEncoding( "v32@0:8{CGPoint=dd}16") == "v@:{CGPoint=dd}");
    }
}

auto runSignatureTests() -> int
{
    static_assert( sizeof( launcher::objc::Point) == 2 * sizeof( double));

    normalisation();
    checkerCanFail();
    everyAppKitMessage();
    theMenuTargetClass();

    return launcher::tests::result();
}
