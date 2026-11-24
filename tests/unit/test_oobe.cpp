// ASan/UBSan unit tests for OOBE first-boot gating, step ordering, validation,
// hostname/username sanitisation, and capability withdrawal.

#include "common/paths.hpp"
#include "shell/full_oobe_flow.hpp"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

using namespace finn::shell::oobe;

void test_first_boot_gating() {
    // 1. Installed normal first boot: runs OOBE.
    assert(finn::should_run_oobe_on_boot(
        "root=UUID=1234 quiet splash vt.global_cursor_default=0",
        /*live_marker=*/false,
        /*installed_marker=*/true,
        /*sys_complete=*/false,
        /*user_complete=*/false));

    // 2. USB live / installer / recovery boots MUST NEVER run OOBE.
    assert(!finn::should_run_oobe_on_boot(
        "archisobasedir=finn archisolabel=FINNOS quiet splash",
        false, true, false, false));
    assert(!finn::should_run_oobe_on_boot(
        "root=UUID=1234 finn.recovery quiet splash",
        false, true, false, false));
    assert(!finn::should_run_oobe_on_boot(
        "root=UUID=1234 finn.install quiet splash",
        false, true, false, false));
    assert(!finn::should_run_oobe_on_boot(
        "root=UUID=1234 quiet splash",
        /*live_marker=*/true, true, false, false));

    // 3. Uninstalled developer host without /var/lib/finn/installed does not run unless forced.
    assert(!finn::should_run_oobe_on_boot(
        "quiet splash", false, /*installed_marker=*/false, false, false));
    assert(finn::should_run_oobe_on_boot(
        "quiet splash", false, false, false, false, "1"));

    // 4. Subsequent normal boots after OOBE completion MUST NOT re-run OOBE.
    assert(!finn::should_run_oobe_on_boot(
        "root=UUID=1234 quiet splash", false, true, /*sys_complete=*/true, false));
    assert(!finn::should_run_oobe_on_boot(
        "root=UUID=1234 quiet splash", false, true, false, /*user_complete=*/true));
}

void test_identifiers_and_hostname_suggestions() {
    assert(is_valid_posix_username("finn"));
    assert(is_valid_posix_username("abhi_dev-1"));
    assert(!is_valid_posix_username(""));
    assert(!is_valid_posix_username("root"));
    assert(!is_valid_posix_username("1bad"));
    assert(!is_valid_posix_username("BadCaps"));
    assert(!is_valid_posix_username("has space"));

    assert(is_valid_rfc1123_hostname("finnos"));
    assert(is_valid_rfc1123_hostname("abhi-finnos-01"));
    assert(!is_valid_rfc1123_hostname(""));
    assert(!is_valid_rfc1123_hostname("-leading"));
    assert(!is_valid_rfc1123_hostname("trailing-"));
    assert(!is_valid_rfc1123_hostname("UpperCase"));
    assert(!is_valid_rfc1123_hostname("under_score"));

    assert(suggest_hostname_from_name("Abhi") == "abhi-finnos");
    assert(suggest_hostname_from_name("  ") == "finnos");
    assert(suggest_hostname_from_name("Jane Doe!!!") == "jane-doe-finnos");
}

void test_complete_10_step_flow() {
    CapabilitySnapshot caps;
    caps.network_manager = Capability::available("nmcli");
    caps.wifi_hardware = Capability::available("wlan0");
    caps.wired_link = Capability::unavailable("no eth");
    caps.account_service = Capability::available("finn-oobe-apply");
    caps.settings_write = Capability::available("writable");
    caps.accessibility_bus = Capability::unavailable("no a11y bus");

    FullOobeFlow flow(caps);
    LocaleCatalog cat;
    cat.locales = {"en_US.UTF-8", "de_DE.UTF-8"};
    cat.regions = {"US", "DE"};
    cat.timezones = {"America/New_York", "Europe/Berlin", "UTC"};
    cat.keyboard_layouts = {"us", "de"};
    flow.setCatalog(cat);

    // Step 0: Welcome -> Step 1: Language
    assert(flow.current() == Step::Welcome);
    assert(flow.advance());
    assert(flow.current() == Step::Language);

    // Language requires valid locale in catalogue
    assert(!flow.advance());
    assert(!flow.setLanguage("fr_FR.UTF-8"));
    assert(flow.setLanguage("de_DE.UTF-8"));
    assert(flow.advance());

    // Step 2: Region & Timezone
    assert(flow.current() == Step::RegionTimezone);
    assert(!flow.advance());
    assert(!flow.setRegion({"de_DE.UTF-8", "DE", "Mars/Olympus"}));
    assert(flow.setRegion({"de_DE.UTF-8", "DE", "Europe/Berlin"}));
    assert(flow.advance());

    // Step 3: Keyboard
    assert(flow.current() == Step::Keyboard);
    assert(flow.setKeyboard({"de", ""}));
    assert(flow.advance());

    // Step 4: Connectivity (offline acknowledgement required if not connected)
    assert(flow.current() == Step::Connectivity);
    assert(!flow.advance());
    ConnectivityState offline;
    offline.attempt = NetworkAttempt::Failed;
    offline.continue_offline = true;
    assert(flow.setConnectivity(offline));
    assert(flow.advance());

    // Step 5: Account + Hostname
    assert(flow.current() == Step::Account);
    assert(!flow.advance());
    AccountState bad_pw;
    bad_pw.outcome = AccountOutcome::LocalOnly;
    bad_pw.display_name = "Ada Lovelace";
    bad_pw.username = "ada";
    bad_pw.hostname = "ada-finnos";
    bad_pw.passwords_match = false;
    assert(!flow.setAccount(bad_pw));

    AccountState good_acc = bad_pw;
    good_acc.passwords_match = true;
    assert(flow.setAccount(good_acc));
    assert(flow.advance());

    // Step 6: Theme
    assert(flow.current() == Step::Theme);
    assert(!flow.advance());
    assert(flow.setTheme({ThemeChoice::Dark}));
    assert(flow.advance());

    // Step 7: Accessibility (screen reader blocked without accessibility_bus)
    assert(flow.current() == Step::Accessibility);
    AccessibilityState bad_a11y;
    bad_a11y.screen_reader = TriState::On;
    assert(!flow.setAccessibility(bad_a11y));
    assert(flow.advance());

    // Step 8: Privacy -> Step 9: Finish
    assert(flow.current() == Step::Privacy);
    assert(!flow.advance());
    assert(flow.setPrivacy({false, false, false, true}));
    assert(flow.advance());
    assert(flow.current() == Step::Finish);
    assert(flow.isComplete());
    assert(flow.report().complete);
}

}  // namespace

int main() {
    test_first_boot_gating();
    test_identifiers_and_hostname_suggestions();
    test_complete_10_step_flow();
    std::puts("test_oobe: ok");
    return 0;
}
