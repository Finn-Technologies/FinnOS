#include "ohos/arkui_declarative.hpp"

#include "ohos/abc_runtime.hpp"
#include "ohos/arkui_bridge.hpp"
#include "ohos/ohos_format.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace finn::ohos {
namespace {

constexpr std::size_t kMaxPages = 64;
constexpr std::size_t kMaxMedia = 6;
constexpr std::size_t kMaxStateHooks = 8;

// resources.index stores most strings as [u16 byte length+1][UTF-8][NUL].
// A resource is a pair of adjacent such strings: [value][key]. This is the
// compiled form of string.json / media / color entries; decoding the pair
// gives the real string value behind a $string:<key> reference in module.json.
// Numeric ID decoding is deliberately not attempted, matching the rest of
// finn-ohos (resource lookup is by name, never by invented path).

bool valid_utf8(const std::uint8_t* s, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t c = s[i];
        if (c < 0x80u) {
            ++i;
            continue;
        }
        int continuation = 0;
        std::uint32_t cp = 0;
        if ((c & 0xe0u) == 0xc0u) {
            if (c < 0xc2u) return false;
            continuation = 1;
            cp = c & 0x1fu;
        } else if ((c & 0xf0u) == 0xe0u) {
            continuation = 2;
            cp = c & 0x0fu;
        } else if ((c & 0xf8u) == 0xf0u) {
            if (c > 0xf4u) return false;
            continuation = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + static_cast<std::size_t>(continuation) >= n) return false;
        for (int k = 1; k <= continuation; ++k) {
            const std::uint8_t cc = s[i + static_cast<std::size_t>(k)];
            if ((cc & 0xc0u) != 0x80u) return false;
            cp = (cp << 6) | (cc & 0x3fu);
        }
        if (continuation == 2 && cp < 0x800u) return false;
        if (continuation == 3 && cp < 0x10000u) return false;
        if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return false;
        i += static_cast<std::size_t>(continuation) + 1;
    }
    return true;
}

bool is_resource_key(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    const auto alpha = [](unsigned char c) {
        return std::isalpha(c) != 0 || c == '_';
    };
    const auto alnum = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    };
    if (!alpha(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin() + 1, s.end(),
                       [&](char c) { return alnum(static_cast<unsigned char>(c)); });
}

struct IndexString {
    std::size_t prefix_offset = 0;
    std::string text;
};

std::size_t decode_resource_pairs(const std::vector<std::uint8_t>& bytes,
                                  std::map<std::string, std::string>* out) {
    std::vector<IndexString> strings;
    std::size_t consumed_until = 0;
    for (std::size_t i = 0; i + 2 <= bytes.size();) {
        if (i < consumed_until) {
            ++i;
            continue;
        }
        const std::size_t length =
            static_cast<std::size_t>(bytes[i]) |
            (static_cast<std::size_t>(bytes[i + 1]) << 8u);
        // Single-byte values are indistinguishable from binary padding here;
        // skip them rather than guess a pairing. All labels we resolve are
        // longer, and every rejected fact stays absent instead of fabricated.
        if (length < 3 || length > 512 || i + 2 + length > bytes.size()) {
            ++i;
            continue;
        }
        if (bytes[i + 2 + length - 1] != 0u) {
            ++i;
            continue;
        }
        const std::uint8_t* start = bytes.data() + i + 2;
        const std::size_t text_len = length - 1;
        bool printable = true;
        for (std::size_t k = 0; k < text_len; ++k) {
            if (start[k] < 0x20u) {
                printable = false;
                break;
            }
        }
        if (!printable || !valid_utf8(start, text_len)) {
            ++i;
            continue;
        }
        strings.push_back({i, std::string(reinterpret_cast<const char*>(start), text_len)});
        consumed_until = i + 2 + length;
        i = consumed_until;
    }

    std::size_t decoded = 0;
    for (std::size_t j = 0; j + 1 < strings.size(); ++j) {
        const IndexString& value = strings[j];
        const IndexString& key = strings[j + 1];
        // The two length-prefixed strings must be genuinely adjacent: binary
        // metadata sits between resources, so a gap means these are not a pair.
        if (key.prefix_offset != value.prefix_offset + value.text.size() + 3) continue;
        if (!is_resource_key(key.text) || value.text.empty()) continue;
        if (out->emplace(key.text, value.text).second) ++decoded;
    }
    return decoded;
}

