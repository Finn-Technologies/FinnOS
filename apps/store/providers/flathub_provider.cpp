#include "flathub_provider.hpp"

#include <cctype>
#include <sstream>

namespace finn::store {
namespace {

std::string json_str(std::string_view obj, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t pos = obj.find(needle);
    while (pos != std::string_view::npos) {
        std::size_t colon = pos + needle.size();
        while (colon < obj.size() && std::isspace(static_cast<unsigned char>(obj[colon]))) ++colon;
        if (colon < obj.size() && obj[colon] == ':') {
            std::size_t v = colon + 1;
            while (v < obj.size() && std::isspace(static_cast<unsigned char>(obj[v]))) ++v;
            if (v < obj.size() && obj[v] == '"') {
                std::string out;
                for (std::size_t i = v + 1; i < obj.size(); ++i) {
                    if (obj[i] == '\\' && i + 1 < obj.size()) {
                        const char esc = obj[++i];
                        if (esc == 'n') out.push_back('\n');
                        else if (esc == 't') out.push_back(' ');
                        else out.push_back(esc);
                    } else if (obj[i] == '"') {
                        return out;
                    } else {
                        out.push_back(obj[i]);
                    }
                }
            }
            return {};
        }
        pos = obj.find(needle, pos + needle.size());
    }
    return {};
}

bool json_bool(std::string_view obj, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t pos = obj.find(needle);
    if (pos == std::string_view::npos) return false;
    const std::size_t colon = obj.find(':', pos + needle.size());
    if (colon == std::string_view::npos) return false;
    std::size_t v = colon + 1;
    while (v < obj.size() && std::isspace(static_cast<unsigned char>(obj[v]))) ++v;
    return obj.substr(v, 4) == "true";
}

// Splits top-level JSON objects inside an array "[{...}, {...}]"
std::vector<std::string_view> split_json_objects(std::string_view arr) {
    std::vector<std::string_view> objs;
    bool in_str = false;
    bool esc = false;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const char c = arr[i];
        if (in_str) {
            if (esc) { esc = false; continue; }
            if (c == '\\') { esc = true; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                objs.push_back(arr.substr(start, i - start + 1));
            }
        }
    }
    return objs;
}

}  // namespace

std::vector<StoreAppItem> parse_flathub_hits_json(std::string_view json) {
    std::vector<StoreAppItem> out;
    std::string_view slice = json;
    const std::size_t hits_pos = json.find("\"hits\"");
    if (hits_pos != std::string_view::npos) {
        const std::size_t lb = json.find('[', hits_pos);
        if (lb != std::string_view::npos) slice = json.substr(lb);
    }
    for (std::string_view obj : split_json_objects(slice)) {
        const std::string app_id = json_str(obj, "app_id");
        const std::string name = json_str(obj, "name");
        if (app_id.empty() || name.empty()) continue;

        StoreAppItem item;
        item.id = "flatpak:" + app_id;
        item.install_ref = app_id;
        item.name = name;
        item.subtitle = json_str(obj, "summary");
        item.description = item.subtitle;
        item.developer = json_str(obj, "developer_name");
        item.icon = json_str(obj, "icon");
        if (item.icon.empty()) item.icon = "linux.svg";
        item.ecosystem = StoreEcosystem::Flatpak;
        item.category = "Linux";
        item.verified = json_bool(obj, "verification_verified");
        item.verified_source = json_str(obj, "verification_website");
        if (item.verified && item.verified_source.empty()) {
            item.verified_source = "Flathub Verified";
        }
        item.accent_hex = "#38bdf8";
        item.permissions = {"Flatpak Sandbox", "Wayland Display", "Network Access"};
        out.push_back(std::move(item));
        if (out.size() >= 48) break;
    }
    return out;
}

bool enrich_flathub_appstream_json(std::string_view json, StoreAppItem* item) {
    if (!item || json.empty()) return false;
    const std::string desc = json_str(json, "description");
    if (!desc.empty()) {
        // Strip minimal HTML tags from Flathub AppStream descriptions
        std::string plain;
        bool in_tag = false;
        for (char c : desc) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; plain.push_back(' '); continue; }
            if (!in_tag) plain.push_back(c);
        }
        item->description = plain;
    }
    const std::string ver = json_str(json, "version");
    if (!ver.empty()) item->version = ver;

    // Extract screenshot URLs ("src": "https://dl.flathub.org/...")
    std::size_t pos = 0;
    while (item->screenshots.size() < 6) {
        const std::size_t sp = json.find("\"src\"", pos);
        if (sp == std::string_view::npos) break;
        const std::string url = json_str(json.substr(sp, 300), "src");
        if (url.rfind("https://", 0) == 0 &&
            (url.find(".png") != std::string::npos || url.find(".webp") != std::string::npos ||
             url.find(".jpg") != std::string::npos)) {
            item->screenshots.push_back(url);
        }
        pos = sp + 5;
    }
    return true;
}

std::vector<std::string> parse_flatpak_updates_output(std::string_view stdout_text) {
    std::vector<std::string> ids;
    std::istringstream in{std::string(stdout_text)};
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t tab = line.find_first_of("\t ");
        const std::string id = (tab == std::string::npos) ? line : line.substr(0, tab);
        if (!id.empty() && id.find('.') != std::string::npos) {
            ids.push_back(id);
        }
    }
    return ids;
}

}  // namespace finn::store
