#pragma once

#include <array>

#include "objc.hpp"

//
// Every Objective-C message the macOS launcher sends, in one list -- see
// objc.hpp for why that list is the whole of the "fragile code" and what
// tests it.
//
// Written once, as an X-macro, so that the named constants the code calls
// and the kAllMessages table the signature test walks cannot drift apart: an
// entry added here is automatically an entry tested. Each row is
//
//     X( name, class, selector, Class|Instance, C++ signature)
//
// with the C++ signature last, since it is the one part that contains
// commas. The signatures are the SDK's own, with types spelt as objc.hpp's
// Encoding<> knows them: NSInteger is long, NSUInteger (and the NS_ENUMs
// declared over it) is unsigned long, CGFloat is double, NSPoint is Point.
//
// Instance methods are listed against the class that declares them, or any
// subclass -- class_getInstanceMethod searches superclasses, so the test
// finds an inherited method the same way a message send does.
//
#define THORIUM_APPKIT_MESSAGES( X) \
    /* Autorelease pools -- see main.cpp and status_item.cpp */ \
    X( kAllocPool,              "NSAutoreleasePool", "alloc",                                   Class,    id()) \
    X( kInit,                   "NSObject",          "init",                                    Instance, id()) \
    X( kDrain,                  "NSAutoreleasePool", "drain",                                   Instance, void()) \
    X( kRetain,                 "NSObject",          "retain",                                  Instance, id()) \
    X( kNSObjectClass,          "NSObject",          "class",                                   Class,    Class()) \
    \
    /* Strings */ \
    X( kStringWithUtf8,         "NSString",          "stringWithUTF8String:",                   Class,    id( const char *)) \
    X( kFileSystemRepresentation, "NSString",        "fileSystemRepresentation",                Instance, const char *()) \
    X( kUtf8String,             "NSString",          "UTF8String",                              Instance, const char *()) \
    \
    /* The application and its event loop */ \
    X( kSharedApplication,      "NSApplication",     "sharedApplication",                       Class,    id()) \
    X( kSetActivationPolicy,    "NSApplication",     "setActivationPolicy:",                    Instance, objc::Bool( long)) \
    X( kActivate,               "NSApplication",     "activate",                                Instance, void()) \
    X( kRun,                    "NSApplication",     "run",                                     Instance, void()) \
    X( kStop,                   "NSApplication",     "stop:",                                   Instance, void( id)) \
    X( kPostEvent,              "NSApplication",     "postEvent:atStart:",                      Instance, void( id, objc::Bool)) \
    X( kOtherEvent,             "NSEvent",           "otherEventWithType:location:modifierFlags:timestamp:windowNumber:context:subtype:data1:data2:", \
                                                                                                Class,    id( unsigned long, objc::Point, unsigned long, double, long, id, short, long, long)) \
    \
    /* Threads -- see StatusItem::postToMain */ \
    X( kIsMultiThreaded,        "NSThread",          "isMultiThreaded",                         Class,    objc::Bool()) \
    X( kDetachNewThread,        "NSThread",          "detachNewThreadSelector:toTarget:withObject:", Class, void( SEL, id, id)) \
    X( kPerformOnMainThread,    "NSObject",          "performSelectorOnMainThread:withObject:waitUntilDone:", Instance, void( SEL, id, objc::Bool)) \
    X( kValueWithPointer,       "NSValue",           "valueWithPointer:",                       Class,    id( void *)) \
    X( kPointerValue,           "NSValue",           "pointerValue",                            Instance, void *()) \
    \
    /* The menu bar item */ \
    X( kSystemStatusBar,        "NSStatusBar",       "systemStatusBar",                         Class,    id()) \
    X( kStatusItemWithLength,   "NSStatusBar",       "statusItemWithLength:",                   Instance, id( double)) \
    X( kRemoveStatusItem,       "NSStatusBar",       "removeStatusItem:",                       Instance, void( id)) \
    X( kButton,                 "NSStatusItem",      "button",                                  Instance, id()) \
    X( kSetMenu,                "NSStatusItem",      "setMenu:",                                Instance, void( id)) \
    X( kMenu,                   "NSStatusItem",      "menu",                                    Instance, id()) \
    X( kSetImage,               "NSStatusBarButton", "setImage:",                               Instance, void( id)) \
    X( kImage,                  "NSStatusBarButton", "image",                                   Instance, id()) \
    X( kSetTitle,               "NSStatusBarButton", "setTitle:",                               Instance, void( id)) \
    X( kSetToolTip,             "NSStatusBarButton", "setToolTip:",                             Instance, void( id)) \
    X( kToolTip,                "NSStatusBarButton", "toolTip",                                 Instance, id()) \
    X( kSymbolImage,            "NSImage",           "imageWithSystemSymbolName:accessibilityDescription:", Class, id( id, id)) \
    X( kSetTemplate,            "NSImage",           "setTemplate:",                            Instance, void( objc::Bool)) \
    X( kIsTemplate,             "NSImage",           "isTemplate",                              Instance, objc::Bool()) \
    \
    /* Its menu */ \
    X( kAllocMenu,              "NSMenu",            "alloc",                                   Class,    id()) \
    X( kAddItem,                "NSMenu",            "addItem:",                                Instance, void( id)) \
    X( kNumberOfItems,          "NSMenu",            "numberOfItems",                           Instance, long()) \
    X( kItemAtIndex,            "NSMenu",            "itemAtIndex:",                            Instance, id( long)) \
    X( kPerformActionAtIndex,   "NSMenu",            "performActionForItemAtIndex:",            Instance, void( long)) \
    X( kAllocMenuItem,          "NSMenuItem",        "alloc",                                   Class,    id()) \
    X( kInitMenuItem,           "NSMenuItem",        "initWithTitle:action:keyEquivalent:",     Instance, id( id, SEL, id)) \
    X( kSeparatorItem,          "NSMenuItem",        "separatorItem",                           Class,    id()) \
    X( kSetTarget,              "NSMenuItem",        "setTarget:",                              Instance, void( id)) \
    X( kTarget,                 "NSMenuItem",        "target",                                  Instance, id()) \
    X( kAction,                 "NSMenuItem",        "action",                                  Instance, SEL()) \
    X( kTitle,                  "NSMenuItem",        "title",                                   Instance, id()) \
    X( kIsSeparatorItem,        "NSMenuItem",        "isSeparatorItem",                         Instance, objc::Bool()) \
    \
    /* Alerts -- main.cpp */ \
    X( kAllocAlert,             "NSAlert",           "alloc",                                   Class,    id()) \
    X( kSetAlertStyle,          "NSAlert",           "setAlertStyle:",                          Instance, void( unsigned long)) \
    X( kSetMessageText,         "NSAlert",           "setMessageText:",                         Instance, void( id)) \
    X( kSetInformativeText,     "NSAlert",           "setInformativeText:",                     Instance, void( id)) \
    X( kRunModal,               "NSAlert",           "runModal",                                Instance, long()) \
    \
    /* Finding Chrome -- browser_launch.cpp */ \
    X( kSharedWorkspace,        "NSWorkspace",       "sharedWorkspace",                         Class,    id()) \
    X( kUrlForBundleId,         "NSWorkspace",       "URLForApplicationWithBundleIdentifier:",  Instance, id( id)) \
    X( kBundleWithUrl,          "NSBundle",          "bundleWithURL:",                          Class,    id( id)) \
    X( kExecutablePath,         "NSBundle",          "executablePath",                          Instance, id())

