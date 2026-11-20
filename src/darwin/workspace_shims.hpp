// Clean-room workspace, status-item and block-bridge shims for finn-darwin.
//
// Itsycal 0.29.0 is an Objective-C menu-bar app. Two things have to be real for
// it to launch from /Applications:
//
// 1. A block literal is an Objective-C object. Its isa must be a real Class so
//    that objc_msgSend can find -copy. The stock runtime uses bare `int`
//    sentinels for the three NSConcreteBlock classes; messaging one of those
//    makes the dispatch table read through an `int*` and crash. We bind the
//    block class symbols to real ObjcClass objects and supply a real _Block_copy
//    that keeps the heap copy's isa a real Class.
// 2. The startup path needs NSStatusBar/NSStatusItem, NSWorkspace and the
//    user's local date. The menu-bar title is taken from the system clock via
//    localtime_r/strftime; no calendar data is invented.

#ifndef FINN_DARWIN_WORKSPACE_SHIMS_HPP
#define FINN_DARWIN_WORKSPACE_SHIMS_HPP

#include <string>
#include <unordered_map>

namespace finn::darwin {

// Adds the shims to `symbols` (the same table the Mach-O loader binds imports
// from). Existing entries are replaced intentionally: the block class symbols
// must point at real classes, and the workspace/status-item methods must be
// the labelled, non-fabricating implementations below.
void register_workspace_shims(std::unordered_map<std::string, void*>* symbols);

// Host-side harness: checks the block-class bridge, real local date formatting
// and status-item lifetime rules without needing a display or a Mach-O.
bool workspace_shims_self_test();

}  // namespace finn::darwin

#endif  // FINN_DARWIN_WORKSPACE_SHIMS_HPP
