#include "ohos/arkui_bridge.hpp"

#include "ohos/ohos_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#ifdef FINN_OHOS_QT6
#include "compat/finn_qt_theme.hpp"
#include "ui/finn/window.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#endif

namespace fs = std::filesystem;

namespace finn::ohos {
namespace {

std::string format_number(double v) {
    if (std::floor(v) == v && std::abs(v) < 1e12) {
        return std::to_string(static_cast<long long>(v));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

ArkUiNode* find_node_impl(ArkUiNode& node, const std::string& id) {
    if (!id.empty() && node.id == id) return &node;
    for (auto& ch : node.children) {
        if (auto* found = find_node_impl(ch, id)) return found;
    }
    return nullptr;
}

const ArkUiNode* find_node_const_impl(const ArkUiNode& node, const std::string& id) {
    if (!id.empty() && node.id == id) return &node;
    for (const auto& ch : node.children) {
        if (const auto* found = find_node_const_impl(ch, id)) return found;
    }
    return nullptr;
}

void assign_default_ids(ArkUiNode& node, int* counter) {
    if (node.id.empty()) {
        node.id = node.type_name + "_" + std::to_string((*counter)++);
    }
    for (auto& ch : node.children) {
        assign_default_ids(ch, counter);
    }
}

ArkUiNode parse_node_from_json(const JsonValue& j) {
    ArkUiNode n;
    n.type_name = j.get_string("type", j.get_string("component", "Column"));
    n.kind = parse_ark_component_kind(n.type_name);
    n.id = j.get_string("id");
    n.text = j.get_string("text", j.get_string("label", j.get_string("title")));
    n.placeholder = j.get_string("placeholder");
    n.state_key = j.get_string("stateKey", j.get_string("bind"));
    n.image_src = j.get_string("src", j.get_string("image"));
    n.on_click = j.get_string("onClick");
    n.on_change = j.get_string("onChange");
    n.background_color = j.get_string("backgroundColor");
    n.font_color = j.get_string("fontColor");
    n.font_size = j.get_double("fontSize", 16.0);
    n.width = j.get_double("width", 0.0);
    n.height = j.get_double("height", 0.0);
    n.padding = j.get_double("padding", 0.0);
    n.spacing = j.get_double("spacing", 8.0);
    n.value = j.get_double("value", 0.0);
    n.min_value = j.get_double("min", 0.0);
    n.max_value = j.get_double("max", 100.0);
    n.columns = std::max(1, j.get_int("columns", 2));
    n.checked = j.get_bool("checked", j.get_bool("selected", false));
    n.multi_select = j.get_bool("multiSelect", false);
    n.enabled = j.get_bool("enabled", true);
    n.visible = j.get_bool("visible", true);

    if (const JsonValue* kids = j.get("children");
        kids && kids->kind == JsonValue::Kind::Array) {
        for (const auto& cj : kids->array_val) {
            if (cj.kind == JsonValue::Kind::Object) {
                n.children.push_back(parse_node_from_json(cj));
            }
        }
    }
    return n;
}

void dump_tree_recursive(const ArkUiEngine& eng, const ArkUiNode& node, int depth,
                         std::ostringstream& out) {
    for (int i = 0; i < depth; ++i) out << "  ";
    out << node.type_name << "#" << node.id;
    const std::string resolved = eng.resolve_text(node);
    if (!resolved.empty()) out << " text=\"" << resolved << "\"";
    if (!node.state_key.empty()) {
        out << " @State(" << node.state_key << "=\"" << eng.get_state(node.state_key) << "\")";
    }
    if (node.kind == ArkComponentKind::Toggle) {
        out << (node.checked ? " [ON]" : " [OFF]");
    } else if (node.kind == ArkComponentKind::Slider ||
               node.kind == ArkComponentKind::Progress) {
        out << " value=" << format_number(node.value) << "/" << format_number(node.max_value);
    } else if (node.kind == ArkComponentKind::Grid && node.multi_select) {
        out << " multiSelect=[";
        for (std::size_t i = 0; i < node.selected_indices.size(); ++i) {
            if (i > 0) out << ",";
            out << node.selected_indices[i];
        }
        out << "]";
    } else if (node.kind == ArkComponentKind::Image) {
        const std::string resolved = eng.resolve_image_src(node);
        if (!resolved.empty()) out << " src=\"" << resolved << "\"";
        else out << " [Image missing: " << node.image_src << "]";
    } else if (node.kind == ArkComponentKind::Tabs ||
               node.kind == ArkComponentKind::Swiper) {
        out << " pages=" << node.children.size();
    } else if (node.kind == ArkComponentKind::RelativeContainer) {
        out << " children=" << node.children.size();
    }
    if (!node.enabled) out << " [disabled]";
    out << "\n";
    for (const auto& ch : node.children) {
        dump_tree_recursive(eng, ch, depth + 1, out);
    }
}

}  // namespace

#ifdef FINN_OHOS_QT6
struct QtWidgetBinding {
    std::string node_id;
    QWidget* widget = nullptr;
};
#endif

struct ArkUiEngine::QtState {
#ifdef FINN_OHOS_QT6
    std::vector<QtWidgetBinding> bindings;
    finn::ui::FinnWindow* window = nullptr;
#else
    int unused = 0;
#endif
};

ArkComponentKind parse_ark_component_kind(std::string_view name) {
    if (name == "Column") return ArkComponentKind::Column;
    if (name == "Row") return ArkComponentKind::Row;
    if (name == "Stack") return ArkComponentKind::Stack;
    if (name == "Flex") return ArkComponentKind::Flex;
    if (name == "Scroll") return ArkComponentKind::Scroll;
    if (name == "Text") return ArkComponentKind::Text;
    if (name == "Button") return ArkComponentKind::Button;
    if (name == "TextInput") return ArkComponentKind::TextInput;
    if (name == "Image") return ArkComponentKind::Image;
    if (name == "Toggle") return ArkComponentKind::Toggle;
    if (name == "Slider") return ArkComponentKind::Slider;
    if (name == "Progress") return ArkComponentKind::Progress;
    if (name == "List") return ArkComponentKind::List;
    if (name == "ListItem") return ArkComponentKind::ListItem;
    if (name == "Grid") return ArkComponentKind::Grid;
    if (name == "GridItem") return ArkComponentKind::GridItem;
    if (name == "Navigation") return ArkComponentKind::Navigation;
    if (name == "Divider") return ArkComponentKind::Divider;
    if (name == "Blank") return ArkComponentKind::Blank;
    if (name == "AlertDialog") return ArkComponentKind::AlertDialog;
    if (name == "Tabs") return ArkComponentKind::Tabs;
    if (name == "TabContent") return ArkComponentKind::TabContent;
    if (name == "Swiper") return ArkComponentKind::Swiper;
    if (name == "RelativeContainer") return ArkComponentKind::RelativeContainer;
    return ArkComponentKind::Custom;
}

std::string ark_component_kind_name(ArkComponentKind kind) {
    switch (kind) {
        case ArkComponentKind::Column: return "Column";
        case ArkComponentKind::Row: return "Row";
        case ArkComponentKind::Stack: return "Stack";
        case ArkComponentKind::Flex: return "Flex";
        case ArkComponentKind::Scroll: return "Scroll";
        case ArkComponentKind::Text: return "Text";
        case ArkComponentKind::Button: return "Button";
        case ArkComponentKind::TextInput: return "TextInput";
        case ArkComponentKind::Image: return "Image";
        case ArkComponentKind::Toggle: return "Toggle";
        case ArkComponentKind::Slider: return "Slider";
        case ArkComponentKind::Progress: return "Progress";
        case ArkComponentKind::List: return "List";
        case ArkComponentKind::ListItem: return "ListItem";
        case ArkComponentKind::Grid: return "Grid";
        case ArkComponentKind::GridItem: return "GridItem";
        case ArkComponentKind::Navigation: return "Navigation";
        case ArkComponentKind::Divider: return "Divider";
        case ArkComponentKind::Blank: return "Blank";
        case ArkComponentKind::AlertDialog: return "AlertDialog";
        case ArkComponentKind::Tabs: return "Tabs";
        case ArkComponentKind::TabContent: return "TabContent";
        case ArkComponentKind::Swiper: return "Swiper";
        case ArkComponentKind::RelativeContainer: return "RelativeContainer";
        case ArkComponentKind::Custom: break;
    }
    return "Custom";
}

std::string lifecycle_event_name(ComponentLifecycleEvent ev) {
    switch (ev) {
        case ComponentLifecycleEvent::AboutToAppear: return "aboutToAppear";
        case ComponentLifecycleEvent::OnAttach: return "onAttach";
        case ComponentLifecycleEvent::OnDetach: return "onDetach";
        case ComponentLifecycleEvent::AboutToDisappear: return "aboutToDisappear";
    }
    return "unknown";
}

ArkUiEngine::ArkUiEngine() : qt_(new QtState()) {}

ArkUiEngine::~ArkUiEngine() {
    if (mounted_) unmount();
    delete qt_;
    qt_ = nullptr;
}

bool ArkUiEngine::qt_backend_available() {
#ifdef FINN_OHOS_QT6
    return true;
#else
    return false;
#endif
}

void ArkUiEngine::set_title(const std::string& title) {
    title_ = title;
#ifdef FINN_OHOS_QT6
    if (qt_ && qt_->window) {
        qt_->window->setWindowTitleText(QString::fromStdString(title_));
    }
#endif
}

void ArkUiEngine::set_root(ArkUiNode root) {
    if (mounted_) unmount();
    int counter = 1;
    assign_default_ids(root, &counter);
    root_ = std::move(root);
    has_root_ = true;
}

ArkUiNode* ArkUiEngine::find_node(const std::string& id) {
    if (!has_root_) return nullptr;
    return find_node_impl(root_, id);
}

const ArkUiNode* ArkUiEngine::find_node(const std::string& id) const {
    if (!has_root_) return nullptr;
    return find_node_const_impl(root_, id);
}

bool ArkUiEngine::load_from_json(std::string_view json_text, std::string* error) {
    auto parsed = parse_json5(json_text, error);
    if (!parsed || parsed->kind != JsonValue::Kind::Object) {
        if (error && error->empty()) *error = "ArkUI document root must be a JSON object";
        return false;
    }

    if (const std::string t = parsed->get_string("title"); !t.empty()) {
        title_ = t;
    }

    if (const JsonValue* st = parsed->get("state");
        st && st->kind == JsonValue::Kind::Object) {
        for (const auto& [k, v] : st->object_val) {
            if (v.kind == JsonValue::Kind::String) {
                state_[k] = v.str_val;
            } else if (v.kind == JsonValue::Kind::Number) {
                state_[k] = format_number(v.num_val);
            } else if (v.kind == JsonValue::Kind::Bool) {
                state_[k] = v.bool_val ? "true" : "false";
            }
        }
    }

    const JsonValue* root_obj = parsed->get("root");
    if (!root_obj || root_obj->kind != JsonValue::Kind::Object) {
        root_obj = &(*parsed);
    }

    set_root(parse_node_from_json(*root_obj));
    return true;
}

void ArkUiEngine::set_state(const std::string& key, const std::string& value) {
    state_[key] = value;
    sync_qt_bindings();
}

std::string ArkUiEngine::get_state(const std::string& key,
                                   const std::string& fallback) const {
    const auto it = state_.find(key);
    return (it == state_.end()) ? fallback : it->second;
}

std::string ArkUiEngine::resolve_text(const ArkUiNode& node) const {
    std::string s = node.text;
    if (s.empty() && !node.state_key.empty() &&
        (node.kind == ArkComponentKind::Text || node.kind == ArkComponentKind::TextInput)) {
        return get_state(node.state_key);
    }
    std::size_t pos = 0;
    while ((pos = s.find("{{", pos)) != std::string::npos) {
        const std::size_t end = s.find("}}", pos + 2);
        if (end == std::string::npos) break;
        const std::string key = s.substr(pos + 2, end - (pos + 2));
        const std::string val = get_state(key);
        s.replace(pos, (end + 2) - pos, val);
        pos += val.size();
    }
    return s;
}

std::string ArkUiEngine::resolve_resource_path(const std::string& resource_id) const {
    if (resource_id.empty() || resource_root_.empty()) return {};

    enum class ResType { Media, Rawfile, Path } type = ResType::Path;
    std::string name = resource_id;

    if (resource_id.rfind("$media:", 0) == 0) {
        type = ResType::Media;
        name = resource_id.substr(7);
    } else if (resource_id.rfind("$rawfile:", 0) == 0) {
        type = ResType::Rawfile;
        name = resource_id.substr(9);
    } else if (resource_id.rfind("$r(", 0) == 0) {
        // $r('app.media.icon') / $r("app.rawfile.data.txt")
        const std::size_t q1 = resource_id.find_first_of("'\"");
        const std::size_t q2 = (q1 == std::string::npos)
                                   ? std::string::npos
                                   : resource_id.find(resource_id[q1], q1 + 1);
        std::string inner = (q1 != std::string::npos && q2 != std::string::npos)
                                ? resource_id.substr(q1 + 1, q2 - q1 - 1)
                                : resource_id.substr(3);
        const std::size_t last_dot = inner.rfind('.');
        const std::size_t prev_dot = (last_dot == std::string::npos)
                                         ? std::string::npos
                                         : inner.rfind('.', last_dot - 1);
        if (last_dot != std::string::npos && prev_dot != std::string::npos) {
            const std::string qualifier = inner.substr(prev_dot + 1, last_dot - prev_dot - 1);
            name = inner.substr(last_dot + 1);
            if (qualifier == "rawfile") type = ResType::Rawfile;
            else if (qualifier == "media") type = ResType::Media;
        } else {
            name = inner;
        }
    }

    if (name.empty()) return {};
    const fs::path root(resource_root_);
    std::error_code ec;

    if (type == ResType::Path) {
        const fs::path p = fs::path(name).is_absolute() ? fs::path(name) : (root / name);
        return fs::is_regular_file(p, ec) ? p.string() : std::string();
    }
    if (type == ResType::Rawfile) {
        const fs::path p = root / "resources" / "rawfile" / name;
        return fs::is_regular_file(p, ec) ? p.string() : std::string();
    }

    // Media resources are referenced by stem, not by numeric resources.index id.
    // Search every real resource qualifier directory, deterministically.
    const fs::path res_root = root / "resources";
    std::vector<fs::path> media_dirs;
    if (fs::is_directory(res_root, ec)) {
        for (fs::directory_iterator it(res_root, ec), end;
             it != end && !ec; it.increment(ec)) {
            const fs::path media = it->path() / "media";
            std::error_code sub_ec;
            if (fs::is_directory(media, sub_ec)) media_dirs.push_back(media);
        }
    }
    std::sort(media_dirs.begin(), media_dirs.end());
    for (const auto& dir : media_dirs) {
        std::error_code walk_ec;
        for (fs::directory_iterator it(dir, walk_ec), end;
             it != end && !walk_ec; it.increment(walk_ec)) {
            if (!fs::is_regular_file(it->path(), walk_ec)) continue;
            if (it->path().stem() == name) return it->path().string();
        }
    }
    return {};  // missing resource: report absent, never invent a path
}

std::string ArkUiEngine::resolve_image_src(const ArkUiNode& node) const {
    if (node.kind != ArkComponentKind::Image || node.image_src.empty()) return {};
    return resolve_resource_path(node.image_src);
}

void ArkUiEngine::apply_inline_state_action(std::string_view action) {
    if (action.rfind("state:", 0) != 0) return;
    action.remove_prefix(6);
    if (const std::size_t plus_eq = action.find("+="); plus_eq != std::string_view::npos) {
        const std::string key(action.substr(0, plus_eq));
        const double delta = std::strtod(std::string(action.substr(plus_eq + 2)).c_str(), nullptr);
        const double cur = std::strtod(get_state(key, "0").c_str(), nullptr);
        set_state(key, format_number(cur + delta));
        return;
    }
    if (const std::size_t eq = action.find('='); eq != std::string_view::npos) {
        const std::string key(action.substr(0, eq));
        const std::string val(action.substr(eq + 1));
        set_state(key, val);
    }
}

bool ArkUiEngine::trigger_click(const std::string& node_id) {
    ArkUiNode* node = find_node(node_id);
    if (!node || !node->enabled) return false;
    if (node->kind == ArkComponentKind::AlertDialog) {
        show_alert(node->text.empty() ? title_ : resolve_text(*node),
                   node->placeholder);
        return true;
    }
    if (!node->on_click.empty()) {
        if (node->on_click.rfind("state:", 0) == 0) {
            apply_inline_state_action(node->on_click);
        } else if (event_handler_) {
            event_handler_(node->on_click, node->id, resolve_text(*node));
        }
    }
    sync_qt_bindings();
    return true;
}

bool ArkUiEngine::trigger_change(const std::string& node_id,
                                 const std::string& new_value) {
    ArkUiNode* node = find_node(node_id);
    if (!node || !node->enabled) return false;
    if (node->kind == ArkComponentKind::Toggle) {
        node->checked = (new_value == "true" || new_value == "1" || new_value == "on");
        node->value = node->checked ? 1.0 : 0.0;
    } else if (node->kind == ArkComponentKind::Slider ||
               node->kind == ArkComponentKind::Progress) {
        node->value = std::clamp(std::strtod(new_value.c_str(), nullptr),
                                 node->min_value, node->max_value);
    } else if (node->kind == ArkComponentKind::TextInput) {
        node->text = new_value;
    }

    if (!node->state_key.empty()) {
        set_state(node->state_key, new_value);
    }
    if (!node->on_change.empty() && event_handler_) {
        event_handler_(node->on_change, node->id, new_value);
    }
    sync_qt_bindings();
    return true;
}

bool ArkUiEngine::toggle_grid_item_selection(const std::string& grid_id,
                                             int item_index) {
    ArkUiNode* grid = find_node(grid_id);
    if (!grid || grid->kind != ArkComponentKind::Grid || !grid->enabled) {
        return false;
    }
    if (item_index < 0 || static_cast<std::size_t>(item_index) >= grid->children.size()) {
        return false;
    }

    auto& sel = grid->selected_indices;
    const auto it = std::find(sel.begin(), sel.end(), item_index);
    if (grid->multi_select) {
        if (it != sel.end()) {
            sel.erase(it);
            grid->children[static_cast<std::size_t>(item_index)].checked = false;
        } else {
            sel.push_back(item_index);
            std::sort(sel.begin(), sel.end());
            grid->children[static_cast<std::size_t>(item_index)].checked = true;
        }
    } else {
        for (auto& ch : grid->children) ch.checked = false;
        sel.clear();
        sel.push_back(item_index);
        grid->children[static_cast<std::size_t>(item_index)].checked = true;
    }

    std::string joined;
    for (std::size_t i = 0; i < sel.size(); ++i) {
        if (i > 0) joined += ",";
        joined += std::to_string(sel[i]);
    }
    if (!grid->state_key.empty()) set_state(grid->state_key, joined);
    if (!grid->on_change.empty() && event_handler_) {
        event_handler_(grid->on_change, grid->id, joined);
    }
    sync_qt_bindings();
    return true;
}

void ArkUiEngine::show_alert(const std::string& alert_title,
                             const std::string& message) {
    alerts_.push_back(alert_title + ": " + message);
#ifdef FINN_OHOS_QT6
    if (qt_ && qt_->window) {
        QMessageBox::information(qt_->window,
                                 QString::fromStdString(alert_title),
                                 QString::fromStdString(message));
    }
#endif
}

void ArkUiEngine::fire_lifecycle_recursive(const ArkUiNode& node,
                                           ComponentLifecycleEvent ev) {
    lifecycle_log_.push_back(lifecycle_event_name(ev) + ":" + node.id);
    if (lifecycle_handler_) lifecycle_handler_(ev, node.id);
    for (const auto& ch : node.children) {
        fire_lifecycle_recursive(ch, ev);
    }
}

void ArkUiEngine::mount() {
    if (!has_root_ || mounted_) return;
    mounted_ = true;
    fire_lifecycle_recursive(root_, ComponentLifecycleEvent::AboutToAppear);
    fire_lifecycle_recursive(root_, ComponentLifecycleEvent::OnAttach);
}

void ArkUiEngine::unmount() {
    if (!has_root_ || !mounted_) return;
    fire_lifecycle_recursive(root_, ComponentLifecycleEvent::OnDetach);
    fire_lifecycle_recursive(root_, ComponentLifecycleEvent::AboutToDisappear);
    mounted_ = false;
}

void ArkUiEngine::sync_qt_bindings() {
#ifdef FINN_OHOS_QT6
    if (!qt_) return;
    for (const auto& b : qt_->bindings) {
        const ArkUiNode* node = find_node(b.node_id);
        if (!node || !b.widget) continue;
        if (auto* lbl = qobject_cast<QLabel*>(b.widget)) {
            if (node->kind != ArkComponentKind::Image) {
                lbl->setText(QString::fromStdString(resolve_text(*node)));
            }
        } else if (auto* btn = qobject_cast<QPushButton*>(b.widget)) {
            btn->setText(QString::fromStdString(resolve_text(*node)));
        } else if (auto* pb = qobject_cast<QProgressBar*>(b.widget)) {
            double v = node->value;
            if (!node->state_key.empty()) {
                v = std::strtod(get_state(node->state_key, format_number(v)).c_str(), nullptr);
            }
            pb->setValue(static_cast<int>(std::lround(v)));
        } else if (auto* cb = qobject_cast<QCheckBox*>(b.widget)) {
            cb->blockSignals(true);
            cb->setChecked(node->checked);
            cb->blockSignals(false);
        }
    }
#endif
}

#ifdef FINN_OHOS_QT6
namespace {

QWidget* build_qt_node(ArkUiEngine* engine,
                       std::vector<QtWidgetBinding>* bindings,
                       const ArkUiNode& node,
                       QWidget* parent) {
    switch (node.kind) {
        case ArkComponentKind::Column:
        case ArkComponentKind::Navigation: {
            auto* w = new QWidget(parent);
            auto* lay = new QVBoxLayout(w);
            lay->setSpacing(static_cast<int>(node.spacing));
            if (node.padding > 0) {
                const int p = static_cast<int>(node.padding);
                lay->setContentsMargins(p, p, p, p);
            }
            if (node.kind == ArkComponentKind::Navigation && !node.text.empty()) {
                auto* hdr = new QLabel(QString::fromStdString(engine->resolve_text(node)), w);
                QFont f = hdr->font();
                f.setPointSize(18);
                f.setBold(true);
                hdr->setFont(f);
                lay->addWidget(hdr);
            }
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, w));
            }
            return w;
        }
        case ArkComponentKind::Row:
        case ArkComponentKind::Flex: {
            auto* w = new QWidget(parent);
            auto* lay = new QHBoxLayout(w);
            lay->setSpacing(static_cast<int>(node.spacing));
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, w));
            }
            return w;
        }
        case ArkComponentKind::Stack: {
            auto* sw = new QStackedWidget(parent);
            for (const auto& ch : node.children) {
                sw->addWidget(build_qt_node(engine, bindings, ch, sw));
            }
            return sw;
        }
        case ArkComponentKind::Scroll: {
            auto* sc = new QScrollArea(parent);
            sc->setWidgetResizable(true);
            auto* inner = new QWidget(sc);
            auto* lay = new QVBoxLayout(inner);
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, inner));
            }
            sc->setWidget(inner);
            return sc;
        }
        case ArkComponentKind::Text: {
            auto* lbl = new QLabel(QString::fromStdString(engine->resolve_text(node)), parent);
            lbl->setWordWrap(true);
            QFont f = lbl->font();
            if (node.font_size > 0) f.setPointSizeF(node.font_size);
            lbl->setFont(f);
            bindings->push_back({node.id, lbl});
            return lbl;
        }
        case ArkComponentKind::Button: {
            auto* btn = new QPushButton(QString::fromStdString(engine->resolve_text(node)), parent);
            btn->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(btn, &QPushButton::clicked, [engine, id]() {
                engine->trigger_click(id);
            });
            bindings->push_back({node.id, btn});
            return btn;
        }
        case ArkComponentKind::TextInput: {
            auto* le = new QLineEdit(parent);
            le->setText(QString::fromStdString(engine->resolve_text(node)));
            if (!node.placeholder.empty()) {
                le->setPlaceholderText(QString::fromStdString(node.placeholder));
            }
            le->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(le, &QLineEdit::textChanged, [engine, id](const QString& txt) {
                engine->trigger_change(id, txt.toStdString());
            });
            bindings->push_back({node.id, le});
            return le;
        }
        case ArkComponentKind::Image: {
            auto* lbl = new QLabel(parent);
            const std::string resolved = engine->resolve_image_src(node);
            if (!resolved.empty()) {
                QPixmap px(QString::fromStdString(resolved));
                if (!px.isNull()) {
                    const int w = node.width > 0 ? static_cast<int>(node.width) : 64;
                    const int h = node.height > 0 ? static_cast<int>(node.height) : 64;
                    lbl->setPixmap(px.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                } else {
                    lbl->setText(QString::fromStdString("[Image decode failed: " + resolved + "]"));
                }
            } else if (!node.image_src.empty()) {
                lbl->setText(QString::fromStdString("[Image missing: " + node.image_src + "]"));
            }
            return lbl;
        }
        case ArkComponentKind::Toggle: {
            auto* cb = new QCheckBox(QString::fromStdString(engine->resolve_text(node)), parent);
            cb->setChecked(node.checked);
            cb->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(cb, &QCheckBox::toggled, [engine, id](bool checked) {
                engine->trigger_change(id, checked ? "true" : "false");
            });
            bindings->push_back({node.id, cb});
            return cb;
        }
        case ArkComponentKind::Slider: {
            auto* sl = new QSlider(Qt::Horizontal, parent);
            sl->setRange(static_cast<int>(node.min_value), static_cast<int>(node.max_value));
            sl->setValue(static_cast<int>(node.value));
            sl->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(sl, &QSlider::valueChanged, [engine, id](int val) {
                engine->trigger_change(id, std::to_string(val));
            });
            return sl;
        }
        case ArkComponentKind::Progress: {
            auto* pb = new QProgressBar(parent);
            pb->setRange(static_cast<int>(node.min_value), static_cast<int>(node.max_value));
            pb->setValue(static_cast<int>(node.value));
            bindings->push_back({node.id, pb});
            return pb;
        }
        case ArkComponentKind::List: {
            auto* lw = new QListWidget(parent);
            for (const auto& ch : node.children) {
                lw->addItem(QString::fromStdString(engine->resolve_text(ch)));
            }
            return lw;
        }
        case ArkComponentKind::Grid: {
            auto* w = new QWidget(parent);
            auto* gl = new QGridLayout(w);
            const int cols = std::max(1, node.columns);
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                const auto& item = node.children[i];
                auto* tile = new QPushButton(QString::fromStdString(engine->resolve_text(item)), w);
                tile->setCheckable(true);
                tile->setChecked(item.checked);
                const std::string grid_id = node.id;
                const int idx = static_cast<int>(i);
                QObject::connect(tile, &QPushButton::clicked, [engine, grid_id, idx]() {
                    engine->toggle_grid_item_selection(grid_id, idx);
                });
                gl->addWidget(tile, idx / cols, idx % cols);
            }
            return w;
        }
        case ArkComponentKind::Divider: {
            auto* f = new QFrame(parent);
            f->setFrameShape(QFrame::HLine);
            f->setFrameShadow(QFrame::Sunken);
            return f;
        }
        case ArkComponentKind::Blank: {
            auto* w = new QWidget(parent);
            w->setMinimumSize(16, 16);
            return w;
        }
        case ArkComponentKind::AlertDialog: {
            auto* btn = new QPushButton(QString::fromStdString(engine->resolve_text(node)), parent);
            const std::string id = node.id;
            QObject::connect(btn, &QPushButton::clicked, [engine, id]() {
                engine->trigger_click(id);
            });
            return btn;
        }
        case ArkComponentKind::Tabs: {
            auto* tabs = new QTabWidget(parent);
            for (const auto& ch : node.children) {
                QWidget* page = build_qt_node(engine, bindings, ch, tabs);
                const std::string label =
                    ch.text.empty() ? ch.type_name : engine->resolve_text(ch);
                tabs->addTab(page, QString::fromStdString(label));
            }
            return tabs;
        }
        case ArkComponentKind::TabContent: {
            auto* w = new QWidget(parent);
            auto* lay = new QVBoxLayout(w);
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, w));
            }
            return w;
        }
        case ArkComponentKind::Swiper: {
            auto* pages = new QStackedWidget(parent);
            for (const auto& ch : node.children) {
                pages->addWidget(build_qt_node(engine, bindings, ch, pages));
            }
            return pages;
        }
        case ArkComponentKind::RelativeContainer: {
            // ArkUI relative positioning needs constraints the clean-room Qt
            // bridge does not evaluate; lay children out in declaration order
            // rather than pretending to honour relative offsets.
            auto* w = new QWidget(parent);
            auto* lay = new QVBoxLayout(w);
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, w));
            }
            return w;
        }
        case ArkComponentKind::ListItem:
        case ArkComponentKind::GridItem:
        case ArkComponentKind::Custom:
            break;
    }
    auto* fallback = new QLabel(QString::fromStdString(engine->resolve_text(node)), parent);
    return fallback;
}

}  // namespace
#endif

