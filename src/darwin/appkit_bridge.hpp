// Clean-room AppKit + CoreGraphics bridge for finn-darwin.
// When compiled with FINN_DARWIN_QT6 (on FinnOS Linux), backs NSApplication,
// NSWindow, NSView, NSButton, NSTextField, NSTextView, NSMenu, NSAlert, and
// NSTimer directly with native Qt6/Wayland widgets. When compiled headless,
// registers the AppKit classes honestly and reports GUI unavailable on run().

#ifndef FINN_DARWIN_APPKIT_BRIDGE_HPP
#define FINN_DARWIN_APPKIT_BRIDGE_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Returns true when this binary was built with the live Qt6/Wayland AppKit backend.
bool appkit_qt_backend_available();

// Registers AppKit & CoreGraphics Objective-C classes and C entrypoints.
void init_appkit(int argc, char** argv);
void register_appkit_symbols(std::unordered_map<std::string, void*>* table);

}  // namespace finn::darwin

#endif
