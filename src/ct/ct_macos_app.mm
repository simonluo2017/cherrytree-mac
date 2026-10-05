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

static CtMacAppDelegate* g_delegate = nil;

void ct_macos_install_quit_handler(std::function<void()> on_quit)
{
    g_onQuit = std::move(on_quit);
    if (g_delegate) return;
    NSApplication* app = [NSApplication sharedApplication];
    g_delegate = [[CtMacAppDelegate alloc] initWithPrevious:[app delegate]];
    [app setDelegate:g_delegate];
}
