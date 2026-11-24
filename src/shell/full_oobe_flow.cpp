#include "shell/full_oobe_flow.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace finn::shell::oobe {
namespace {

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool has_text(const std::string& text) { return !trimmed(text).empty(); }

StepValidation pass(std::string_view reason) {
    return StepValidation{true, std::string(reason), std::string()};
}

StepValidation fail(std::string_view reason, std::string_view capability = {}) {
    return StepValidation{false, std::string(reason), std::string(capability)};
}

std::string_view network_name(NetworkAttempt attempt) {
    switch (attempt) {
        case NetworkAttempt::NotAttempted: return "not_attempted";
        case NetworkAttempt::Scanning: return "scanning";
        case NetworkAttempt::Attempted: return "attempted";
        case NetworkAttempt::Connected: return "connected";
        case NetworkAttempt::Failed: return "failed";
        case NetworkAttempt::Unavailable: return "unavailable";
    }
    return "unknown";
}

std::string_view account_name(AccountOutcome outcome) {
    switch (outcome) {
        case AccountOutcome::NotAttempted: return "not_attempted";
        case AccountOutcome::Creating: return "creating";
        case AccountOutcome::Created: return "created";
        case AccountOutcome::Failed: return "failed";
        case AccountOutcome::LocalOnly: return "local_only";
        case AccountOutcome::Unavailable: return "unavailable";
    }
    return "unknown";
}

std::string_view tri_name(TriState state) {
    switch (state) {
        case TriState::Unset: return "unset";
        case TriState::On: return "on";
        case TriState::Off: return "off";
    }
    return "unknown";
}

}  // namespace

bool is_valid_posix_username(std::string_view username) noexcept {
    if (username.empty() || username.size() > 32) return false;
    const unsigned char c0 = static_cast<unsigned char>(username[0]);
    if (!((c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for (std::size_t i = 1; i < username.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(username[i]);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) return false;
    }
    if (username == "root") return false;
    return true;
}

bool is_valid_rfc1123_hostname(std::string_view hostname) noexcept {
    if (hostname.empty() || hostname.size() > 63) return false;
    const unsigned char first = static_cast<unsigned char>(hostname.front());
    const unsigned char last = static_cast<unsigned char>(hostname.back());
    auto is_alnum_lower = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    if (!is_alnum_lower(first) || !is_alnum_lower(last)) return false;
    for (char ch : hostname) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!is_alnum_lower(c) && c != '-') return false;
    }
    return true;
}

std::string suggest_hostname_from_name(std::string_view full_name) {
    std::string slug;
    for (char ch : trimmed(full_name)) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            slug.push_back(static_cast<char>(std::tolower(c)));
        } else if ((c == ' ' || c == '-' || c == '_') && !slug.empty() && slug.back() != '-') {
            slug.push_back('-');
        }
        if (slug.size() >= 48) break;
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) return "finnos";
    std::string out = slug + "-finnos";
    if (out.size() > 63) out.resize(63);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return is_valid_rfc1123_hostname(out) ? out : "finnos";
}

std::string_view step_id(Step step) noexcept {
    switch (step) {
        case Step::Welcome: return "welcome";
        case Step::Language: return "language";
        case Step::RegionTimezone: return "region_timezone";
        case Step::Keyboard: return "keyboard";
        case Step::Connectivity: return "connectivity";
        case Step::Account: return "account";
        case Step::Theme: return "theme";
        case Step::Accessibility: return "accessibility";
        case Step::Privacy: return "privacy";
        case Step::Finish: return "finish";
    }
    return "invalid";
}

std::string_view step_title(Step step) noexcept {
    switch (step) {
        case Step::Welcome: return "hi.";
        case Step::Language: return "How do we talk?";
        case Step::RegionTimezone: return "Where in the world?";
        case Step::Keyboard: return "How do you type?";
        case Step::Connectivity: return "Where do we connect?";
        case Step::Account: return "What should we call you?";
        case Step::Theme: return "Dark or light? Your choice.";
        case Step::Accessibility: return "Need a hand?";
        case Step::Privacy: return "How much should we remember?";
        case Step::Finish: return "You're all set";
    }
    return "Unknown step";
}

