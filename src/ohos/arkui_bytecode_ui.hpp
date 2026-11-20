// Rebuilds the real ArkUI widget tree of an installed OpenHarmony HAP from
// its compiled Panda .abc bytecode and package resources.
//
// Real apps ship `ets/modules.abc`, not a declarative ui.json. The ArkTS
// compiler lowers a declarative build() into `observeComponentCreation2`
// calls whose arguments are a builder function and the ArkUI component
// constructor (`Text`, `Button`, `Column`, ...). This module reads those real
// calls, follows the real builder methods, and resolves the real $r resource
// ids found in their literal arrays through resources.index. A label is set
// only when the bytecode or the package's resource table actually supplies
// one; a component with no string is left empty rather than given a plausible
// default.

#ifndef FINN_OHOS_ARKUI_BYTECODE_UI_HPP
#define FINN_OHOS_ARKUI_BYTECODE_UI_HPP

#include <string>

namespace finn::ohos {

class ArkUiEngine;

// install_dir is an extracted HAP containing ets/modules.abc (the same
// directory passed to ArkUiEngine::set_resource_root). On success the engine
// owns a mounted tree reconstructed from the bytecode and error, when given,
// is cleared. On failure the engine is untouched and error names the missing
// fact: no .abc, no page class, no initialRender, or no component calls.
bool build_arkui_from_panda_bytecode(const std::string& install_dir,
                                     ArkUiEngine* engine,
                                     std::string* error);

// Fixture checks of the bytecode/resource decoders plus a pass over installed
// real packages (HOME/.local/share/finn/ohos/apps, or FINN_OHOS_INSTALL_DIR
// for one directory). Prints the headless tree of each rebuilt package and
// returns false only when a check fails. When no real packages are installed
// the fixture checks still run and the absence is reported honestly.
bool arkui_bytecode_ui_self_test();

}  // namespace finn::ohos

#endif
