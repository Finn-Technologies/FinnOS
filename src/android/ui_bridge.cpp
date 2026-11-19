#include "android/ui_bridge.hpp"

#include "android/apk_format.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>

#ifdef FINN_ANDROID_QT6
#include "compat/finn_qt_theme.hpp"
#include "ui/finn/components.hpp"
#include "ui/finn/window.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QHBoxLayout>
#include <QLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#endif

namespace fs = std::filesystem;

namespace finn::android {
namespace {

std::string format_number(double v) {
    if (std::floor(v) == v && std::abs(v) < 1e12) {
        return std::to_string(static_cast<long long>(v));
    }
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// A small JSON DOM for the FinnOS-side descriptor. This is deliberately not an
// Android format; binary layouts are handled by inflate_binary_layout().
struct UiJson {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<UiJson> array_val;
    std::map<std::string, UiJson> object_val;

    const UiJson* get(const std::string& key) const {
        if (kind != Kind::Object) return nullptr;
        const auto it = object_val.find(key);
        return it == object_val.end() ? nullptr : &it->second;
    }
    std::string get_string(const std::string& key,
                           const std::string& def = {}) const {
        const UiJson* v = get(key);
        return (v && v->kind == Kind::String) ? v->str_val : def;
    }
    double get_double(const std::string& key, double def = 0.0) const {
        const UiJson* v = get(key);
        return (v && v->kind == Kind::Number) ? v->num_val : def;
    }
    bool get_bool(const std::string& key, bool def = false) const {
        const UiJson* v = get(key);
        return (v && v->kind == Kind::Bool) ? v->bool_val : def;
    }
};

class UiJsonParser {
public:
    explicit UiJsonParser(std::string_view src) : src_(src) {}

    bool parse(UiJson* out, std::string* error) {
        skip_ws();
        if (!parse_value(0, out)) {
            if (error) *error = err_.empty() ? "invalid UI JSON" : err_;
            return false;
        }
        skip_ws();
        if (pos_ != src_.size()) {
            if (error) *error = "trailing characters after UI JSON document";
            return false;
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 48;

    void skip_ws() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    bool parse_value(int depth, UiJson* out) {
        if (depth > kMaxDepth) {
            err_ = "UI JSON nesting limit exceeded";
            return false;
        }
        skip_ws();
        if (pos_ >= src_.size()) {
            err_ = "unexpected end of UI JSON";
            return false;
        }
        const char c = src_[pos_];
        if (c == '{') return parse_object(depth + 1, out);
        if (c == '[') return parse_array(depth + 1, out);
        if (c == '"') {
            auto s = parse_string();
            if (!s) return false;
            out->kind = UiJson::Kind::String;
            out->str_val = std::move(*s);
            return true;
        }
        if (src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            out->kind = UiJson::Kind::Bool;
            out->bool_val = true;
            return true;
        }
        if (src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            out->kind = UiJson::Kind::Bool;
            out->bool_val = false;
            return true;
        }
        if (src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            out->kind = UiJson::Kind::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t start = pos_;
            if (src_[pos_] == '-') ++pos_;
            while (pos_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
            if (pos_ < src_.size() && src_[pos_] == '.') {
                ++pos_;
                while (pos_ < src_.size() &&
                       std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
            }
            if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < src_.size() && (src_[pos_] == '-' || src_[pos_] == '+')) ++pos_;
                while (pos_ < src_.size() &&
                       std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
            }
            out->kind = UiJson::Kind::Number;
            out->num_val = std::strtod(std::string(src_.substr(start, pos_ - start)).c_str(),
                                       nullptr);
            return true;
        }
        err_ = std::string("unexpected UI JSON token '") + c + "'";
        return false;
    }

    static void append_utf8(std::string* out, std::uint32_t cp) {
        if (cp <= 0x7fu) {
            out->push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ffu) {
            out->push_back(static_cast<char>(0xc0u | (cp >> 6)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else {
            out->push_back(static_cast<char>(0xe0u | (cp >> 12)));
            out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        }
    }

    static int hex_digit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::optional<std::string> parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= src_.size()) break;
            const char e = src_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > src_.size()) return std::nullopt;
                    std::uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int d = hex_digit(src_[pos_++]);
                        if (d < 0) return std::nullopt;
                        cp = (cp << 4) | static_cast<std::uint32_t>(d);
                    }
                    append_utf8(&out, cp);
                    break;
                }
                default: out.push_back(e); break;
            }
        }
        err_ = "unterminated UI JSON string";
        return std::nullopt;
    }

    bool parse_array(int depth, UiJson* out) {
        ++pos_;
        out->kind = UiJson::Kind::Array;
        while (true) {
            skip_ws();
            if (pos_ >= src_.size()) { err_ = "unterminated UI JSON array"; return false; }
            if (src_[pos_] == ']') { ++pos_; return true; }
            UiJson elem;
            if (!parse_value(depth, &elem)) return false;
            out->array_val.push_back(std::move(elem));
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; return true; }
            err_ = "expected ',' or ']' in UI JSON array";
            return false;
        }
    }

