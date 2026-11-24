// Headless first-time-setup flow model for finn-shell / finn-oobe.
//
// This header is deliberately pure C++20: the step machine, validation and
// capability accounting must be testable without a display or a Qt build
// (AGENTS.md: "If you add behaviour that can be tested without a display,
// test it").  Nothing in here connects to a network, creates an account or
// writes a setting.  A caller supplies *measured* outcomes; absent outcomes
// stay absent and are reported as absent.

#ifndef FINN_SHELL_FULL_OOBE_FLOW_HPP
#define FINN_SHELL_FULL_OOBE_FLOW_HPP

#include "common/paths.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace finn::shell::oobe {

// The ten steps of first-boot setup, ordered to match the UI progression:
// language, region/timezone, and keyboard come first so the user can type
// Wi-Fi passphrases, account credentials, and hostname in their own language
// and layout; Finish is terminal.
enum class Step {
    Welcome = 0,
    Language,
    RegionTimezone,
    Keyboard,
    Connectivity,
    Account,
    Theme,
    Accessibility,
    Privacy,
    Finish,
    // Compatibility alias for callers naming the combined region/locale step.
    RegionLocale = RegionTimezone,
};

inline constexpr std::size_t kStepCount = 10;
inline constexpr std::array<Step, kStepCount> kStepOrder{{
    Step::Welcome,
    Step::Language,
    Step::RegionTimezone,
    Step::Keyboard,
    Step::Connectivity,
    Step::Account,
    Step::Theme,
    Step::Accessibility,
    Step::Privacy,
    Step::Finish,
}};

constexpr bool valid_step(Step step) noexcept {
    return static_cast<std::size_t>(step) < kStepCount;
}
constexpr std::size_t index_of(Step step) noexcept {
    return static_cast<std::size_t>(step);
}
// Stable machine ids (tests and the audit trail never depend on translations).
std::string_view step_id(Step step) noexcept;
std::string_view step_title(Step step) noexcept;

// Validation helpers shared by the model, OOBE UI, and unit tests.
bool is_valid_posix_username(std::string_view username) noexcept;
bool is_valid_rfc1123_hostname(std::string_view hostname) noexcept;
std::string suggest_hostname_from_name(std::string_view full_name);

// A capability is a measurement, never a default.  Unknown means "not
// probed"; it is not a quiet yes and never unlocks a success state.
enum class Availability { Unknown, Available, Unavailable };

struct Capability {
    Availability state = Availability::Unknown;
    std::string detail = "not probed";

    static Capability available(std::string detail) {
        return Capability{Availability::Available, std::move(detail)};
    }
    static Capability unavailable(std::string reason) {
        return Capability{Availability::Unavailable, std::move(reason)};
    }
    static Capability unknown(std::string why = "not probed") {
        return Capability{Availability::Unknown, std::move(why)};
    }
    bool confirmed() const noexcept { return state != Availability::Unknown; }
};

struct CapabilitySnapshot {
    Capability network_manager;    // is there a real connector (NetworkManager)?
    Capability wifi_hardware;      // is there a Wi-Fi device at all?
    Capability wired_link;         // is there a wired link?
    Capability account_service;    // is there a real account/first-boot helper?
    Capability settings_write;     // can the chosen settings be persisted?
    Capability accessibility_bus;  // is an AT-SPI/screen-reader bus present?
};

enum class NetworkAttempt {
    NotAttempted,
    Scanning,
    Attempted,
    Connected,
    Failed,
    Unavailable,
};

struct ConnectivityState {
    NetworkAttempt attempt = NetworkAttempt::NotAttempted;
    std::string ssid;
    std::string detail = "no scan has been run";
    int signal_quality = -1;  // -1 means unmeasured, not "weak signal"
    bool continue_offline = false;  // explicit user acknowledgement
};

enum class AccountOutcome {
    NotAttempted,
    Creating,
    Created,
    Failed,
    LocalOnly,
    Unavailable,
};

