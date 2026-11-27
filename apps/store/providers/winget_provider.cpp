#include "winget_provider.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace finn::store {
namespace {

std::string trim(std::string_view s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n\"'");
    if (a == std::string_view::npos) return {};
    const std::size_t b = s.find_last_not_of(" \t\r\n\"'");
    return std::string(s.substr(a, b - a + 1));
}

std::string json_str(std::string_view obj, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t pos = obj.find(needle);
    if (pos == std::string_view::npos) return {};
    const std::size_t colon = obj.find(':', pos + needle.size());
    if (colon == std::string_view::npos) return {};
    const std::size_t q1 = obj.find('"', colon + 1);
    if (q1 == std::string_view::npos) return {};
    std::string out;
    for (std::size_t i = q1 + 1; i < obj.size(); ++i) {
        if (obj[i] == '\\' && i + 1 < obj.size()) {
            out.push_back(obj[++i]);
        } else if (obj[i] == '"') {
            return out;
        } else {
            out.push_back(obj[i]);
        }
    }
    return {};
}

}  // namespace

std::vector<StoreAppItem> parse_winget_search_json(std::string_view json) {
    std::vector<StoreAppItem> out;
    std::size_t pos = json.find("\"Packages\"");
    if (pos == std::string_view::npos) pos = 0;

    bool in_str = false;
    bool esc = false;
    int depth = 0;
    std::size_t start = 0;
    const std::size_t lb = json.find('[', pos);
    if (lb == std::string_view::npos) return out;

    for (std::size_t i = lb + 1; i < json.size(); ++i) {
        const char c = json[i];
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
                const std::string_view obj = json.substr(start, i - start + 1);
                const std::string id = json_str(obj, "Id");
                const std::string name = json_str(obj, "Name");
                if (id.empty() || name.empty()) continue;

                StoreAppItem item;
                item.id = "winget:" + id;
                item.install_ref = id;
                item.name = name;
                item.developer = json_str(obj, "Publisher");
                item.subtitle = json_str(obj, "Description");
                if (item.subtitle.empty()) item.subtitle = "Windows package via winget-pkgs";
                item.description = item.subtitle;

                // Prefer official IconUrl; else derive high-res brand icon from Homepage domain.
                const std::string icon_url = json_str(obj, "IconUrl");
                if (icon_url.rfind("https://", 0) == 0 || icon_url.rfind("http://", 0) == 0) {
                    item.icon = icon_url;
                } else {
                    const std::string hp = json_str(obj, "Homepage");
                    std::string host;
                    const std::size_t scheme = hp.find("://");
                    if (scheme != std::string::npos) {
                        const std::size_t h0 = scheme + 3;
                        const std::size_t h1 = hp.find_first_of("/?#:", h0);
                        host = hp.substr(h0, h1 == std::string::npos ? std::string::npos : (h1 - h0));
                    }
                    if (!host.empty() && host.find('.') != std::string::npos &&
                        host != "github.com" && host != "www.github.com") {
                        item.icon = "https://www.google.com/s2/favicons?sz=128&domain=" + host;
                    } else {
                        item.icon = "windows";
                    }
                }
                item.ecosystem = StoreEcosystem::Windows;
                const std::string tag0 = json_str(obj, "Tags");
                item.category = tag0.empty() ? "Apps" : tag0;
                item.verified = true;
                item.verified_source = "winget-pkgs (SHA-256)";
                item.accent_hex = "#6366f1";
                item.permissions = {"Windows Prefix (finn-win)", "SHA-256 Verified Installer"};
                out.push_back(std::move(item));
                if (out.size() >= 48) break;
            }
        }
    }
    return out;
}

WingetInstallerManifest parse_winget_installer_yaml(std::string_view yaml) {
    WingetInstallerManifest m;
    std::istringstream in{std::string(yaml)};
    std::string line;

    struct Candidate {
        std::string arch;
        std::string url;
        std::string sha256;
        std::string type;
        std::string silent;
    };
    std::vector<Candidate> cands;
    Candidate cur;
    bool in_installers = false;
    bool in_file_exts = false;

    while (std::getline(in, line)) {
        const std::size_t first_non_ws = line.find_first_not_of(" \t");
        if (first_non_ws == std::string::npos || line[first_non_ws] == '#') continue;
        const std::string_view sv(line.data() + first_non_ws, line.size() - first_non_ws);

        if (first_non_ws == 0 && sv.rfind("- ", 0) != 0) {
            in_file_exts = false;
        }
        if (sv.rfind("PackageIdentifier:", 0) == 0) {
            m.package_id = trim(sv.substr(18));
        } else if (sv.rfind("PackageVersion:", 0) == 0) {
            m.version = trim(sv.substr(15));
        } else if (sv.rfind("InstallerType:", 0) == 0 && !in_installers) {
            m.installer_type = trim(sv.substr(14));
        } else if (sv.rfind("FileExtensions:", 0) == 0) {
            in_file_exts = true;
        } else if (in_file_exts && sv.rfind("- ", 0) == 0) {
            const std::string ext = trim(sv.substr(2));
            if (!ext.empty()) m.file_extensions.push_back(ext);
        } else if (sv.rfind("Installers:", 0) == 0) {
            in_installers = true;
            in_file_exts = false;
        } else if (in_installers) {
            std::string_view kv = sv;
            if (kv.rfind("- ", 0) == 0) {
                if (!cur.url.empty()) cands.push_back(cur);
                cur = Candidate{};
                cur.type = m.installer_type;
                kv = kv.substr(2);
            }
            if (kv.rfind("Architecture:", 0) == 0) cur.arch = trim(kv.substr(13));
            else if (kv.rfind("InstallerUrl:", 0) == 0) cur.url = trim(kv.substr(13));
            else if (kv.rfind("InstallerSha256:", 0) == 0) cur.sha256 = trim(kv.substr(16));
            else if (kv.rfind("InstallerType:", 0) == 0) cur.type = trim(kv.substr(14));
            else if (kv.rfind("Silent:", 0) == 0) cur.silent = trim(kv.substr(7));
        }
    }
    if (!cur.url.empty()) cands.push_back(cur);

    if (cands.empty()) {
        m.error = "winget manifest contains no InstallerUrl entries";
        return m;
    }
    // Prefer x64 installer, then x86, then first available
    const Candidate* best = &cands.front();
    for (const auto& c : cands) {
        if (c.arch == "x64") { best = &c; break; }
        if (c.arch == "x86" && best->arch != "x64") best = &c;
    }
    m.architecture = best->arch.empty() ? "x64" : best->arch;
    m.installer_url = best->url;
    m.sha256 = best->sha256;
    m.installer_type = best->type.empty() ? "exe" : best->type;
    m.silent_switches = best->silent;
    m.valid = !m.installer_url.empty();
    return m;
}

}  // namespace finn::store
