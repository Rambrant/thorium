#pragma once

#include <functional>
#include <string>
#include <vector>

#include <objc/runtime.h>

namespace launcher
{
    //
    // The macOS counterpart of the Windows launcher's TrayIcon: an
    // NSStatusItem in the menu bar with the same three actions. Like
    // TrayIcon it owns the event loop -- here NSApplication's -- since the
    // status item's menu needs one running to do anything.
    //
    // Driven through the Objective-C runtime rather than AppKit's headers --
    // see objc.hpp. Nothing it creates is ever released: there is no ARC
    // under GCC, the handful of objects here (the item, its menu and the menu's
    // entries) are all needed until the process exits, and an object never
    // released can never be released once too often. The
    // destructor takes the item out of the menu bar and leaves the rest to
    // process exit.
    //
    // One StatusItem at a time: the menu target finds its handlers through
    // a pointer this object sets, the same way TrayIcon's window procedure
    // finds its TrayIcon.
    //
    class StatusItem
    {
        public:
            struct Handlers
            {
                // Braced, for designated initialisers -- the same pattern as
                // the Windows TrayIcon::Handlers.
                std::function<void()>  OnShowConsole{};
                std::function<void()>  OnSafeTheRig{};
                std::function<void()>  OnQuit{};
            };

            StatusItem( const std::string & tooltip, Handlers handlers);
            ~StatusItem();

            StatusItem( const StatusItem &) = delete;
            auto operator=( const StatusItem &) -> StatusItem & = delete;

            //
            // Runs NSApplication's event loop until stop() is called.
            //
            auto run() -> int;

            //
            // Makes run() return. Not -terminate:, which exits the process
            // from inside the loop and skips main's destructors -- the
            // process group's watchdog would still clean up after that (see
            // process_group.hpp), but a clean quit should not need to lean on
            // the crash path. Main thread only.
            //
            static auto stop() -> void;

            //
            // Runs `work` on the main thread, from any thread -- the only
            // thread AppKit may be touched from. main.cpp's "Safe the rig"
            // posts its alert through this from the thread that made the
            // request.
            //
            static auto postToMain( std::function<void()> work) -> void;

            // --- For tests/test_menu.cpp ---------------------------------

            [[nodiscard]]
            auto menuTitles() const -> std::vector<std::string>;

            //
            // Fires menu item `index` the way a click would: through the
            // item's own target and action, so a mis-wired item fails here
            // rather than only under a real mouse.
            //
            auto performItem( long index) const -> void;

            [[nodiscard]] auto hasImage() const -> bool;
            [[nodiscard]] auto imageIsTemplate() const -> bool;
            [[nodiscard]] auto toolTip() const -> std::string;

        private:
            Handlers  mHandlers;
            id        mItem{ nullptr };
            id        mMenu{ nullptr };
    };

    //
    // The class the menu items (and postToMain) call back into, created at
    // runtime with objc_allocateClassPair since there is no @implementation
    // to compile. Exposed for tests/test_signatures.cpp, which checks that
    // it exists once registered and that each of its methods has the
    // signature its caller will use.
    //
    namespace menu_target
    {
        inline constexpr const char *  kClassName = "ThoriumMenuTarget";

        //
        // The selectors of every method the class has. All are *class*
        // methods -- the class object itself is the menu items' target, so
        // there is no instance to allocate -- and all are `void (id sender)`,
        // "v@:@", the one signature NSMenuItem's action,
        // performSelectorOnMainThread: and detachNewThreadSelector: all call
        // with.
        //
        auto selectors() -> std::vector<const char *>;

        //
        // The encoding its methods were registered with, derived from the
        // C++ type of the functions that implement them -- so the test is
        // comparing the real functions against the runtime, not a string
        // against a string.
        //
        auto implementationEncoding() -> std::string;

        // Idempotent. StatusItem calls it; tests call it to have something to check.
        auto registerClass() -> Class;
    }
}