bool FullOobeFlow::contains(const std::vector<std::string>& list,
                            const std::string& value) const {
    return std::find(list.begin(), list.end(), value) != list.end();
}

void FullOobeFlow::note(std::string entry) {
    audit_.push_back(std::move(entry));
}

StepValidation FullOobeFlow::validate(Step step) const {
    switch (step) {
        case Step::Welcome:
            return pass("welcome is informational");

        case Step::Language: {
            const LocaleCatalog& catalog = data_.catalog;
            if (catalog.locales.empty()) {
                return fail("no language catalogue was supplied", "locale_catalogue");
            }
            if (!has_text(data_.region.locale) || !contains(catalog.locales, data_.region.locale)) {
                return fail("select a language from the catalogue");
            }
            return pass("language selected from catalogue");
        }

        case Step::RegionTimezone: {
            const LocaleCatalog& catalog = data_.catalog;
            const RegionState& region = data_.region;
            if (catalog.regions.empty()) {
                return fail("no region catalogue was supplied", "locale_catalogue");
            }
            if (!has_text(region.region) || !contains(catalog.regions, region.region)) {
                return fail("select a country or region");
            }
            if (!catalog.timezones.empty()) {
                if (!has_text(region.timezone) || !contains(catalog.timezones, region.timezone)) {
                    return fail("select a valid time zone");
                }
            }
            return pass("region and time zone selected");
        }

        case Step::Keyboard: {
            const LocaleCatalog& catalog = data_.catalog;
            const KeyboardState& kb = data_.keyboard;
            if (!has_text(kb.layout)) {
                return fail("select a keyboard layout");
            }
            if (!catalog.keyboard_layouts.empty() && !contains(catalog.keyboard_layouts, kb.layout)) {
                return fail("keyboard layout is not present in the supplied catalogue");
            }
            return pass("keyboard layout selected");
        }

        case Step::Connectivity: {
            const ConnectivityState& net = data_.connectivity;
            switch (net.attempt) {
                case NetworkAttempt::Connected:
                    if (caps_.network_manager.state != Availability::Available) {
                        return fail("a connection cannot be claimed without a measured network manager",
                                    "network_manager");
                    }
                    if (!has_text(net.ssid)) {
                        return fail("a connected result must name the network it reached");
                    }
                    return pass("connected to a named network");
                case NetworkAttempt::Attempted:
                case NetworkAttempt::Failed:
                case NetworkAttempt::Unavailable:
                    if (net.continue_offline) {
                        return pass("continuing offline by explicit choice");
                    }
                    if (net.attempt == NetworkAttempt::Unavailable &&
                        caps_.network_manager.state == Availability::Unavailable) {
                        return fail("no network manager is present; acknowledge continuing offline",
                                    "network_manager");
                    }
                    return fail("no connection was established; acknowledge continuing offline");
                case NetworkAttempt::Scanning:
                    return fail("the network scan has not finished");
                case NetworkAttempt::NotAttempted:
                default:
                    return fail("no network attempt has been recorded");
            }
        }

        case Step::Account: {
            const AccountState& account = data_.account;
            if (!has_text(account.display_name)) {
                return fail("enter your full name to continue");
            }
            if (!has_text(account.username) || !is_valid_posix_username(account.username)) {
                return fail("account name must start with a lowercase letter and use [a-z0-9_-]");
            }
            if (!has_text(account.hostname) || !is_valid_rfc1123_hostname(account.hostname)) {
                return fail("computer hostname must use lowercase letters, digits, and hyphens");
            }
            if (!account.passwords_match) {
                return fail("passwords do not match");
            }
            switch (account.outcome) {
                case AccountOutcome::Created:
                    if (caps_.account_service.state != Availability::Available) {
                        return fail("account creation cannot be claimed without a measured account service",
                                    "account_service");
                    }
                    return pass("real account service reported success");
                case AccountOutcome::LocalOnly:
                    return pass("account details validated; provisioned at Finish");
                case AccountOutcome::Unavailable:
                    return fail("account service is absent; choose a local profile to continue",
                                "account_service");
                case AccountOutcome::Failed:
                    return fail("account creation failed: " + account.detail);
                case AccountOutcome::Creating:
                    return fail("account creation is still in progress");
                case AccountOutcome::NotAttempted:
                default:
                    return fail("no account outcome has been recorded");
            }
        }

        case Step::Theme:
            if (data_.theme.choice == ThemeChoice::Unset) {
                return fail("choose light or dark appearance");
            }
            return pass("appearance choice recorded");

        case Step::Accessibility:
            if (data_.accessibility.screen_reader == TriState::On &&
                caps_.accessibility_bus.state != Availability::Available) {
                return fail("a screen reader cannot be enabled without an accessibility bus",
                            "accessibility_bus");
            }
            return pass("accessibility choices recorded (unset keeps system defaults)");

        case Step::Privacy:
            if (!data_.privacy.choice_made) {
                return fail("make an explicit privacy choice (leaving everything off is valid)");
            }
            return pass("privacy choice acknowledged");

        case Step::Finish:
        default:
            return fail("finish is terminal");
    }
}

