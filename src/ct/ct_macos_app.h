/*
 * ct_macos_app.h
 *
 * macOS application integration (see ct_macos_app.mm), only built on macOS.
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

#pragma once

#include <functional>

/// Make the Quit Apple event (Dock > Quit, logout, shutdown) call on_quit on the GLib
/// main loop instead of terminating the process; call once after GTK is initialised.
void ct_macos_install_quit_handler(std::function<void()> on_quit);

/// Lock the screen like the system Control+Command+Q shortcut (GTK keeps that key event
/// for itself, so the application triggers the lock); returns false when not possible.
bool ct_macos_lock_screen();

/// Hand a key press that GTK did not use back to Cocoa (menu key equivalents of the native
/// menu bar, system shortcuts such as the lock screen); call it from an "after" key press
/// handler of the toplevel. Returns true when the event was forwarded.
bool ct_macos_forward_unhandled_key(void* gdk_event_key);

/// Called before GTK sees a key press / release. Modifier-only events (Shift, Caps Lock...)
/// are passed to the active input method, which GDK never does: input methods that toggle
/// Chinese/English with Shift need them. Returns true when a Control+Command system
/// shortcut was handled here (lock screen) or forwarded to Cocoa, so GTK must not see it.
bool ct_macos_handle_system_key(void* gdk_event_key, bool is_press);
