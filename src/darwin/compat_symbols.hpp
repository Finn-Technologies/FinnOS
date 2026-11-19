// Real C/Objective-C ABI shims shared by downloaded macOS apps for symbols
// exported by Foundation, AppKit, libobjc, libdispatch, libc++ and libc.
//
// Every entry here has an actual implementation: Foundation geometry is
// computed, NSString constants are real string objects, associated objects and
// weak references have a backing store, libdispatch semaphores block and wake,
// and the libc++/unwinder entries bind to the host Itanium ABI. Symbols that
// cannot be implemented truthfully are deliberately absent so diagnose keeps
// reporting them.

#ifndef FINN_DARWIN_COMPAT_SYMBOLS_HPP
#define FINN_DARWIN_COMPAT_SYMBOLS_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Creates constant NSString objects. Safe to call more than once.
void init_compat_symbols();

// Populates the loader symbol table with the shims above.
void register_compat_symbols(std::unordered_map<std::string, void*>* table);

// Zeroes weak references to an object when the Objective-C runtime deallocates
// it. Called from finn_objc_release; declared here so the linker keeps the two
// modules independent of include order.
void compat_weak_zero(void* object);

// Releases a semaphore created by dispatch_semaphore_create. libdispatch has
// no exported destroy symbol; guests rely on ARC-style ownership, but callers
// that create a semaphore directly need a real way to free it.
void dispatch_semaphore_release(void* semaphore);

// Test/diagnostic helper: true when the named (Mach-O, underscore-prefixed)
// symbol is provided by this module.
bool compat_has_symbol(const std::string& macho_symbol);

}  // namespace finn::darwin

#endif