bool FullOobeFlow::advance() {
    if (current_ == Step::Finish) {
        last_error_ = "finish is terminal";
        return false;
    }
    const StepValidation check = validate(current_);
    if (!check.can_advance) {
        last_error_ = check.reason;
        return false;
    }
    const Step from = current_;
    history_.push_back(from);
    current_ = kStepOrder[index_of(from) + 1];
    last_error_.clear();
    note(std::string("step ") + std::string(step_id(from)) + " -> " +
         std::string(step_id(current_)));
    if (current_ == Step::Finish) finalize();
    return true;
}

bool FullOobeFlow::back() {
    if (history_.empty()) {
        last_error_ = "already at the first step";
        return false;
    }
    const Step from = current_;
    current_ = history_.back();
    history_.pop_back();
    complete_ = false;
    report_ = CompletionReport{};
    last_error_.clear();
    note(std::string("step ") + std::string(step_id(from)) + " -> " +
         std::string(step_id(current_)) + " (back)");
    return true;
}

bool FullOobeFlow::goTo(Step target) {
    if (!valid_step(target)) {
        last_error_ = "invalid step";
        return false;
    }
    if (target == current_) return true;
    const auto it = std::find(history_.begin(), history_.end(), target);
    if (it == history_.end()) {
        last_error_ = "forward navigation must use advance() so no step is skipped";
        return false;
    }
    const std::size_t keep = static_cast<std::size_t>(it - history_.begin());
    const Step from = current_;
    history_.resize(keep);
    current_ = target;
    complete_ = false;
    report_ = CompletionReport{};
    last_error_.clear();
    note(std::string("step ") + std::string(step_id(from)) + " -> " +
         std::string(step_id(target)) + " (revisit)");
    return true;
}

void FullOobeFlow::setCapabilities(CapabilitySnapshot caps) {
    caps_ = std::move(caps);

    if (data_.connectivity.attempt == NetworkAttempt::Connected &&
        caps_.network_manager.state != Availability::Available) {
        data_.connectivity = ConnectivityState{};
        data_.connectivity.detail = "network manager capability withdrawn; connection cleared";
        note("connectivity cleared: network_manager is no longer available");
    }
    if (data_.account.outcome == AccountOutcome::Created &&
        caps_.account_service.state != Availability::Available) {
        data_.account = AccountState{};
        data_.account.detail = "account service capability withdrawn; creation cleared";
        note("account cleared: account_service is no longer available");
    }
    if (data_.accessibility.screen_reader == TriState::On &&
        caps_.accessibility_bus.state != Availability::Available) {
        data_.accessibility.screen_reader = TriState::Unset;
        note("screen reader reset: accessibility_bus is no longer available");
    }
    last_error_.clear();
}

bool FullOobeFlow::setLanguage(const std::string& locale) {
    const LocaleCatalog& catalog = data_.catalog;
    if (!has_text(locale) || (!catalog.locales.empty() && !contains(catalog.locales, locale))) {
        last_error_ = "locale is not in the supplied catalogue";
        return false;
    }
    data_.region.locale = locale;
    last_error_.clear();
    note("language: " + locale);
    return true;
}