    bool parse_object(int depth, UiJson* out) {
        ++pos_;
        out->kind = UiJson::Kind::Object;
        while (true) {
            skip_ws();
            if (pos_ >= src_.size()) { err_ = "unterminated UI JSON object"; return false; }
            if (src_[pos_] == '}') { ++pos_; return true; }
            if (src_[pos_] != '"') { err_ = "expected UI JSON object key"; return false; }
            auto key = parse_string();
            if (!key) return false;
            skip_ws();
            if (pos_ >= src_.size() || src_[pos_] != ':') {
                err_ = "expected ':' after UI JSON key";
                return false;
            }
            ++pos_;
            UiJson val;
            if (!parse_value(depth, &val)) return false;
            out->object_val[std::move(*key)] = std::move(val);
            skip_ws();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; return true; }
            err_ = "expected ',' or '}' in UI JSON object";
            return false;
        }
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    std::string err_;
};

bool parse_ui_json(std::string_view text, UiJson* out, std::string* error) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xefu &&
        static_cast<unsigned char>(text[1]) == 0xbbu &&
        static_cast<unsigned char>(text[2]) == 0xbfu) {
        text.remove_prefix(3);
    }
    return UiJsonParser(text).parse(out, error);
}

std::string short_tag(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    return (dot == std::string_view::npos) ? std::string(name)
                                           : std::string(name.substr(dot + 1));
}

bool parse_bool_value(const std::string& s, bool fallback) {
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return fallback;
}

double parse_dp(const std::string& s) {
    if (s.empty()) return 0.0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0.0;
    return v;
}

AndroidViewNode* find_node_impl(AndroidViewNode& node, const std::string& id) {
    if (!id.empty() && node.id == id) return &node;
    for (auto& ch : node.children) {
        if (auto* found = find_node_impl(ch, id)) return found;
    }
    return nullptr;
}

const AndroidViewNode* find_node_const_impl(const AndroidViewNode& node,
                                            const std::string& id) {
    if (!id.empty() && node.id == id) return &node;
    for (const auto& ch : node.children) {
        if (const auto* found = find_node_const_impl(ch, id)) return found;
    }
    return nullptr;
}

void assign_default_ids(AndroidViewNode& node, int* counter) {
    if (node.id.empty()) {
        node.id = short_tag(node.type_name) + "_" + std::to_string((*counter)++);
    }
    for (auto& ch : node.children) assign_default_ids(ch, counter);
}

// Returns a node through a path of child indices. Used while inflating AXML
// so no raw pointer into a growing vector is ever retained.
AndroidViewNode* node_at_path(AndroidViewNode& root,
                              const std::vector<std::size_t>& path) {
    AndroidViewNode* cur = &root;
    for (std::size_t index : path) {
        if (index >= cur->children.size()) return nullptr;
        cur = &cur->children[index];
    }
    return cur;
}

