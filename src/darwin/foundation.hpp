// Clean-room CoreFoundation + Foundation framework implementation for finn-darwin.
// Provides NSObject, NSString/NSMutableString/NSConstantString, NSArray, NSDictionary,
// NSNumber, NSData, NSURL, NSBundle, NSProcessInfo, NSFileManager, NSUserDefaults,
// NSNotificationCenter, NSAutoreleasePool, NSLog, and core CF* C APIs.

#ifndef FINN_DARWIN_FOUNDATION_HPP
#define FINN_DARWIN_FOUNDATION_HPP

#include "darwin/objc_runtime.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace finn::darwin {

// Registers all Foundation & CoreFoundation Objective-C classes and C symbols.
void init_foundation(const std::string& bundle_path = {});
void register_foundation_symbols(std::unordered_map<std::string, void*>* table);

// C++ bridge helpers used by AppKit and tests
ObjcId nsstring_from_utf8(std::string_view text);
std::string nsstring_to_utf8(ObjcId str_obj);

}  // namespace finn::darwin

#endif