bool FullOobeFlow::setRegion(RegionState state) {
    const LocaleCatalog& catalog = data_.catalog;
    if (has_text(state.locale) && !catalog.locales.empty() &&
        !contains(catalog.locales, state.locale)) {
        last_error_ = "locale is not in the supplied catalogue";
        return false;
    }
    if (!catalog.regions.empty() && !contains(catalog.regions, state.region)) {
        last_error_ = "region is not in the supplied catalogue";
        return false;
    }
    if (has_text(state.timezone) && !catalog.timezones.empty() &&
        !contains(catalog.timezones, state.timezone)) {
        last_error_ = "timezone is not in the supplied catalogue";
        return false;
    }
    if (!has_text(state.locale)) state.locale = data_.region.locale;
    data_.region = std::move(state);
    last_error_.clear();
    note("region: " + data_.region.locale + "/" + data_.region.region +
         (data_.region.timezone.empty() ? std::string()
                                        : " (" + data_.region.timezone + ")"));
    return true;
}

bool FullOobeFlow::setKeyboard(KeyboardState state) {
    const LocaleCatalog& catalog = data_.catalog;
    if (!has_text(state.layout)) {
        last_error_ = "keyboard layout is required";
        return false;
    }
    if (!catalog.keyboard_layouts.empty() &&
        !contains(catalog.keyboard_layouts, state.layout)) {
        last_error_ = "keyboard layout is not in the supplied catalogue";
        return false;
    }
    data_.keyboard = std::move(state);
    last_error_.clear();
    note("keyboard: " + data_.keyboard.layout +
         (data_.keyboard.variant.empty() ? std::string()
                                         : "(" + data_.keyboard.variant + ")"));
    return true;
}

bool FullOobeFlow::setConnectivity(ConnectivityState state) {
    if (state.attempt == NetworkAttempt::Connected) {
        if (caps_.network_manager.state != Availability::Available) {
            last_error_ = "cannot record a connection without a measured network manager";
            return false;
        }
        if (!has_text(state.ssid)) {
            last_error_ = "a connection must name the network it reached";
            return false;
        }
        state.continue_offline = false;
    }
    if (state.attempt == NetworkAttempt::Unavailable) {
        if (caps_.network_manager.state != Availability::Unavailable) {
            last_error_ = "cannot record an unavailable network without a measured absence";
            return false;
        }
    }
    if (state.attempt == NetworkAttempt::Scanning) state.signal_quality = -1;
    data_.connectivity = std::move(state);
    last_error_.clear();
    note(std::string("connectivity: ") + std::string(network_name(data_.connectivity.attempt)) +
         (data_.connectivity.ssid.empty() ? std::string()
                                          : " (" + data_.connectivity.ssid + ")"));
    return true;
}

bool FullOobeFlow::setAccount(AccountState state) {
    if (!has_text(state.display_name)) {
        last_error_ = "a profile name is required";
        return false;
    }
    if (!has_text(state.username)) {
        state.username = "finn";
    }
    if (!is_valid_posix_username(state.username)) {
        last_error_ = "account name must match [a-z_][a-z0-9_-]*";
        return false;
    }
    if (!has_text(state.hostname)) {
        state.hostname = "finnos";
    }
    if (!is_valid_rfc1123_hostname(state.hostname)) {
        last_error_ = "invalid RFC-1123 computer hostname";
        return false;
    }
    if (!state.passwords_match) {
        last_error_ = "passwords do not match";
        return false;
    }
    switch (state.outcome) {
        case AccountOutcome::Created:
            if (caps_.account_service.state != Availability::Available) {
                last_error_ = "cannot record account creation without a measured account service";
                return false;
            }
            break;
        case AccountOutcome::Unavailable:
            if (caps_.account_service.state != Availability::Unavailable) {
                last_error_ = "cannot record an absent account service without measuring it";
                return false;
            }
            if (!has_text(state.detail)) {
                last_error_ = "an absent account service must say why it is absent";
                return false;
            }
            break;
        case AccountOutcome::Failed:
            if (!has_text(state.detail)) {
                last_error_ = "a failed account request must say why it failed";
                return false;
            }
            break;
        default:
            break;
    }
    data_.account = std::move(state);
    last_error_.clear();
    note(std::string("account: ") + std::string(account_name(data_.account.outcome)) +
         " user=" + data_.account.username + " host=" + data_.account.hostname);
    return true;
}