std::string attr_literal(const AxmlElement& el, const char* name) {
    return el.attr_value(name);
}

// A resource reference resolves to an entry *key*, never to localized text.
// Text attributes therefore stay empty when they are references.
std::string attr_text(const AxmlElement& el, const char* name,
                      std::vector<std::string>* warnings) {
    const std::string raw = el.attr_value(name);
    if (raw.empty()) return {};
    if (raw.front() == '@') {
        if (warnings) {
            warnings->push_back(std::string(name) + " is a compiled resource reference (" +
                                raw + "); its string value is not decoded");
        }
        return {};
    }
    return raw;
}

AndroidViewNode node_from_axml(const AxmlElement& el, const ArscInfo& arsc,
                               std::vector<std::string>* warnings) {
    AndroidViewNode n;
    n.type_name = el.name;
    n.kind = android_view_kind_from_name(el.name);
    n.id = resolve_apk_resource_key(attr_literal(el, "id"), arsc);
    if (n.id.empty()) {
        const std::string raw = attr_literal(el, "id");
        const std::size_t slash = raw.find('/');
        if (slash != std::string::npos) n.id = raw.substr(slash + 1);
    }
    n.text = attr_text(el, "text", warnings);
    n.hint = attr_text(el, "hint", warnings);
    n.content_description = attr_text(el, "contentDescription", warnings);
    n.src = resolve_apk_resource_key(attr_literal(el, "src"), arsc);
    n.on_click = attr_literal(el, "onClick");
    n.orientation = attr_literal(el, "orientation") == "0" ? "horizontal" : "vertical";
    n.background_ref = attr_literal(el, "background");
    n.text_color_ref = attr_literal(el, "textColor");
    n.width = parse_dp(attr_literal(el, "layout_width"));
    n.height = parse_dp(attr_literal(el, "layout_height"));
    n.weight = parse_dp(attr_literal(el, "layout_weight"));
    n.padding = parse_dp(attr_literal(el, "padding"));
    n.value = parse_dp(attr_literal(el, "progress"));
    n.min_value = parse_dp(attr_literal(el, "min"));
    n.max_value = parse_dp(attr_literal(el, "max"));
    n.enabled = parse_bool_value(attr_literal(el, "enabled"), true);
    const std::string visibility = attr_literal(el, "visibility");
    n.visible = visibility.empty() || visibility == "0" || visibility == "visible";
    n.checked = parse_bool_value(attr_literal(el, "checked"), false);
    return n;
}

AndroidViewNode parse_node_from_json(const UiJson& j) {
    AndroidViewNode n;
    n.type_name = j.get_string("type", j.get_string("tag", j.get_string("widget", "LinearLayout")));
    n.kind = android_view_kind_from_name(n.type_name);
    n.id = j.get_string("id");
    n.text = j.get_string("text", j.get_string("label"));
    n.hint = j.get_string("hint");
    n.content_description = j.get_string("contentDescription");
    n.src = j.get_string("src");
    n.on_click = j.get_string("onClick");
    n.orientation = j.get_string("orientation", "vertical");
    n.background_ref = j.get_string("background");
    n.text_color_ref = j.get_string("textColor");
    n.width = j.get_double("width", 0.0);
    n.height = j.get_double("height", 0.0);
    n.weight = j.get_double("weight", 0.0);
    n.padding = j.get_double("padding", 0.0);
    n.value = j.get_double("progress", j.get_double("value", 0.0));
    n.min_value = j.get_double("min", 0.0);
    n.max_value = j.get_double("max", 100.0);
    n.enabled = j.get_bool("enabled", true);
    n.visible = j.get_bool("visible", true);
    n.checked = j.get_bool("checked", false);
    if (const UiJson* kids = j.get("children");
        kids && kids->kind == UiJson::Kind::Array) {
        for (const auto& cj : kids->array_val) {
            if (cj.kind == UiJson::Kind::Object) n.children.push_back(parse_node_from_json(cj));
        }
    }
    return n;
}