struct AccountState {
    AccountOutcome outcome = AccountOutcome::NotAttempted;
    std::string display_name;
    std::string username;
    std::string hostname = "finnos";
    bool passwords_match = true;
    std::string detail = "no account request has been made";
};

// Privacy defaults to the least permissive choice and requires an explicit
// acknowledgement.  Silence is never treated as consent.
struct PrivacyState {
    bool location = false;
    bool diagnostics = false;
    bool crash_reports = false;
    bool choice_made = false;
};

// Catalogues are supplied by the host from real locale/region/keyboard data.
// With no catalogue the step cannot validate: invented values are not accepted.
struct LocaleCatalog {
    std::vector<std::string> locales;
    std::vector<std::string> regions;
    std::vector<std::string> timezones;
    std::vector<std::string> keyboard_layouts;
};

struct RegionState {
    std::string locale;
    std::string region;
    std::string timezone;
};

struct KeyboardState {
    std::string layout = "us";
    std::string variant;
};

enum class TriState { Unset, On, Off };

struct AccessibilityState {
    TriState reduced_motion = TriState::Unset;
    TriState high_contrast = TriState::Unset;
    TriState large_text = TriState::Unset;
    TriState screen_reader = TriState::Unset;
};

enum class ThemeChoice { Unset, Light, Dark };

struct ThemeState {
    ThemeChoice choice = ThemeChoice::Unset;
};

struct OobeData {
    ConnectivityState connectivity;
    AccountState account;
    PrivacyState privacy;
    LocaleCatalog catalog;
    RegionState region;
    KeyboardState keyboard;
    AccessibilityState accessibility;
    ThemeState theme;
};

struct StepValidation {
    bool can_advance = false;
    std::string reason;
    std::string capability;
};

struct CompletionReport {
    bool complete = false;
    std::vector<std::string> configured;
    std::vector<std::string> unavailable;
    std::string summary;
    const char* marker_path = finn::kOobeCompleteRel;
    const char* system_marker_path = finn::kSystemOobeCompletePath;
};

class FullOobeFlow final {
public:
    FullOobeFlow() = default;
    explicit FullOobeFlow(CapabilitySnapshot caps) : caps_(std::move(caps)) {}

    // Navigation ---------------------------------------------------------
    Step current() const noexcept { return current_; }
    std::size_t index() const noexcept { return index_of(current_); }
    const std::vector<Step>& history() const noexcept { return history_; }
    bool canGoBack() const noexcept { return !history_.empty(); }
    bool isComplete() const noexcept { return complete_; }

    StepValidation validation() const { return validate(current_); }
    StepValidation validate(Step step) const;

    bool advance();
    bool back();
    bool goTo(Step target);

    // Inputs --------------------------------------------------------------
    void setCapabilities(CapabilitySnapshot caps);
    const CapabilitySnapshot& capabilities() const noexcept { return caps_; }
    void setCatalog(LocaleCatalog catalog) { data_.catalog = std::move(catalog); }

    bool setLanguage(const std::string& locale);
    bool setRegion(RegionState state);
    bool setKeyboard(KeyboardState state);
    bool setConnectivity(ConnectivityState state);
    bool setAccount(AccountState state);
    bool setPrivacy(PrivacyState state);
    bool setAccessibility(AccessibilityState state);
    bool setTheme(ThemeState state);

    const OobeData& data() const noexcept { return data_; }
    const CompletionReport& report() const { return report_; }
    const std::vector<std::string>& audit() const noexcept { return audit_; }
    const std::string& last_error() const noexcept { return last_error_; }

    void reset(CapabilitySnapshot caps = {});

private:
    void finalize();
    void note(std::string entry);
    bool contains(const std::vector<std::string>& list, const std::string& value) const;

    CapabilitySnapshot caps_;
    OobeData data_;
    Step current_ = Step::Welcome;
    std::vector<Step> history_;
    CompletionReport report_;
    std::vector<std::string> audit_;
    std::string last_error_;
    bool complete_ = false;
};

}  // namespace finn::shell::oobe

#endif  // FINN_SHELL_FULL_OOBE_FLOW_HPP
