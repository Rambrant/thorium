#include "status_item.hpp"

#include <array>
#include <memory>
#include <utility>

#include "appkit.hpp"

namespace launcher
{
    namespace
    {
        // Set by the one live StatusItem -- see status_item.hpp.
        StatusItem::Handlers *  gHandlers = nullptr;

        using Implementation = void ( *)( id, SEL, id);

        auto fire( std::function<void()> StatusItem::Handlers::* handler) -> void
        {
            if ( gHandlers != nullptr && gHandlers->*handler)
            {
                ( gHandlers->*handler)();
            }
        }

        auto showConsole( id, SEL, id) -> void { fire( &StatusItem::Handlers::OnShowConsole); }
        auto safeTheRig( id, SEL, id) -> void  { fire( &StatusItem::Handlers::OnSafeTheRig); }
        auto quit( id, SEL, id) -> void        { fire( &StatusItem::Handlers::OnQuit); }

        // The other end of StatusItem::postToMain: `boxed` is an NSValue
        // holding the std::function it heap-allocated.
        auto runPosted( id, SEL, id boxed) -> void
        {
            const std::unique_ptr<std::function<void()>>  work(
                static_cast<std::function<void()> *>( appkit::kPointerValue( boxed)));
            if ( *work)
            {
                ( *work)();
            }
        }

        // Runs, and does nothing, on a thread of its own -- see the
        // constructor on why that thread has to exist.
        auto noop( id, SEL, id) -> void {}

        struct Entry
        {
            const char *    Selector;
            Implementation  Function;
        };

        constexpr Entry  kShowConsole{ "showConsole:", &showConsole };
        constexpr Entry  kSafeTheRig{ "safeTheRig:", &safeTheRig };
        constexpr Entry  kQuit{ "quit:", &quit };
        constexpr Entry  kRunPosted{ "runPosted:", &runPosted };
        constexpr Entry  kNoop{ "noop:", &noop };

        constexpr std::array  kEntries{ kShowConsole, kSafeTheRig, kQuit, kRunPosted, kNoop };

        template <typename R, typename... Args>
        auto encodingOf( R ( *)( id, SEL, Args...)) -> std::string
        {
            return objc::methodEncoding<R, Args...>();
        }

        auto target() -> id
        {
            return reinterpret_cast<id>( menu_target::registerClass());
        }

        auto application() -> id
        {
            return appkit::kSharedApplication.onClass();
        }

        auto addItem( id menu, const char * title, const Entry & action) -> void
        {
            const auto  item = appkit::kInitMenuItem(
                appkit::kAllocMenuItem.onClass(),
                appkit::string( title),
                objc::selector( action.Selector),
                appkit::string( ""));
            appkit::kSetTarget( item, target());
            appkit::kAddItem( menu, item);
        }

        auto toStdString( id nsString) -> std::string
        {
            return nsString != nullptr ? appkit::kUtf8String( nsString) : "";
        }
    }

    namespace menu_target
    {
        auto selectors() -> std::vector<const char *>
        {
            std::vector<const char *>  result;
            for ( const auto & entry : kEntries)
            {
                result.push_back( entry.Selector);
            }
            return result;
        }

        auto implementationEncoding() -> std::string
        {
            // Every entry has the one type Implementation, so any function's
            // encoding is all of theirs.
            return encodingOf( kEntries.front().Function);
        }

        auto registerClass() -> Class
        {
            if ( const auto existing = objc_lookUpClass( kClassName))
            {
                return existing;
            }

            const auto  cls = objc_allocateClassPair( appkit::kNSObjectClass.onClass(), kClassName, 0);
            const auto  metaclass = object_getClass( reinterpret_cast<id>( cls));
            const auto  encoding = implementationEncoding();

            for ( const auto & entry : kEntries)
            {
                class_addMethod( metaclass, objc::selector( entry.Selector),
                                 reinterpret_cast<IMP>( entry.Function), encoding.c_str());
            }

            objc_registerClassPair( cls);
            return cls;
        }
    }

