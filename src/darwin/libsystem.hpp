// Clean-room libSystem.B.dylib / libc / libdispatch / Mach / POSIX ABI shim
// translating Darwin syscall flags, errno values, and struct layouts onto Linux.

#ifndef FINN_DARWIN_LIBSYSTEM_HPP
#define FINN_DARWIN_LIBSYSTEM_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Sets argc/argv/executable path exposed via _NSGetArgc / _NSGetArgv / _NSGetExecutablePath.
void set_darwin_process_args(int argc, char** argv, const std::string& executable_path);

// Populates the loader symbol table with libSystem.B.dylib, libc, libm, and libdispatch symbols.
void register_libsystem_symbols(std::unordered_map<std::string, void*>* table);

}  // namespace finn::darwin

#endif
