#include "shell/full_oobe_flow.hpp"

#include <algorithm>
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

std::string_view step_id(Step step) noexcept {
    switch (step) {
        case Step::Welcome: return "welcome";
        case Step::Connectivity: return "connectivity";
        case Step::Account: return "account";
        case Step::Privacy: return "privacy";
        case Step::RegionLocale: return "region_locale";
        case Step::Accessibility: return "accessibility";
        case Step::Theme: return "theme";
        case Step::Finish: return "finish";
    }
    return "invalid";
}

std::string_view step_title(Step step) noexcept {
    switch (step) {
        case Step::Welcome: return "hi.";
        case Step::Connectivity: return "Where do we connect?";
        case Step::Account: return "What should we call you?";
        case Step::Privacy: return "How much should we remember?";
        case Step::RegionLocale: return "A big world. But one country. Where?";
        case Step::Accessibility: return "Need a hand?";
        case Step::Theme: return "Dark or light? Your choice.";
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
            // Informational: the ribbon intro runs here, there is nothing to
            // validate and no capability to claim.
            return pass("welcome is informational");

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
                return fail("a profile name is required to continue");
            }
            switch (account.outcome) {
                case AccountOutcome::Created:
                    if (caps_.account_service.state != Availability::Available) {
                        return fail("account creation cannot be claimed without a measured account service",
                                    "account_service");
                    }
                    return pass("real account service reported success");
                case AccountOutcome::LocalOnly:
                    return pass("local profile chosen; no account was claimed");
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

        case Step::Privacy:
            if (!data_.privacy.choice_made) {
                return fail("make an explicit privacy choice (leaving everything off is valid)");
            }
            return pass("privacy choice acknowledged");

        case Step::RegionLocale: {
            const LocaleCatalog& catalog = data_.catalog;
            const RegionState& region = data_.region;
            if (catalog.locales.empty() || catalog.regions.empty()) {
                return fail("no locale and region catalogue was supplied", "locale_catalogue");
            }
            if (!has_text(region.locale) || !contains(catalog.locales, region.locale)) {
                return fail("locale is not present in the supplied catalogue");
            }
            if (!has_text(region.region) || !contains(catalog.regions, region.region)) {
                return fail("region is not present in the supplied catalogue");
            }
            if (has_text(region.timezone) && !catalog.timezones.empty() &&
                !contains(catalog.timezones, region.timezone)) {
                return fail("timezone is not present in the supplied catalogue");
            }
            return pass("locale and region are from the real catalogue");
        }

        case Step::Accessibility:
            // Unset means "keep the system default".  It is a complete answer,
            // so this step can never block setup.
            if (data_.accessibility.screen_reader == TriState::On &&
                caps_.accessibility_bus.state != Availability::Available) {
                return fail("a screen reader cannot be enabled without an accessibility bus",
                            "accessibility_bus");
            }
            return pass("accessibility choices recorded (unset keeps system defaults)");

        case Step::Theme:
            if (data_.theme.choice == ThemeChoice::Unset) {
                return fail("choose light or dark appearance");
            }
            return pass("appearance choice recorded");

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

    // A previously recorded success is void when its capability is withdrawn.
    // Keeping it would be a fake capability surviving a hardware change.
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
    note(std::string("account: ") + std::string(account_name(data_.account.outcome)));
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

bool FullOobeFlow::setRegion(RegionState state) {
    const LocaleCatalog& catalog = data_.catalog;
    if (!catalog.locales.empty() && !contains(catalog.locales, state.locale)) {
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
    data_.region = std::move(state);
    last_error_.clear();
    note("region: " + data_.region.locale + "/" + data_.region.region +
         (data_.region.timezone.empty() ? std::string()
                                        : " (" + data_.region.timezone + ")"));
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

    const ConnectivityState& net = data_.connectivity;
    report_.configured.push_back(
        net.attempt == NetworkAttempt::Connected
            ? "network: connected to " + net.ssid
            : "network: offline (explicitly acknowledged)");
    report_.configured.push_back(
        data_.account.outcome == AccountOutcome::Created
            ? "account: created by the real service"
            : "account: local profile only");
    report_.configured.push_back("profile name: set");
    report_.configured.push_back("privacy: choice acknowledged");
    report_.configured.push_back("locale: " + data_.region.locale +
                                 " region: " + data_.region.region);
    report_.configured.push_back("accessibility: " +
                                 std::string(tri_name(data_.accessibility.reduced_motion)) +
                                 "/" + std::string(tri_name(data_.accessibility.high_contrast)) +
                                 "/" + std::string(tri_name(data_.accessibility.large_text)));
    report_.configured.push_back(
        std::string("appearance: ") +
        (data_.theme.choice == ThemeChoice::Light ? "light" : "dark"));

    // Every unmeasured or absent capability is named, with the reason it was
    // reported.  A report never silently upgrades Unknown to Available.
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
        " unconfirmed or absent capabilities; the completion marker is written "
        "only by a real settings sink at " + std::string(report_.marker_path);
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
