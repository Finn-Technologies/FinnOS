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

// The eight steps of a complete setup.  The order is the product contract:
// connectivity and account precede the choices that can be personalised by
// them, and Finish is terminal.
enum class Step {
    Welcome = 0,
    Connectivity,
    Account,
    Privacy,
    RegionLocale,
    Accessibility,
    Theme,
    Finish,
};

inline constexpr std::size_t kStepCount = 8;
inline constexpr std::array<Step, kStepCount> kStepOrder{{
    Step::Welcome,       Step::Connectivity, Step::Account, Step::Privacy,
    Step::RegionLocale,  Step::Accessibility, Step::Theme,  Step::Finish,
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
    Capability account_service;    // is there a real account/local-signup service?
    Capability settings_write;     // can the chosen settings be persisted?
    Capability accessibility_bus;  // is an AT-SPI/screen-reader bus present?
};

// Connectivity starts at NotAttempted.  Only a caller holding a real connect
// result may write Connected, and setConnectivity() refuses to record it when
// no network manager capability was measured as available.
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

// Catalogues are supplied by the host from real locale/region data.  With no
// catalogue the step cannot validate: invented values are not accepted.
struct LocaleCatalog {
    std::vector<std::string> locales;
    std::vector<std::string> regions;
    std::vector<std::string> timezones;
};

struct RegionState {
    std::string locale;
    std::string region;
    std::string timezone;  // empty is allowed and means "not selected"
};

// Unset means "keep the system default" — not "off".  This lets a person
// finish setup without claiming an accessibility preference they did not make.
enum class TriState { Unset, On, Off };

struct AccessibilityState {
    TriState reduced_motion = TriState::Unset;
    TriState high_contrast = TriState::Unset;
    TriState large_text = TriState::Unset;
    TriState screen_reader = TriState::Unset;
};

// v1 has no clock-driven appearance service.  "Auto" is not offered because
// it would have to guess the user's intent from a time we do not own.
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
    AccessibilityState accessibility;
    ThemeState theme;
};

struct StepValidation {
    bool can_advance = false;
    std::string reason;
    std::string capability;  // named missing capability, empty when not capability-bound
};

struct CompletionReport {
    bool complete = false;
    std::vector<std::string> configured;
    std::vector<std::string> unavailable;
    std::string summary;
    // The marker is a shared fact; writing it belongs to a real settings sink,
    // not to this model.
    const char* marker_path = finn::kOobeCompleteRel;
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

    // advance() validates the *current* step before moving.  finish is
    // terminal; a report is only produced by arriving there through advance().
    bool advance();
    bool back();
    // Only visited steps may be revisited; forward motion always goes through
    // advance() so no step can be skipped by a jump.
    bool goTo(Step target);

    // Inputs --------------------------------------------------------------
    void setCapabilities(CapabilitySnapshot caps);
    const CapabilitySnapshot& capabilities() const noexcept { return caps_; }
    void setCatalog(LocaleCatalog catalog) { data_.catalog = std::move(catalog); }

    // Each setter either records a real measurement or refuses it.  A false
    // return leaves the state untouched and fills last_error().
    bool setConnectivity(ConnectivityState state);
    bool setAccount(AccountState state);
    bool setPrivacy(PrivacyState state);
    bool setRegion(RegionState state);
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
