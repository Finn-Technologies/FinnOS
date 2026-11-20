// Builds a package-derived ArkUI tree from an installed OpenHarmony HAP.
//
// Real HAPs from the OpenHarmony sample repository ship compiled Panda
// bytecode (ets/modules.abc) rather than a declarative ui.json. They do ship
// the facts a navigation shell needs: module.json (bundle, version, labels,
// icons), resources/base/profile/main_pages.json (the real page list),
// resources.index (the compiled string table) and resources/* (real media).
// This module turns exactly those facts into an ArkUiEngine tree. Nothing is
// invented: if the package has no page profile, the caller keeps its honest
// fallback card.

#ifndef FINN_OHOS_ARKUI_DECLARATIVE_HPP
#define FINN_OHOS_ARKUI_DECLARATIVE_HPP

#include <string>

namespace finn::ohos {

class ArkUiEngine;

// install_dir is an extracted HAP (the same directory passed to
// ArkUiEngine::set_resource_root by run_ohos_target). On success the engine
// owns a mounted Navigation tree built solely from package facts, and its
// title is the app label resolved from resources.index (or the bundle name
// when the package has no label resource). On failure the engine is untouched
// and error, when given, explains which package fact is missing.
bool build_declarative_ui_from_hap(const std::string& install_dir,
                                   ArkUiEngine* engine,
                                   std::string* error);

// Deterministic fixture checks plus a pass over installed real packages
// (HOME/.local/share/finn/ohos/apps, or FINN_OHOS_INSTALL_DIR for one dir).
// Prints what it verified and returns false if a check failed.
bool ohos_declarative_self_test();

}  // namespace finn::ohos

#endif