void dump_tree_recursive(const AndroidUiEngine& eng, const AndroidViewNode& node,
                         int depth, std::ostringstream& out) {
    for (int i = 0; i < depth; ++i) out << "  ";
    out << android_view_kind_name(node.kind) << "#" << node.id;
    const std::string resolved = eng.resolve_text(node);
    if (!resolved.empty()) out << " text=\"" << resolved << "\"";
    if (node.kind == AndroidViewKind::CheckBox || node.kind == AndroidViewKind::Switch) {
        out << (node.checked ? " [ON]" : " [OFF]");
    } else if (node.kind == AndroidViewKind::SeekBar ||
               node.kind == AndroidViewKind::ProgressBar) {
        out << " value=" << format_number(node.value) << "/" << format_number(node.max_value);
    } else if (node.kind == AndroidViewKind::ImageView) {
        out << " src=\"" << node.src << "\"";
    } else if (node.kind == AndroidViewKind::Unknown) {
        out << " [unsupported: " << node.type_name << "]";
    }
    if (!node.enabled) out << " [disabled]";
    if (!node.visible) out << " [invisible]";
    out << "\n";
    for (const auto& ch : node.children) dump_tree_recursive(eng, ch, depth + 1, out);
}

}  // namespace

#ifdef FINN_ANDROID_QT6
struct QtWidgetBinding {
    std::string node_id;
    QWidget* widget = nullptr;
};
#endif

struct AndroidUiEngine::QtState {
#ifdef FINN_ANDROID_QT6
    std::vector<QtWidgetBinding> bindings;
    finn::ui::FinnWindow* window = nullptr;
#else
    int unused = 0;
#endif
};

AndroidViewKind android_view_kind_from_name(std::string_view name) {
    const std::string tag = short_tag(name);
    if (tag == "LinearLayout") return AndroidViewKind::LinearLayout;
    if (tag == "FrameLayout") return AndroidViewKind::FrameLayout;
    if (tag == "RelativeLayout") return AndroidViewKind::RelativeLayout;
    if (tag == "ScrollView" || tag == "HorizontalScrollView") return AndroidViewKind::ScrollView;
    if (tag == "TextView") return AndroidViewKind::TextView;
    if (tag == "Button") return AndroidViewKind::Button;
    if (tag == "EditText") return AndroidViewKind::EditText;
    if (tag == "ImageView") return AndroidViewKind::ImageView;
    if (tag == "CheckBox") return AndroidViewKind::CheckBox;
    if (tag == "Switch" || tag == "ToggleButton") return AndroidViewKind::Switch;
    if (tag == "SeekBar" || tag == "RatingBar") return AndroidViewKind::SeekBar;
    if (tag == "ProgressBar") return AndroidViewKind::ProgressBar;
    if (tag == "ListView") return AndroidViewKind::ListView;
    if (tag == "RecyclerView") return AndroidViewKind::RecyclerView;
    return AndroidViewKind::Unknown;
}

const char* android_view_kind_name(AndroidViewKind kind) {
    switch (kind) {
        case AndroidViewKind::LinearLayout: return "LinearLayout";
        case AndroidViewKind::FrameLayout: return "FrameLayout";
        case AndroidViewKind::RelativeLayout: return "RelativeLayout";
        case AndroidViewKind::ScrollView: return "ScrollView";
        case AndroidViewKind::TextView: return "TextView";
        case AndroidViewKind::Button: return "Button";
        case AndroidViewKind::EditText: return "EditText";
        case AndroidViewKind::ImageView: return "ImageView";
        case AndroidViewKind::CheckBox: return "CheckBox";
        case AndroidViewKind::Switch: return "Switch";
        case AndroidViewKind::SeekBar: return "SeekBar";
        case AndroidViewKind::ProgressBar: return "ProgressBar";
        case AndroidViewKind::ListView: return "ListView";
        case AndroidViewKind::RecyclerView: return "RecyclerView";
        case AndroidViewKind::Unknown: break;
    }
    return "Unknown";
}