bool read_binary(const fs::path& path, std::vector<std::uint8_t>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool read_text(const fs::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void write_binary(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::string reference_key(const std::string& ref) {
    const std::size_t colon = ref.find(':');
    if (colon == std::string::npos || colon + 1 >= ref.size()) return {};
    std::string key = ref.substr(colon + 1);
    const std::size_t dot = key.rfind('.');
    if (dot != std::string::npos && dot + 1 < key.size()) key = key.substr(dot + 1);
    return key;
}

bool is_media_reference(const std::string& ref) {
    return ref.rfind("$media:", 0) == 0 || ref.rfind("$rawfile:", 0) == 0;
}

bool read_page_profile(const fs::path& root, const std::string& pages_ref,
                       std::vector<std::string>* pages, std::string* error) {
    std::string profile = "main_pages";
    if (pages_ref.rfind("$profile:", 0) == 0 && pages_ref.size() > 9) {
        const std::string named = pages_ref.substr(9);
        if (!named.empty() && named.find('/') == std::string::npos) profile = named;
    }
    const fs::path profile_dir = root / "resources" / "base" / "profile";
    fs::path path = profile_dir / (profile + ".json");
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) path = profile_dir / "main_pages.json";
    if (!fs::is_regular_file(path, ec)) {
        if (error) {
            *error = "missing declarative page profile " + path.string() +
                     " in " + root.string();
        }
        return false;
    }

    std::string text;
    if (!read_text(path, &text)) {
        if (error) *error = "cannot read declarative page profile " + path.string();
        return false;
    }
    std::string parse_error;
    const auto doc = parse_json5(text, &parse_error);
    if (!doc || doc->kind != JsonValue::Kind::Object) {
        if (error) {
            *error = "invalid declarative page profile " + path.string() + ": " + parse_error;
        }
        return false;
    }
    const JsonValue* src = doc->get("src");
    if (!src || src->kind != JsonValue::Kind::Array) {
        if (error) {
            *error = "declarative page profile " + path.string() +
                     " has no src page array";
        }
        return false;
    }
    for (const JsonValue& entry : src->array_val) {
        if (entry.kind != JsonValue::Kind::String || entry.str_val.empty()) continue;
        if (pages->size() >= kMaxPages) break;
        pages->push_back(entry.str_val);
    }
    if (pages->empty()) {
        if (error) {
            *error = "declarative page profile " + path.string() +
                     " lists no pages";
        }
        return false;
    }
    return true;
}

bool is_image_file(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".webp" || ext == ".gif" || ext == ".svg";
}

// Collects real rawfile images. Each reference is checked against the real
// file through the existing ArkUI resource resolver before it is used.
std::vector<std::string> real_rawfile_images(const fs::path& root,
                                             const ArkUiEngine& engine) {
    std::vector<std::string> refs;
    const fs::path raw = root / "resources" / "rawfile";
    std::error_code ec;
    if (!fs::is_directory(raw, ec)) return refs;

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(raw, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !is_image_file(it->path())) continue;
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        if (refs.size() >= kMaxMedia) break;
        const fs::path relative = file.lexically_relative(raw);
        std::string name = relative.generic_string();
        if (name.empty() || name.find("..") != std::string::npos) continue;
        const std::string ref = "$rawfile:" + name;
        if (!engine.resolve_resource_path(ref).empty()) refs.push_back(ref);
    }
    return refs;
}

bool is_plain_identifier(const std::string& s) {
    if (s.size() < 2 || s.size() > 48) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

std::string strip_method_decoration(const std::string& raw) {
    std::string name = raw;
    const std::size_t hash = name.rfind('#');
    if (hash != std::string::npos) name = name.substr(hash + 1);
    while (true) {
        const std::size_t caret = name.rfind('^');
        if (caret == std::string::npos || caret + 1 >= name.size()) break;
        bool digits = true;
        for (std::size_t i = caret + 1; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                digits = false;
                break;
            }
        }
        if (!digits) break;
        name = name.substr(0, caret);
    }
    return name;
}

bool is_framework_member(const std::string& name) {
    static const std::set<std::string> kFramework = {
        "aboutToAppear", "aboutToDisappear", "aboutToBeDeleted",
        "initialRender", "rerender", "updateStateVars",
        "setInitiallyProvidedValue", "purgeVariableDependenciesOnElmtId",
        "func_main_0", "getEntryName", "onPageShow", "onPageHide",
        "onBackPress", "onAttach", "onDetach", "builder", "aboutToRecycle",
        "aboutToReuse", "onAreaChange", "onVisibleAreaChange",
    };
    return kFramework.count(name) != 0;
}

// Compiled ETS emits a getter and setter pair for each @State variable:
// "#~@0>#expression" and "#~@0>#expression^1". Requiring both names in a
// page class keeps these hooks real instead of treating every method as state.
std::vector<std::string> state_hooks_from_abc(
    const fs::path& root, const std::vector<std::string>& pages) {
    std::vector<std::string> hooks;
    std::vector<std::uint8_t> bytes;
    if (!read_binary(root / "ets" / "modules.abc", &bytes)) return hooks;
    const AbcFile abc = parse_abc_bytes(bytes.data(), bytes.size());
    if (!abc.valid) return hooks;

    std::set<std::string> seen;
    for (const AbcClass& cls : abc.classes) {
        bool page_class = false;
        for (const std::string& page : pages) {
            if (cls.name.find(page) != std::string::npos) {
                page_class = true;
                break;
            }
        }
        if (!page_class) continue;

        std::set<std::string> names;
        std::set<std::string> normalized;
        for (const AbcMethod& method : cls.methods) {
            names.insert(method.name);
            normalized.insert(strip_method_decoration(method.name));
        }
        for (const std::string& raw : names) {
            if (raw.find('^') == std::string::npos) continue;
            const std::string base = strip_method_decoration(raw);
            if (normalized.count(base) == 0) continue;
            if (!is_plain_identifier(base) || is_framework_member(base)) continue;
            if (!seen.insert(base).second) continue;
            hooks.push_back(base);
            if (hooks.size() >= kMaxStateHooks) return hooks;
        }
    }
    return hooks;
}

ArkUiNode text_node(const std::string& id, const std::string& text) {
    ArkUiNode node;
    node.kind = ArkComponentKind::Text;
    node.type_name = "Text";
    node.id = id;
    node.text = text;
    return node;
}

}  // namespace

