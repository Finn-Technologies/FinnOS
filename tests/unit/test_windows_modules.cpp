// Standalone headless regression test for the Windows compatibility modules.
// Each module ships a self-test that runs without Wine, a display or the VM.
// Run it with AddressSanitizer and UndefinedBehaviorSanitizer, as the other
// unit tests under tests/unit are run. No display, Wine install or VM needed.

#include "compat/msi_installer.hpp"
#include "compat/wine_prefix.hpp"
#include "compat/windows_registry.hpp"

#include <cstdio>

int main() {
    bool ok = true;

    const bool prefix = finn::compat::wine_prefix_self_test();
    std::printf("wine_prefix_self_test: %s\n", prefix ? "ok" : "FAIL");
    ok &= prefix;

    const bool msi = finn::compat::msi_installer_self_test();
    std::printf("msi_installer_self_test: %s\n", msi ? "ok" : "FAIL");
    ok &= msi;

    const bool package = finn::compat::windows_package_self_test();
    std::printf("windows_package_self_test: %s\n", package ? "ok" : "FAIL");
    ok &= package;

    const bool registry = finn::compat::windows_registry_self_test();
    std::printf("windows_registry_self_test: %s\n", registry ? "ok" : "FAIL");
    ok &= registry;

    if (!ok) return 1;
    std::puts("test_windows_modules: ok");
    return 0;
}
