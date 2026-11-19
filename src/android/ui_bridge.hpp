// Android view bridge for finn-android: inflates real binary AXML layouts from
// an APK into a view tree and renders it with the FinnOS Qt6/Wayland theme.
//
// There is no Android framework here. Only the view classes listed below are
// modelled; an unknown widget is reported, not silently drawn as something
// else. Compiled resource references that cannot be resolved stay empty.

#ifndef FINN_ANDROID_UI_BRIDGE_HPP
#define FINN_ANDROID_UI_BRIDGE_HPP

#include "android/android_format.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finn::android {

enum class AndroidViewKind {
    LinearLayout,
    FrameLayout,
    RelativeLayout,
    ScrollView,
    TextView,
    Button,
    EditText,
    ImageView,
    CheckBox,
    Switch,
    SeekBar,
    ProgressBar,
    ListView,
    RecyclerView,
    Unknown,
};

const char* android_view_kind_name(AndroidViewKind kind);
AndroidViewKind android_view_kind_from_name(std::string_view name);

struct AndroidViewNode {
    std::string id;
    AndroidViewKind kind = AndroidViewKind::Unknown;
    std::string type_name;
    std::string text;
    std::string hint;
    std::string content_description;
    std::string src;
    std::string on_click;
    std::string orientation = "vertical";
    std::string background_ref;
    std::string text_color_ref;
    double width = 0.0;
    double height = 0.0;
    double weight = 0.0;
    double padding = 0.0;
    double value = 0.0;
    double min_value = 0.0;
    double max_value = 100.0;
    bool enabled = true;
    bool visible = true;
    bool checked = false;
    std::vector<AndroidViewNode> children;
};

class AndroidUiEngine {
public:
    using EventHandler = std::function<void(const std::string& handler,
                                            const std::string& view_id,
                                            const std::string& payload)>;
    using LifecycleHandler = std::function<bool(const std::string& phase)>;

    AndroidUiEngine();
    ~AndroidUiEngine();
    AndroidUiEngine(const AndroidUiEngine&) = delete;
    AndroidUiEngine& operator=(const AndroidUiEngine&) = delete;

    static bool qt_backend_available();

    void set_title(const std::string& title);
    const std::string& title() const { return title_; }
    void set_resource_root(const std::string& dir) { resource_root_ = dir; }
    const std::string& resource_root() const { return resource_root_; }

    // Inflates a real binary Android layout (res/layout/*.xml AXML) using the
    // APK's resources.arsc. Unresolvable @string/@drawable refs stay empty.
    bool inflate_binary_layout(const std::vector<std::uint8_t>& axml_bytes,
                               const ArscInfo& arsc,
                               std::vector<std::string>* warnings = nullptr,
                               std::string* error = nullptr);
    // A small JSON descriptor used by FinnOS-side test apps and by diagnose
    // previews. Not an Android format; kept separate from binary inflation.
    bool inflate_json(std::string_view json_text, std::string* error = nullptr);

    bool has_root() const { return has_root_; }
    const AndroidViewNode& root() const { return root_; }
    AndroidViewNode* find_node(const std::string& id);

    void set_state(const std::string& key, const std::string& value);
    std::string get_state(const std::string& key,
                          const std::string& fallback = {}) const;
    std::string resolve_text(const AndroidViewNode& node) const;

    void set_event_handler(EventHandler handler) { event_handler_ = std::move(handler); }
    void set_lifecycle_handler(LifecycleHandler handler) {
        lifecycle_handler_ = std::move(handler);
    }

    bool trigger_click(const std::string& view_id);
    // Dispatches a named event to the installed handler (used by Qt signals).
    void emit_event(const std::string& handler, const std::string& view_id,
                    const std::string& payload);
    bool set_text(const std::string& view_id, const std::string& value);
    std::string get_text(const std::string& view_id) const;

    void show_alert(const std::string& title, const std::string& message);
    const std::vector<std::string>& alerts() const { return alerts_; }
    const std::vector<std::string>& lifecycle_log() const { return lifecycle_log_; }
    const std::vector<std::string>& unsupported_views() const { return unsupported_views_; }

    void mount();
    void unmount();
    bool is_mounted() const { return mounted_; }

    // Fires onCreate -> onStart -> onResume -> onPause -> onStop -> onDestroy.
    // Each phase is delegated to the lifecycle handler; a false return means
    // the runtime did not actually execute that method and is logged as skipped.
    void run_activity_lifecycle(const std::vector<std::string>& phases);

    // Renders a live Qt6/Wayland window when compiled with FINN_ANDROID_QT6
    // and !headless, otherwise prints an honest headless tree summary.
    int run_window(int argc, char** argv, bool headless = false);
    std::string render_headless_summary() const;

private:
    void fire_lifecycle_recursive(const AndroidViewNode& node, bool mounting);
    void sync_qt_bindings();
    void apply_inline_state_action(std::string_view action);

    std::string title_ = "Android App";
    std::string resource_root_;
    AndroidViewNode root_;
    bool has_root_ = false;
    bool mounted_ = false;
    std::unordered_map<std::string, std::string> state_;
    EventHandler event_handler_;
    LifecycleHandler lifecycle_handler_;
    std::vector<std::string> alerts_;
    std::vector<std::string> lifecycle_log_;
    std::vector<std::string> unsupported_views_;

    struct QtState;
    QtState* qt_ = nullptr;
};

}  // namespace finn::android

#endif  // FINN_ANDROID_UI_BRIDGE_HPP