bool build_declarative_ui_from_hap(const std::string& install_dir,
                                   ArkUiEngine* engine,
                                   std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (!engine) return fail("ArkUiEngine is null");

    const fs::path root(install_dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return fail("install directory does not exist: " + install_dir);
    }

    std::string module_text;
    const fs::path module_path = root / "module.json";
    if (!read_text(module_path, &module_text)) {
        return fail("missing module.json in " + install_dir);
    }
    std::string parse_error;
    const auto manifest = parse_json5(module_text, &parse_error);
    if (!manifest || manifest->kind != JsonValue::Kind::Object) {
        return fail("invalid module.json: " + parse_error);
    }
    const JsonValue* app = manifest->get("app");
    const JsonValue* module = manifest->get("module");
    if (!app || app->kind != JsonValue::Kind::Object ||
        !module || module->kind != JsonValue::Kind::Object) {
        return fail("module.json has no app or module object in " + install_dir);
    }

    const std::string bundle = app->get_string("bundleName");
    if (bundle.empty()) return fail("module.json has no app.bundleName in " + install_dir);
    const std::string version = app->get_string("versionName");
    const std::string app_label_ref = app->get_string("label");
    const std::string app_icon_ref = app->get_string("icon");
    const std::string pages_ref = module->get_string("pages");
    const std::string module_desc_ref = module->get_string("description");
    const std::string main_element = module->get_string("mainElement");

    std::string ability_name;
    std::string ability_label_ref;
    std::string ability_desc_ref;
    std::string ability_icon_ref;
    const JsonValue* abilities = module->get("abilities");
    if (abilities && abilities->kind == JsonValue::Kind::Array &&
        !abilities->array_val.empty()) {
        const JsonValue& first = abilities->array_val.front();
        ability_name = first.get_string("name");
        ability_label_ref = first.get_string("label");
        ability_desc_ref = first.get_string("description");
        ability_icon_ref = first.get_string("icon");
    }
    if (ability_name.empty()) ability_name = main_element;

    // The page profile is the package's own declarative page tree. Without it
    // there is no page to show, so keep the caller's honest fallback instead.
    std::vector<std::string> pages;
    if (!read_page_profile(root, pages_ref, &pages, error)) return false;

    std::map<std::string, std::string> resources;
    std::vector<std::uint8_t> index_bytes;
    if (read_binary(root / "resources.index", &index_bytes)) {
        (void)decode_resource_pairs(index_bytes, &resources);
    }

    const auto resolve_text = [&resources](const std::string& ref) -> std::string {
        if (ref.empty()) return {};
        if (ref[0] != '$') return ref;  // a literal label already in module.json
        const std::string key = reference_key(ref);
        if (key.empty()) return {};
        const auto it = resources.find(key);
        return it == resources.end() ? std::string() : it->second;
    };

    const std::string app_label = resolve_text(app_label_ref);
    const std::string title = app_label.empty() ? bundle : app_label;
    const std::string ability_label = resolve_text(ability_label_ref);
    const std::string description =
        resolve_text(module_desc_ref.empty() ? ability_desc_ref : module_desc_ref);

    engine->set_resource_root(install_dir);

    ArkUiNode root_node;
    root_node.kind = ArkComponentKind::Navigation;
    root_node.type_name = "Navigation";
    root_node.id = "ohos_declarative_root";
    root_node.text = title;
    root_node.padding = 12;
    root_node.spacing = 8;

    ArkUiNode header;
    header.kind = ArkComponentKind::Row;
    header.type_name = "Row";
    header.id = "ohos_declarative_header";
    header.spacing = 12;
    std::string icon_ref = is_media_reference(app_icon_ref) ? app_icon_ref : std::string();
    if (icon_ref.empty() && is_media_reference(ability_icon_ref)) icon_ref = ability_icon_ref;
    if (!icon_ref.empty() && !engine->resolve_resource_path(icon_ref).empty()) {
        ArkUiNode icon;
        icon.kind = ArkComponentKind::Image;
        icon.type_name = "Image";
        icon.id = "ohos_app_icon";
        icon.image_src = icon_ref;
        icon.width = 64;
        icon.height = 64;
        header.children.push_back(std::move(icon));
    }
    ArkUiNode identity;
    identity.kind = ArkComponentKind::Column;
    identity.type_name = "Column";
    identity.id = "ohos_declarative_identity";
    identity.spacing = 4;
    identity.children.push_back(text_node("ohos_bundle", bundle));
    if (!version.empty()) {
        identity.children.push_back(text_node("ohos_version", version));
    }
    if (!ability_label.empty()) {
        identity.children.push_back(text_node("ohos_ability_label", ability_label));
    }
    if (!ability_name.empty() && ability_name != ability_label) {
        identity.children.push_back(text_node("ohos_ability_name", ability_name));
    }
    header.children.push_back(std::move(identity));
    root_node.children.push_back(std::move(header));

    if (!description.empty()) {
        root_node.children.push_back(text_node("ohos_description", description));
    }

    ArkUiNode divider;
    divider.kind = ArkComponentKind::Divider;
    divider.type_name = "Divider";
    divider.id = "ohos_declarative_divider";
    root_node.children.push_back(std::move(divider));

    ArkUiNode page_list;
    page_list.kind = ArkComponentKind::List;
    page_list.type_name = "List";
    page_list.id = "ohos_declarative_pages";
    for (std::size_t i = 0; i < pages.size(); ++i) {
        ArkUiNode item;
        item.kind = ArkComponentKind::ListItem;
        item.type_name = "ListItem";
        item.id = "ohos_page_" + std::to_string(i);
        item.text = pages[i];
        page_list.children.push_back(std::move(item));
    }
    root_node.children.push_back(std::move(page_list));

    const std::vector<std::string> media = real_rawfile_images(root, *engine);
    if (!media.empty()) {
        ArkUiNode media_row;
        media_row.kind = ArkComponentKind::Row;
        media_row.type_name = "Row";
        media_row.id = "ohos_declarative_media";
        media_row.spacing = 8;
        for (std::size_t i = 0; i < media.size(); ++i) {
            ArkUiNode image;
            image.kind = ArkComponentKind::Image;
            image.type_name = "Image";
            image.id = "ohos_media_" + std::to_string(i);
            image.image_src = media[i];
            image.width = 48;
            image.height = 48;
            media_row.children.push_back(std::move(image));
        }
        root_node.children.push_back(std::move(media_row));
    }

    // @State hooks whose getter/setter pairs are really present in the page
    // classes of ets/modules.abc. Values stay empty: no runtime value exists
    // in the package, so none is invented.
    for (const std::string& hook : state_hooks_from_abc(root, pages)) {
        engine->set_state(hook, "");
    }

    engine->set_title(title);
    engine->set_root(std::move(root_node));
    engine->mount();
    if (error) error->clear();
    return true;
}

