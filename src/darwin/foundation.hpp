// Clean-room CoreFoundation + Foundation framework implementation for finn-darwin.
// Provides NSObject, NSString/NSMutableString/NSConstantString, NSArray, NSDictionary,
// NSNumber, NSData/NSMutableData, NSDate, NSError, NSNull, NSJSONSerialization,
// NSURL, NSBundle, NSProcessInfo, NSFileManager, NSUserDefaults (with disk persistence),
// NSNotificationCenter, NSRunLoop, NSAutoreleasePool, NSLog, and core CF* C APIs.

#ifndef FINN_DARWIN_FOUNDATION_HPP
#define FINN_DARWIN_FOUNDATION_HPP

#include "darwin/objc_runtime.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finn::darwin {

// Registers all Foundation & CoreFoundation Objective-C classes and C symbols.
void init_foundation(const std::string& bundle_path = {});
void register_foundation_symbols(std::unordered_map<std::string, void*>* table);

// Provides process arguments exposed via NSProcessInfo.processInfo.arguments / processName.
void set_foundation_process_args(int argc, char** argv);

// Forces NSUserDefaults to reload from disk (used on bundle change and in tests).
void reload_user_defaults();

// Returns the current on-disk path used by NSUserDefaults.standardUserDefaults.
std::string user_defaults_file_path();

// C++ bridge helpers used by AppKit, CoreGraphics, and tests.
ObjcId nsstring_from_utf8(std::string_view text);
std::string nsstring_to_utf8(ObjcId str_obj);
ObjcId nsdata_from_bytes(const void* ptr, std::size_t len, bool mut = false);
ObjcId nsurl_from_path(std::string_view path);
ObjcId nserror_create(std::string_view domain, std::intptr_t code, std::string_view description);

}  // namespace finn::darwin

#endif