bool FullOobeFlow::setPrivacy(PrivacyState state) {
    data_.privacy = state;
    last_error_.clear();
    note(std::string("privacy: acknowledged=") +
         (state.choice_made ? "yes" : "no") +
         " location=" + (state.location ? "on" : "off") +
         " diagnostics=" + (state.diagnostics ? "on" : "off") +
         " crash_reports=" + (state.crash_reports ? "on" : "off"));
    return true;
}

bool FullOobeFlow::setAccessibility(AccessibilityState state) {
    if (state.screen_reader == TriState::On &&
        caps_.accessibility_bus.state != Availability::Available) {
        last_error_ = "cannot enable a screen reader without a measured accessibility bus";
        return false;
    }
    data_.accessibility = state;
    last_error_.clear();
    note(std::string("accessibility: reduced_motion=") + std::string(tri_name(state.reduced_motion)) +
         " high_contrast=" + std::string(tri_name(state.high_contrast)) +
         " large_text=" + std::string(tri_name(state.large_text)) +
         " screen_reader=" + std::string(tri_name(state.screen_reader)));
    return true;
}

bool FullOobeFlow::setTheme(ThemeState state) {
    if (state.choice == ThemeChoice::Unset) {
        last_error_ = "an appearance choice must be made explicitly";
        return false;
    }
    data_.theme = state;
    last_error_.clear();
    note(std::string("theme: ") +
         (state.choice == ThemeChoice::Light ? "light" : "dark"));
    return true;
}

void FullOobeFlow::finalize() {
    report_ = CompletionReport{};
    report_.complete = true;

    report_.configured.push_back("locale: " + data_.region.locale +
                                 " region: " + data_.region.region +
                                 (data_.region.timezone.empty() ? std::string()
                                                                : " tz: " + data_.region.timezone));
    report_.configured.push_back("keyboard: " + data_.keyboard.layout +
                                 (data_.keyboard.variant.empty() ? std::string()
                                                                 : "(" + data_.keyboard.variant + ")"));
    const ConnectivityState& net = data_.connectivity;
    report_.configured.push_back(
        net.attempt == NetworkAttempt::Connected
            ? "network: connected to " + net.ssid
            : "network: offline (explicitly acknowledged)");
    report_.configured.push_back(
        data_.account.outcome == AccountOutcome::Created
            ? "account: created by the real service"
            : "account: validated for first-boot provisioning");
    report_.configured.push_back("username: " + data_.account.username +
                                 " hostname: " + data_.account.hostname);
    report_.configured.push_back(
        std::string("appearance: ") +
        (data_.theme.choice == ThemeChoice::Light ? "light" : "dark"));
    report_.configured.push_back("accessibility: " +
                                 std::string(tri_name(data_.accessibility.reduced_motion)) +
                                 "/" + std::string(tri_name(data_.accessibility.high_contrast)) +
                                 "/" + std::string(tri_name(data_.accessibility.large_text)));
    report_.configured.push_back("privacy: choice acknowledged");

    struct CapEntry {
        const char* name;
        const Capability& value;
    };
    const CapEntry entries[] = {
        {"network_manager", caps_.network_manager},
        {"wifi_hardware", caps_.wifi_hardware},
        {"wired_link", caps_.wired_link},
        {"account_service", caps_.account_service},
        {"settings_write", caps_.settings_write},
        {"accessibility_bus", caps_.accessibility_bus},
    };
    for (const CapEntry& entry : entries) {
        if (entry.value.state == Availability::Available) continue;
        report_.unavailable.push_back(std::string(entry.name) + ": " +
                                      entry.value.detail);
    }

    report_.summary =
        "Setup finished with " + std::to_string(report_.configured.size()) +
        " recorded choices and " + std::to_string(report_.unavailable.size()) +
        " unconfirmed or absent capabilities; completion markers are written "
        "only after real provisioning at " + std::string(report_.system_marker_path) +
        " and " + std::string(report_.marker_path);
    complete_ = true;
    note("finish: setup report generated");
}

void FullOobeFlow::reset(CapabilitySnapshot caps) {
    caps_ = std::move(caps);
    data_ = OobeData{};
    current_ = Step::Welcome;
    history_.clear();
    report_ = CompletionReport{};
    audit_.clear();
    last_error_.clear();
    complete_ = false;
    note("reset: flow returned to welcome");
}

}  // namespace finn::shell::oobe