bool ohos_declarative_self_test() {
    std::cout << "== ohos_declarative_self_test ==\n";
    bool ok = true;
    const auto check = [&ok](bool condition, const std::string& what) {
        std::cout << (condition ? "  ok   " : "  FAIL ") << what << "\n";
        if (!condition) ok = false;
    };

    const fs::path tmp = fs::temp_directory_path() / "finn_ohos_declarative_self_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "resources" / "base" / "profile", ec);
    fs::create_directories(tmp / "resources" / "base" / "media", ec);
    fs::create_directories(tmp / "bad", ec);

    const std::string fixture_module = R"JSON({"app":{"bundleName":"fixture.bundle",
"versionName":"2.3.4","label":"$string:app_name","icon":"$media:app_icon"},
"module":{"mainElement":"FixtureAbility","pages":"$profile:main_pages",
"abilities":[{"name":"FixtureAbility","label":"$string:ability_label",
"icon":"$media:app_icon"}]}})JSON";
    const std::string fixture_pages = R"JSON({"src":["pages/Alpha","pages/Beta"]})JSON";
    const std::vector<std::uint8_t> tiny_png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0xab, 0xce, 0x36, 0x89, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };

    const auto write_text = [](const fs::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    };
    write_text(tmp / "module.json", fixture_module);
    write_text(tmp / "resources" / "base" / "profile" / "main_pages.json", fixture_pages);
    write_binary(tmp / "resources" / "base" / "media" / "app_icon.png", tiny_png);
    write_text(tmp / "bad" / "module.json", fixture_module);
    write_binary(tmp / "bad" / "resources.index", std::vector<std::uint8_t>(128, 0));

    std::vector<std::uint8_t> index_bytes(128, 0);
    const char header[] = "Restool 6.0.0.001";
    for (std::size_t i = 0; i < sizeof(header) - 1; ++i) {
        index_bytes[i] = static_cast<std::uint8_t>(header[i]);
    }
    const auto append_pair = [&index_bytes](const std::string& value,
                                            const std::string& key) {
        const auto append_one = [&index_bytes](const std::string& s) {
            const std::uint16_t length = static_cast<std::uint16_t>(s.size() + 1);
            index_bytes.push_back(static_cast<std::uint8_t>(length & 0xffu));
            index_bytes.push_back(static_cast<std::uint8_t>(length >> 8u));
            index_bytes.insert(index_bytes.end(), s.begin(), s.end());
            index_bytes.push_back(0);
        };
        append_one(value);
        append_one(key);
    };
    append_pair("Fixture Notes", "app_name");
    append_pair("Fixture Ability", "ability_label");
    write_binary(tmp / "resources.index", index_bytes);

    ArkUiEngine fixture;
    std::string fixture_error;
    const bool fixture_built =
        build_declarative_ui_from_hap(tmp.string(), &fixture, &fixture_error);
    check(fixture_built, "fixture package builds: " + fixture_error);
    check(fixture.title() == "Fixture Notes",
          "label resolved through resources.index: '" + fixture.title() + "'");
    check(fixture.root().kind == ArkComponentKind::Navigation,
          "root is Navigation");
    const ArkUiNode* pages = fixture.find_node("ohos_declarative_pages");
    check(pages != nullptr && pages->children.size() == 2,
          "page list has the two profile pages");
    check(pages != nullptr && !pages->children.empty() &&
              pages->children[0].text == "pages/Alpha",
          "first page path comes from main_pages.json");
    const ArkUiNode* icon = fixture.find_node("ohos_app_icon");
    check(icon != nullptr && !fixture.resolve_image_src(*icon).empty(),
          "app icon resolves to a real media file");
    const ArkUiNode* ability = fixture.find_node("ohos_ability_label");
    check(ability != nullptr && ability->text == "Fixture Ability",
          "ability label resolved through resources.index");

    ArkUiEngine missing_pages;
    std::string missing_error;
    const bool rejected = build_declarative_ui_from_hap(
        (tmp / "bad").string(), &missing_pages, &missing_error);
    check(!rejected && missing_error.find("page profile") != std::string::npos,
          "no page profile is rejected: " + missing_error);

    std::vector<fs::path> real_dirs;
    if (const char* single = std::getenv("FINN_OHOS_INSTALL_DIR")) {
        if (*single) real_dirs.emplace_back(single);
    } else {
        std::string home;
        if (const char* h = std::getenv("HOME")) home = h;
        const fs::path apps = fs::path(home) / ".local" / "share" / "finn" / "ohos" / "apps";
        if (fs::is_directory(apps, ec)) {
            for (fs::directory_iterator it(apps, ec), end;
                 it != end && !ec; it.increment(ec)) {
                if (it->is_directory(ec)) real_dirs.push_back(it->path());
            }
        }
    }

    std::size_t real_built = 0;
    std::size_t best_score = 0;
    std::string best_summary;
    for (const fs::path& dir : real_dirs) {
        if (!fs::is_regular_file(dir / "module.json", ec)) continue;
        ArkUiEngine engine;
        std::string error;
        if (!build_declarative_ui_from_hap(dir.string(), &engine, &error)) {
            std::cout << "  FAIL real " << dir.filename().string() << ": " << error << "\n";
            ok = false;
            continue;
        }
        ++real_built;
        const ArkUiNode* list = engine.find_node("ohos_declarative_pages");
        const std::size_t page_count = list ? list->children.size() : 0;
        const std::size_t state_count = engine.all_state().size();
        std::cout << "  ok   real " << dir.filename().string()
                  << " | title=\"" << engine.title() << "\""
                  << " | pages=" << page_count
                  << " | state=" << state_count << "\n";
        if (state_count + page_count > best_score) {
            best_score = state_count + page_count;
            best_summary = engine.render_headless_summary();
        }
    }
    if (!best_summary.empty()) std::cout << best_summary;
    if (!real_dirs.empty()) {
        check(real_built > 0,
              "at least one installed real HAP produced a package-derived tree");
    }

    fs::remove_all(tmp, ec);
    std::cout << (ok ? "SELFTEST: PASS\n" : "SELFTEST: FAIL\n");
    return ok;
}

}  // namespace finn::ohos