int ArkUiEngine::run_window(int argc, char** argv, bool headless) {
    if (!has_root_) return 1;
    if (!mounted_) mount();

#ifdef FINN_OHOS_QT6
    if (!headless) {
        int qt_argc = argc;
        char** qt_argv = argv;
        static char fallback_prog[] = "finn-ohos";
        static char* fallback_argv[] = {fallback_prog, nullptr};
        if (qt_argc <= 0 || !qt_argv) {
            qt_argc = 1;
            qt_argv = fallback_argv;
        }
        QApplication app(qt_argc, qt_argv);
        // Use the same application and window chrome as native FinnOS apps so
        // an ArkUI guest does not look like a generic Qt program.
        finn::compat::apply_finn_qt_theme(&app);
        finn::ui::FinnWindow win(QString::fromStdString(title_),
                                 QStringLiteral("ohos"));
        win.resize(520, 680);
        qt_->window = &win;
        qt_->bindings.clear();
        QWidget* central = build_qt_node(this, &qt_->bindings, root_, &win);
        win.setContentWidget(central);
        win.show();
        const int rc = app.exec();
        qt_->window = nullptr;
        qt_->bindings.clear();
        unmount();
        return rc;
    }
#else
    (void)argc;
    (void)argv;
    (void)headless;
#endif

    // Headless render & validation pass: prints real component tree without faking a GUI.
    const std::string summary = render_headless_summary();
    std::fwrite(summary.data(), 1, summary.size(), stdout);
    return 0;
}

std::string ArkUiEngine::render_headless_summary() const {
    std::ostringstream out;
    out << "ArkUI Window: " << title_ << "\n";
    if (!state_.empty()) {
        out << "  @State:";
        std::vector<std::string> keys;
        keys.reserve(state_.size());
        for (const auto& [k, _] : state_) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) {
            out << " " << k << "=\"" << state_.at(k) << "\"";
        }
        out << "\n";
    }
    if (has_root_) {
        dump_tree_recursive(*this, root_, 1, out);
    } else {
        out << "  (no root component mounted)\n";
    }
    return out.str();
}

}  // namespace finn::ohos