AndroidUiEngine::AndroidUiEngine() : qt_(new QtState()) {}

AndroidUiEngine::~AndroidUiEngine() {
    if (mounted_) unmount();
    delete qt_;
    qt_ = nullptr;
}

bool AndroidUiEngine::qt_backend_available() {
#ifdef FINN_ANDROID_QT6
    return true;
#else
    return false;
#endif
}

void AndroidUiEngine::set_title(const std::string& title) {
    title_ = title;
#ifdef FINN_ANDROID_QT6
    if (qt_ && qt_->window) qt_->window->setWindowTitleText(QString::fromStdString(title_));
#endif
}

bool AndroidUiEngine::inflate_binary_layout(const std::vector<std::uint8_t>& axml_bytes,
                                            const ArscInfo& arsc,
                                            std::vector<std::string>* warnings,
                                            std::string* error) {
    const AxmlDocument doc = parse_axml(axml_bytes.data(), axml_bytes.size());
    if (!doc.valid) {
        if (error) *error = doc.error.empty() ? "layout AXML is invalid" : doc.error;
        return false;
    }
    unsupported_views_.clear();
    std::vector<std::string> local_warnings;
    std::vector<std::size_t> path;  // child-index path to the current open parent
    bool have_root = false;
    for (const auto& el : doc.elements) {
        if (el.is_end) {
            if (!path.empty()) path.pop_back();
            continue;
        }
        AndroidViewNode node = node_from_axml(el, arsc, &local_warnings);
        if (node.kind == AndroidViewKind::Unknown) {
            unsupported_views_.push_back(node.type_name);
            local_warnings.push_back("unsupported Android view: " + node.type_name);
        }
        if (!have_root) {
            root_ = std::move(node);
            has_root_ = true;
            have_root = true;
            int counter = 1;
            assign_default_ids(root_, &counter);
        } else {
            AndroidViewNode* parent = path.empty() ? &root_ : node_at_path(root_, path);
            if (!parent) {
                if (error) *error = "layout element has no parent";
                return false;
            }
            parent->children.push_back(std::move(node));
            int counter = 1;
            assign_default_ids(parent->children.back(), &counter);
            path.push_back(parent->children.size() - 1u);
        }
    }
    if (!has_root_) {
        if (error) *error = "binary layout contains no root view";
        return false;
    }
    if (warnings) warnings->insert(warnings->end(), local_warnings.begin(), local_warnings.end());
    return true;
}

bool AndroidUiEngine::inflate_json(std::string_view json_text, std::string* error) {
    UiJson parsed;
    if (!parse_ui_json(json_text, &parsed, error) || parsed.kind != UiJson::Kind::Object) {
        if (error && error->empty()) *error = "Android UI document root must be a JSON object";
        return false;
    }
    if (const std::string t = parsed.get_string("title"); !t.empty()) title_ = t;
    if (const UiJson* st = parsed.get("state");
        st && st->kind == UiJson::Kind::Object) {
        for (const auto& pair : st->object_val) {
            const UiJson& v = pair.second;
            if (v.kind == UiJson::Kind::String) state_[pair.first] = v.str_val;
            else if (v.kind == UiJson::Kind::Number) state_[pair.first] = format_number(v.num_val);
            else if (v.kind == UiJson::Kind::Bool) state_[pair.first] = v.bool_val ? "true" : "false";
        }
    }
    const UiJson* root_obj = parsed.get("root");
    if (!root_obj || root_obj->kind != UiJson::Kind::Object) root_obj = &parsed;
    AndroidViewNode root = parse_node_from_json(*root_obj);
    if (mounted_) unmount();
    int counter = 1;
    assign_default_ids(root, &counter);
    root_ = std::move(root);
    has_root_ = true;
    return true;
}

