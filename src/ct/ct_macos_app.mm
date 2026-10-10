/*
 * ct_macos_app.mm
 *
 * macOS application integration: route the Quit requests that reach the
 * process as Apple events (Dock > Quit, logout/shutdown, "osascript quit")
 * through CherryTree's own quit flow (save prompts) instead of letting
 * NSApplication terminate the process immediately.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#import <Cocoa/Cocoa.h>
#include "ct_macos_app.h"
#include <glib.h>
#include <gdk/gdk.h>
// the header has no extern "C" guard: in Objective-C++ the symbol would get C++ linkage
extern "C" {
#include <gdk/quartz/gdkquartz-cocoa-access.h>
}
#include <dlfcn.h>

namespace {
std::function<void()> g_onQuit;

gboolean run_quit_flow(gpointer)
{
    // on the GLib main loop, outside of the GDK event poll that delivered the Apple event
    if (g_onQuit) g_onQuit();
    return G_SOURCE_REMOVE;
}
}

// Wraps the delegate GDK may have installed: every other message is forwarded to it.
@interface CtMacAppDelegate : NSObject <NSApplicationDelegate>
{
    id _previous;
}
- (instancetype)initWithPrevious:(id)previous;
@end

@implementation CtMacAppDelegate

- (instancetype)initWithPrevious:(id)previous
{
    self = [super init];
    if (self) _previous = [previous retain];
    return self;
}

- (void)dealloc
{
    [_previous release];
    [super dealloc];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    // CherryTree decides: it asks about unsaved changes and quits by itself when allowed
    g_idle_add(run_quit_flow, nullptr);
    return NSTerminateCancel;
}

- (BOOL)respondsToSelector:(SEL)aSelector
{
    return [super respondsToSelector:aSelector] || (_previous && [_previous respondsToSelector:aSelector]);
}

- (id)forwardingTargetForSelector:(SEL)aSelector
{
    if (_previous && [_previous respondsToSelector:aSelector]) return _previous;
    return [super forwardingTargetForSelector:aSelector];
}

@end

bool ct_macos_lock_screen()
{
    // the lock screen entry point of the system login framework (what the menu bar lock uses)
    static int (*lock_now)() = nullptr;
    static bool looked_up = false;
    if (not looked_up) {
        looked_up = true;
        void* handle = dlopen("/System/Library/PrivateFrameworks/login.framework/login", RTLD_LAZY);
        if (handle) lock_now = reinterpret_cast<int(*)()>(dlsym(handle, "SACLockScreenImmediate"));
    }
    if (lock_now) {
        lock_now();
        return true;
    }
    // fallback: start the screen saver, which locks when the system is set to ask for the password
    return [[NSWorkspace sharedWorkspace] launchApplication:@"ScreenSaverEngine"];
}

bool ct_macos_forward_unhandled_key(void* gdk_event_key)
{
    // GDK translates every key press itself and never passes it to [NSApp sendEvent:], so
    // Cocoa never sees the shortcuts GTK did not consume: system ones (Control+Command+Q
    // lock screen...) and those only known to the native menu bar. Re-dispatch the original
    // NSEvent through Cocoa. Only Command combinations are forwarded: plain keys would go
    // through the text input path a second time.
    static bool forwarding = false;
    if (forwarding) return false;
    auto* pEvent = static_cast<GdkEventKey*>(gdk_event_key);
    if (not pEvent or pEvent->type != GDK_KEY_PRESS) return false;
    if (not (pEvent->state & (GDK_META_MASK | GDK_MOD2_MASK))) return false;
    NSEvent* nsevent = gdk_quartz_event_get_nsevent(reinterpret_cast<GdkEvent*>(pEvent));
    if (not nsevent or [nsevent type] != NSEventTypeKeyDown) return false;
    if (not ([nsevent modifierFlags] & NSEventModifierFlagCommand)) return false;
    forwarding = true;
    [NSApp sendEvent:nsevent];
    forwarding = false;
    return true;
}

bool ct_macos_handle_system_key(void* gdk_event_key, bool is_press)
{
    auto* pEvent = static_cast<GdkEventKey*>(gdk_event_key);
    if (not pEvent) return false;
    NSEvent* nsevent = gdk_quartz_event_get_nsevent(reinterpret_cast<GdkEvent*>(pEvent));
    if (not nsevent) return false;
    if ([nsevent type] == NSEventTypeFlagsChanged) {
        // a modifier alone: let the input method see it (Shift toggles Chinese/English in
        // many input methods); GDK's view ignores flagsChanged
        NSTextInputContext* context = [NSTextInputContext currentInputContext];
        if (context) [context handleEvent:nsevent];
        return false; // GTK still gets the modifier press/release
    }
    if (not is_press or [nsevent type] != NSEventTypeKeyDown) return false;
    const NSEventModifierFlags flags = [nsevent modifierFlags];
    if (not ((flags & NSEventModifierFlagCommand) and (flags & NSEventModifierFlagControl))) return false;
    // Control+Command combinations are system shortcuts, CherryTree binds none of them
    if (pEvent->keyval == GDK_KEY_q or pEvent->keyval == GDK_KEY_Q) {
        ct_macos_lock_screen();
        return true;
    }
    if (pEvent->keyval == GDK_KEY_f or pEvent->keyval == GDK_KEY_F) {
        return false; // full screen: handled by the window (toggle_fullscreen)
    }
    static bool forwarding = false;
    if (forwarding) return false;
    forwarding = true;
    [NSApp sendEvent:nsevent];
    forwarding = false;
    return true;
}

static CtMacAppDelegate* g_delegate = nil;

void ct_macos_install_quit_handler(std::function<void()> on_quit)
{
    g_onQuit = std::move(on_quit);
    if (g_delegate) return;
    NSApplication* app = [NSApplication sharedApplication];
    g_delegate = [[CtMacAppDelegate alloc] initWithPrevious:[app delegate]];
    [app setDelegate:g_delegate];
}