    StatusItem::StatusItem( const std::string & tooltip, Handlers handlers)
        : mHandlers( std::move( handlers))
    {
        gHandlers = &mHandlers;
        application();

        // Cocoa only takes the locks that make it safe to message from a
        // second thread once it has seen an NSThread start -- a std::thread
        // does not count. postToMain is called from one, so start (and
        // immediately finish) an NSThread here.
        if ( !appkit::kIsMultiThreaded.onClass())
        {
            appkit::kDetachNewThread.onClass( objc::selector( kNoop.Selector), target(), nullptr);
        }

        const auto  bar = appkit::kSystemStatusBar.onClass();
        mItem = appkit::kRetain( appkit::kStatusItemWithLength( bar, appkit::kVariableStatusItemLength));

        const auto  button = appkit::kButton( mItem);
        const auto  title = appkit::string( tooltip.c_str());

        // A template SF Symbol, so the menu bar tints it for light, dark and
        // highlighted states by itself. The text fallback is for a symbol
        // name the running macOS does not know.
        const auto  image = appkit::kSymbolImage.onClass( appkit::string( "testtube.2"), title);
        if ( image != nullptr)
        {
            appkit::kSetTemplate( image, objc::kYes);
            appkit::kSetImage( button, image);
        }
        else
        {
            appkit::kSetTitle( button, appkit::string( "Thorium"));
        }
        appkit::kSetToolTip( button, title);

        mMenu = appkit::kInit( appkit::kAllocMenu.onClass());
        addItem( mMenu, "Show console", kShowConsole);
        addItem( mMenu, "Safe the rig", kSafeTheRig);
        appkit::kAddItem( mMenu, appkit::kSeparatorItem.onClass());
        addItem( mMenu, "Quit", kQuit);
        appkit::kSetMenu( mItem, mMenu);
    }

    StatusItem::~StatusItem()
    {
        appkit::kRemoveStatusItem( appkit::kSystemStatusBar.onClass(), mItem);
        gHandlers = nullptr;
    }

    auto StatusItem::run() -> int
    {
        appkit::kRun( application());
        return 0;
    }

    auto StatusItem::stop() -> void
    {
        const auto  app = application();
        appkit::kStop( app, nullptr);

        // -stop: only takes effect once the loop finishes handling an event,
        // and a menu action runs inside the menu's own tracking loop rather
        // than the one -run is waiting in. An empty application-defined
        // event is what gets -run to look again.
        const auto  wake = appkit::kOtherEvent.onClass(
            appkit::kEventTypeApplicationDefined, objc::Point{ 0, 0 },
            0ul, 0.0, 0l, nullptr, static_cast<short>( 0), 0l, 0l);
        appkit::kPostEvent( app, wake, objc::kYes);
    }

    auto StatusItem::postToMain( std::function<void()> work) -> void
    {
        // Called from threads with no autorelease pool of their own.
        const auto  pool = appkit::kInit( appkit::kAllocPool.onClass());

        const auto  boxed = appkit::kValueWithPointer.onClass(
            new std::function<void()>( std::move( work)));
        appkit::kPerformOnMainThread( target(), objc::selector( kRunPosted.Selector), boxed, objc::kNo);

        appkit::kDrain( pool);
    }

    auto StatusItem::menuTitles() const -> std::vector<std::string>
    {
        std::vector<std::string>  titles;

        const auto  count = appkit::kNumberOfItems( mMenu);
        for ( long i = 0; i < count; ++i)
        {
            const auto  item = appkit::kItemAtIndex( mMenu, i);
            titles.push_back( appkit::kIsSeparatorItem( item) ? "-" : toStdString( appkit::kTitle( item)));
        }
        return titles;
    }

    auto StatusItem::performItem( long index) const -> void
    {
        appkit::kPerformActionAtIndex( mMenu, index);
    }

    auto StatusItem::hasImage() const -> bool
    {
        return appkit::kImage( appkit::kButton( mItem)) != nullptr;
    }

    auto StatusItem::imageIsTemplate() const -> bool
    {
        const auto  image = appkit::kImage( appkit::kButton( mItem));
        return image != nullptr && appkit::kIsTemplate( image);
    }

    auto StatusItem::toolTip() const -> std::string
    {
        return toStdString( appkit::kToolTip( appkit::kButton( mItem)));
    }
}
