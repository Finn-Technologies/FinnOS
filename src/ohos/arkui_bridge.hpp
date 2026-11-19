// Clean-room OpenHarmony 6.1 LTS (API Level 23) ArkUI declarative engine and
// Qt6/Wayland widget bridge for finn-ohos.
//
// Supports Column, Row, Stack, Flex, Scroll, Text, Button, TextInput, Image,
// Toggle, Slider, Progress, List/ListItem, Grid/GridItem (with API 23
// multi-select state), Navigation, Divider, Blank, AlertDialog, reactive
// @State bindings, and API 23 custom component lifecycle hooks (aboutToAppear,
// onAttach, onDetach, aboutToDisappear).

#ifndef FINN_OHOS_ARKUI_BRIDGE_HPP
#define FINN_OHOS_ARKUI_BRIDGE_HPP

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finn::ohos {

enum class ArkComponentKind {
    Column,
    Row,
    Stack,
    Flex,
    Scroll,
    Text,
    Button,
    TextInput,
    Image,
    Toggle,
    Slider,
    Progress,
    List,
    ListItem,
    Grid,
    GridItem,
    Navigation,
    Divider,
    Blank,
    AlertDialog,
    Tabs,
    TabContent,
    Swiper,
    RelativeContainer,
    Custom,
};

enum class ComponentLifecycleEvent {
    AboutToAppear,
    OnAttach,          // OpenHarmony 6.1 LTS (API 23)
    OnDetach,          // OpenHarmony 6.1 LTS (API 23)
    AboutToDisappear,
};

struct ArkUiNode {
    std::string id;
    ArkComponentKind kind = ArkComponentKind::Column;
    std::string type_name = "Column";
    std::string text;
    std::string placeholder;
    std::string state_key;
    std::string image_src;
    std::string on_click;
    std::string on_change;
    std::string background_color;
    std::string font_color;
    double font_size = 16.0;
    double width = 0.0;
    double height = 0.0;
    double padding = 0.0;
    double spacing = 8.0;
    double value = 0.0;
    double min_value = 0.0;
    double max_value = 100.0;
    int columns = 2;
    bool checked = false;
    bool multi_select = false;  // OpenHarmony 6.1 LTS (API 23) Grid multi-select
    bool enabled = true;
    bool visible = true;
    std::vector<int> selected_indices;
    std::vector<ArkUiNode> children;
};

ArkComponentKind parse_ark_component_kind(std::string_view name);
std::string ark_component_kind_name(ArkComponentKind kind);
std::string lifecycle_event_name(ComponentLifecycleEvent ev);

class ArkUiEngine {
public:
    using EventHandler = std::function<void(const std::string& handler_name,
                                            const std::string& node_id,
                                            const std::string& payload)>;
    using LifecycleHandler = std::function<void(ComponentLifecycleEvent event,
                                                const std::string& node_id)>;

    ArkUiEngine();
    ~ArkUiEngine();

    ArkUiEngine(const ArkUiEngine&) = delete;
    ArkUiEngine& operator=(const ArkUiEngine&) = delete;

    // Returns true when built with FINN_OHOS_QT6 live Qt6/Wayland rendering.
    static bool qt_backend_available();

    void set_title(const std::string& title);
    const std::string& title() const { return title_; }

    void set_resource_root(const std::string& dir) { resource_root_ = dir; }
    const std::string& resource_root() const { return resource_root_; }

    void set_root(ArkUiNode root);
    const ArkUiNode& root() const { return root_; }
    ArkUiNode* find_node(const std::string& id);
    const ArkUiNode* find_node(const std::string& id) const;

    // Loads a declarative ArkUI JSON/JSON5 tree (with optional "title" and "state").
    bool load_from_json(std::string_view json_text, std::string* error = nullptr);

    // Reactive @State store. Triggers live Qt6 widget updates when mounted.
    void set_state(const std::string& key, const std::string& value);
    std::string get_state(const std::string& key,
                          const std::string& fallback = {}) const;
    const std::unordered_map<std::string, std::string>& all_state() const {
        return state_;
    }

    // Expands {{key}} templates in node.text against reactive @State values.
    std::string resolve_text(const ArkUiNode& node) const;

    // Resolves an OpenHarmony resource reference ($media:name, $rawfile:name,
    // $r('app.media.name')) against real files under resource_root(). Name-based
    // lookup is used; numeric resources.index IDs are not decoded. Returns an
    // empty string when no real file exists — never a fabricated path.
    std::string resolve_resource_path(const std::string& resource_id) const;
    // Resolves an Image node's src to an existing file, or "" when missing.
    std::string resolve_image_src(const ArkUiNode& node) const;

    void set_event_handler(EventHandler handler) { event_handler_ = std::move(handler); }
    void set_lifecycle_handler(LifecycleHandler handler) {
        lifecycle_handler_ = std::move(handler);
    }

    // Dispatches component interactions (shared by headless tests and Qt6 signals).
    bool trigger_click(const std::string& node_id);
    bool trigger_change(const std::string& node_id, const std::string& new_value);
    bool toggle_grid_item_selection(const std::string& grid_id, int item_index);

    void show_alert(const std::string& alert_title, const std::string& message);
    const std::vector<std::string>& alerts() const { return alerts_; }
    const std::vector<std::string>& lifecycle_log() const { return lifecycle_log_; }

    // Fires aboutToAppear -> onAttach on mount, and onDetach -> aboutToDisappear on unmount.
    void mount();
    void unmount();
    bool is_mounted() const { return mounted_; }

    // Renders live Qt6/Wayland window when FINN_OHOS_QT6 is active and !headless;
    // otherwise validates and renders the tree headlessly.
    int run_window(int argc, char** argv, bool headless = false);

    // Produces an honest text summary of the mounted component tree and @State.
    std::string render_headless_summary() const;

private:
    void fire_lifecycle_recursive(const ArkUiNode& node, ComponentLifecycleEvent ev);
    void sync_qt_bindings();
    void apply_inline_state_action(std::string_view action);

    std::string title_ = "OpenHarmony App";
    std::string resource_root_;
    ArkUiNode root_;
    bool has_root_ = false;
    bool mounted_ = false;
    std::unordered_map<std::string, std::string> state_;
    EventHandler event_handler_;
    LifecycleHandler lifecycle_handler_;
    std::vector<std::string> alerts_;
    std::vector<std::string> lifecycle_log_;

    struct QtState;
    QtState* qt_ = nullptr;
};

}  // namespace finn::ohos

#endif