AndroidViewNode* AndroidUiEngine::find_node(const std::string& id) {
    return has_root_ ? find_node_impl(root_, id) : nullptr;
}

void AndroidUiEngine::set_state(const std::string& key, const std::string& value) {
    state_[key] = value;
    sync_qt_bindings();
}

std::string AndroidUiEngine::get_state(const std::string& key,
                                       const std::string& fallback) const {
    const auto it = state_.find(key);
    return (it == state_.end()) ? fallback : it->second;
}

std::string AndroidUiEngine::resolve_text(const AndroidViewNode& node) const {
    std::string s = node.text;
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

void AndroidUiEngine::apply_inline_state_action(std::string_view action) {
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
        set_state(std::string(action.substr(0, eq)), std::string(action.substr(eq + 1)));
    }
}

bool AndroidUiEngine::trigger_click(const std::string& view_id) {
    AndroidViewNode* node = find_node(view_id);
    if (!node || !node->enabled) return false;
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

void AndroidUiEngine::emit_event(const std::string& handler, const std::string& view_id,
                                 const std::string& payload) {
    if (event_handler_) event_handler_(handler, view_id, payload);
}

bool AndroidUiEngine::set_text(const std::string& view_id, const std::string& value) {
    AndroidViewNode* node = find_node(view_id);
    if (!node) return false;
    node->text = value;
    sync_qt_bindings();
    return true;
}

std::string AndroidUiEngine::get_text(const std::string& view_id) const {
    if (!has_root_) return {};
    const AndroidViewNode* node = find_node_const_impl(root_, view_id);
    return node ? node->text : std::string();
}

void AndroidUiEngine::show_alert(const std::string& alert_title,
                                 const std::string& message) {
    alerts_.push_back(alert_title + ": " + message);
#ifdef FINN_ANDROID_QT6
    if (qt_ && qt_->window) {
        QMessageBox::information(qt_->window, QString::fromStdString(alert_title),
                                 QString::fromStdString(message));
    }
#endif
}

void AndroidUiEngine::fire_lifecycle_recursive(const AndroidViewNode& node, bool mounting) {
    lifecycle_log_.push_back(std::string(mounting ? "onCreate:" : "onPause:") + node.id);
    if (lifecycle_handler_) lifecycle_handler_(mounting ? "onCreate" : "onPause");
    for (const auto& ch : node.children) fire_lifecycle_recursive(ch, mounting);
}

void AndroidUiEngine::mount() {
    if (!has_root_ || mounted_) return;
    mounted_ = true;
    fire_lifecycle_recursive(root_, true);
    lifecycle_log_.push_back("onResume:" + root_.id);
    if (lifecycle_handler_) lifecycle_handler_("onResume");
}

void AndroidUiEngine::unmount() {
    if (!has_root_ || !mounted_) return;
    lifecycle_log_.push_back("onPause:" + root_.id);
    if (lifecycle_handler_) lifecycle_handler_("onPause");
    fire_lifecycle_recursive(root_, false);
    lifecycle_log_.push_back("onDestroy:" + root_.id);
    if (lifecycle_handler_) lifecycle_handler_("onDestroy");
    mounted_ = false;
}

void AndroidUiEngine::run_activity_lifecycle(const std::vector<std::string>& phases) {
    static constexpr const char* kKnown[] = {"onCreate", "onStart", "onResume",
                                             "onPause", "onStop", "onDestroy"};
    for (const auto& phase : phases) {
        bool known = false;
        for (const char* k : kKnown) if (phase == k) known = true;
        if (!known) {
            lifecycle_log_.push_back(phase + ": unknown lifecycle phase, skipped");
            continue;
        }
        const bool executed = lifecycle_handler_ ? lifecycle_handler_(phase) : false;
        lifecycle_log_.push_back(phase + ": " +
                                 (executed ? "executed" : "skipped (not executed by runtime)"));
    }
}

void AndroidUiEngine::sync_qt_bindings() {
#ifdef FINN_ANDROID_QT6
    if (!qt_) return;
    for (const auto& b : qt_->bindings) {
        AndroidViewNode* node = find_node(b.node_id);
        if (!node || !b.widget) continue;
        if (auto* lbl = qobject_cast<QLabel*>(b.widget)) {
            lbl->setText(QString::fromStdString(resolve_text(*node)));
        } else if (auto* btn = qobject_cast<QPushButton*>(b.widget)) {
            btn->setText(QString::fromStdString(resolve_text(*node)));
        } else if (auto* pb = qobject_cast<finn::ui::FinnProgressBar*>(b.widget)) {
            pb->setValue(node->value);
        } else if (auto* tog = qobject_cast<QAbstractButton*>(b.widget)) {
            tog->blockSignals(true);
            tog->setChecked(node->checked);
            tog->blockSignals(false);
        }
    }
#endif
}

#ifdef FINN_ANDROID_QT6
namespace {

QWidget* build_qt_node(AndroidUiEngine* engine,
                       std::vector<QtWidgetBinding>* bindings,
                       const AndroidViewNode& node, QWidget* parent) {
    const QString text = QString::fromStdString(engine->resolve_text(node));
    switch (node.kind) {
        case AndroidViewKind::LinearLayout:
        case AndroidViewKind::FrameLayout:
        case AndroidViewKind::RelativeLayout: {
            auto* w = new QWidget(parent);
            QLayout* lay = nullptr;
            if (node.orientation == "horizontal" && node.kind == AndroidViewKind::LinearLayout) {
                lay = new QHBoxLayout(w);
            } else {
                lay = new QVBoxLayout(w);
            }
            if (node.padding > 0) {
                const int p = static_cast<int>(node.padding);
                lay->setContentsMargins(p, p, p, p);
            }
            for (const auto& ch : node.children) {
                lay->addWidget(build_qt_node(engine, bindings, ch, w));
            }
            return w;
        }
        case AndroidViewKind::ScrollView: {
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
        case AndroidViewKind::TextView: {
            auto* lbl = new finn::ui::FinnLabel(text, finn::ui::FinnLabel::Role::Body,
                                                finn::ui::FinnLabel::Tone::Ink, parent);
            bindings->push_back({node.id, lbl});
            return lbl;
        }
        case AndroidViewKind::Button: {
            auto* btn = new finn::ui::FinnButton(text, finn::ui::FinnButton::Variant::Secondary,
                                                QString(), parent);
            btn->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(btn, &QPushButton::clicked, [engine, id]() {
                engine->trigger_click(id);
            });
            bindings->push_back({node.id, btn});
            return btn;
        }
        case AndroidViewKind::EditText: {
            auto* edit = new finn::ui::FinnTextField(QString(), parent);
            edit->setText(text);
            if (!node.hint.empty()) {
                edit->setPlaceholderText(QString::fromStdString(node.hint));
            }
            edit->setEnabled(node.enabled);
            bindings->push_back({node.id, edit});
            return edit;
        }
        case AndroidViewKind::ImageView: {
            auto* lbl = new finn::ui::FinnLabel(QString(), finn::ui::FinnLabel::Role::Body,
                                                finn::ui::FinnLabel::Tone::Ink, parent);
            if (!node.src.empty() && !engine->resource_root().empty()) {
                const fs::path candidate = fs::path(engine->resource_root()) / node.src;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) {
                    QPixmap px(QString::fromStdString(candidate.string()));
                    if (!px.isNull()) {
                        const int w = node.width > 0 ? static_cast<int>(node.width) : 64;
                        const int h = node.height > 0 ? static_cast<int>(node.height) : 64;
                        lbl->setPixmap(px.scaled(w, h, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
                    } else {
                        lbl->setText(QString::fromStdString("[Image decode failed: " +
                                                            candidate.string() + "]"));
                    }
                } else {
                    lbl->setText(QString::fromStdString("[Image missing: " + node.src + "]"));
                }
            } else {
                lbl->setText(QString::fromStdString("[Image resource unresolved: " +
                                                    node.src + "]"));
            }
            return lbl;
        }
        case AndroidViewKind::CheckBox:
        case AndroidViewKind::Switch: {
            auto* tog = new finn::ui::FinnToggle(node.checked, parent);
            tog->setText(text.isEmpty() ? QString::fromStdString(node.id) : text);
            tog->setEnabled(node.enabled);
            const std::string id = node.id;
            QObject::connect(tog, &QAbstractButton::clicked, [engine, id]() {
                engine->trigger_click(id);
            });
            bindings->push_back({node.id, tog});
            return tog;
        }
        case AndroidViewKind::SeekBar: {
            auto* sl = new finn::ui::FinnSlider(static_cast<int>(node.min_value),
                                                static_cast<int>(node.max_value),
                                                static_cast<int>(node.value), parent);
            const std::string id = node.id;
            QObject::connect(sl, &finn::ui::FinnSlider::valueChanged, [engine, id](int v) {
                engine->emit_event("onProgressChanged", id, std::to_string(v));
            });
            return sl;
        }
        case AndroidViewKind::ProgressBar: {
            auto* pb = new finn::ui::FinnProgressBar(5, parent);
            pb->setValue(node.value);
            bindings->push_back({node.id, pb});
            return pb;
        }
        case AndroidViewKind::ListView:
        case AndroidViewKind::RecyclerView: {
            auto* lw = new QListWidget(parent);
            for (const auto& ch : node.children) {
                lw->addItem(QString::fromStdString(engine->resolve_text(ch)));
            }
            return lw;
        }
        case AndroidViewKind::Unknown:
            break;
    }
    return new finn::ui::FinnLabel(
        QString::fromStdString("[Unsupported view: " + node.type_name + "]"),
        finn::ui::FinnLabel::Role::Body, finn::ui::FinnLabel::Tone::Warning, parent);
}

}  // namespace
#endif

int AndroidUiEngine::run_window(int argc, char** argv, bool headless) {
    if (!has_root_) return 1;
    if (!mounted_) mount();
#ifdef FINN_ANDROID_QT6
    if (!headless) {
        int qt_argc = argc;
        char** qt_argv = argv;
        static char fallback_prog[] = "finn-android";
        static char* fallback_argv[] = {fallback_prog, nullptr};
        if (qt_argc <= 0 || !qt_argv) {
            qt_argc = 1;
            qt_argv = fallback_argv;
        }
        QApplication app(qt_argc, qt_argv);
        finn::compat::apply_finn_qt_theme(&app);
        finn::ui::FinnWindow win(QString::fromStdString(title_), QStringLiteral("android"));
        win.resize(480, 720);
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
    const std::string summary = render_headless_summary();
    std::fwrite(summary.data(), 1, summary.size(), stdout);
    return 0;
}

std::string AndroidUiEngine::render_headless_summary() const {
    std::ostringstream out;
    out << "Android Window: " << title_ << "\n";
    if (!state_.empty()) {
        out << "  State:";
        std::vector<std::string> keys;
        for (const auto& pair : state_) keys.push_back(pair.first);
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) out << " " << k << "=\"" << state_.at(k) << "\"";
        out << "\n";
    }
    if (has_root_) dump_tree_recursive(*this, root_, 1, out);
    else out << "  (no root view inflated)\n";
    if (!unsupported_views_.empty()) {
        out << "  Unsupported views:";
        for (const auto& v : unsupported_views_) out << " " << v;
        out << "\n";
    }
    return out.str();
}

}  // namespace finn::android