namespace launcher::appkit
{
    // Values of the SDK's own constants, which live in the headers GCC
    // cannot read. Each is a fixed part of AppKit's ABI.
    inline constexpr long           kActivationPolicyAccessory  = 1;    // NSApplicationActivationPolicyAccessory
    inline constexpr unsigned long  kEventTypeApplicationDefined = 15;  // NSEventTypeApplicationDefined
    inline constexpr double         kVariableStatusItemLength  = -1.0;  // NSVariableStatusItemLength
    inline constexpr unsigned long  kAlertStyleCritical        = 2;     // NSAlertStyleCritical

#define THORIUM_APPKIT_DECLARE( name, cls, sel, kind, ...) \
    inline constexpr objc::Msg<__VA_ARGS__>  name{ cls, sel, objc::Kind::kind };

    THORIUM_APPKIT_MESSAGES( THORIUM_APPKIT_DECLARE)

#undef THORIUM_APPKIT_DECLARE

#define THORIUM_APPKIT_INFO( name, cls, sel, kind, ...) \
    objc::MessageInfo{ #name, cls, sel, objc::Kind::kind, &objc::Msg<__VA_ARGS__>::expectedEncoding },

    inline const auto  kAllMessages = std::to_array<objc::MessageInfo>( {
        THORIUM_APPKIT_MESSAGES( THORIUM_APPKIT_INFO)
    });

#undef THORIUM_APPKIT_INFO

    //
    // An NSString from UTF-8, autoreleased -- the one conversion every
    // caller needs.
    //
    inline auto string( const char * utf8) -> id
    {
        return kStringWithUtf8.onClass( utf8);
    }
}
