#include "ohos/ohos_installer.hpp"

#include "ohos/zip_reader.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::ohos {
namespace {

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::size_t off = s.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string slugify_bundle(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool prev_dash = false;
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            prev_dash = false;
        } else if (!prev_dash && !out.empty()) {
            out.push_back('-');
            prev_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "ohos-app" : out;
}

std::string sanitize_bundle_dirname(std::string_view bundle) {
    std::string out;
    out.reserve(bundle.size());
    for (char c : bundle) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    return out.empty() ? "unknown.bundle" : out;
}

std::string sanitize_pipe_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '|' || c == '\n' || c == '\r') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

std::vector<std::string> split_pipe_line(std::string_view line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t bar = line.find('|', start);
        if (bar == std::string_view::npos) {
            parts.emplace_back(line.substr(start));
            break;
        }
        parts.emplace_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    return parts;
}

std::string friendly_app_name(const OhosModuleManifest& m) {
    auto clean_label = [](const std::string& lbl) -> std::string {
        if (lbl.empty()) return {};
        if (lbl.rfind("$string:", 0) == 0) return {};
        return lbl;
    };
    if (std::string n = clean_label(m.app_label); !n.empty()) return n;
    for (const auto& ab : m.abilities) {
        if (std::string n = clean_label(ab.label); !n.empty()) return n;
    }
    if (!m.bundle_name.empty()) {
        const std::size_t dot = m.bundle_name.rfind('.');
        std::string tail = (dot != std::string::npos && dot + 1 < m.bundle_name.size())
                               ? m.bundle_name.substr(dot + 1)
                               : m.bundle_name;
        if (!tail.empty()) {
            tail[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(tail[0])));
            return tail;
        }
    }
    return m.module_name.empty() ? "OpenHarmony App" : m.module_name;
}

std::string extract_icon_stem(const OhosModuleManifest& m) {
    auto parse_ref = [](const std::string& ref) -> std::string {
        if (ref.rfind("$media:", 0) == 0) return ref.substr(7);
        if (ref.rfind("$layered_image:", 0) == 0) return ref.substr(15);
        return {};
    };
    if (std::string s = parse_ref(m.app_icon); !s.empty()) return s;
    for (const auto& ab : m.abilities) {
        if (std::string s = parse_ref(ab.icon); !s.empty()) return s;
        if (std::string s = parse_ref(ab.start_window_icon); !s.empty()) return s;
    }
    return "app_icon";
}

bool is_valid_image_bytes(const std::vector<std::uint8_t>& bytes, std::string* ext_out) {
    if (bytes.size() >= 8 && bytes[0] == 0x89u && bytes[1] == 'P' &&
        bytes[2] == 'N' && bytes[3] == 'G') {
        if (ext_out) *ext_out = ".png";
        return true;
    }
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
        std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        if (ext_out) *ext_out = ".webp";
        return true;
    }
    if (bytes.size() >= 5) {
        const std::string_view sv(reinterpret_cast<const char*>(bytes.data()),
                                  std::min<std::size_t>(bytes.size(), 256));
        if (sv.find("<svg") != std::string_view::npos) {
            if (ext_out) *ext_out = ".svg";
            return true;
        }
    }
    return false;
}

std::string extract_icon_from_zip(const ZipReader& zip,
                                  const OhosModuleManifest& manifest,
                                  const std::string& icons_dir) {
    const std::string stem = extract_icon_stem(manifest);
    std::vector<std::string> candidates = {
        "resources/base/media/" + stem + ".png",
        "resources/base/media/" + stem + ".webp",
        "resources/base/media/" + stem + ".svg",
        "resources/base/media/icon.png",
        "resources/base/media/app_icon.png",
        "resources/rawfile/" + stem + ".png",
    };

    // Also include any media or rawfile image present in the archive
    for (const auto& e : zip.entries()) {
        if (e.is_directory) continue;
        if (e.name.rfind("resources/base/media/", 0) == 0 ||
            e.name.rfind("resources/rawfile/", 0) == 0) {
            if (ends_with_ci(e.name, ".png") || ends_with_ci(e.name, ".webp") ||
                ends_with_ci(e.name, ".svg")) {
                candidates.push_back(e.name);
            }
        }
    }

    for (const auto& entry_name : candidates) {
        if (!zip.has_entry(entry_name)) continue;
        std::vector<std::uint8_t> bytes;
        if (!zip.read_entry(entry_name, &bytes)) continue;
        std::string ext;
        if (!is_valid_image_bytes(bytes, &ext)) continue;

        std::error_code ec;
        fs::create_directories(icons_dir, ec);
        const std::string safe_bundle = sanitize_bundle_dirname(manifest.bundle_name);
        const fs::path dst = fs::path(icons_dir) / (safe_bundle + ext);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) continue;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (out) return dst.string();
    }
    return {};
}

bool write_registry_file(const std::string& reg_path,
                         const std::vector<InstalledOhosApp>& apps,
                         std::string* error) {
    std::error_code ec;
    const fs::path p(reg_path);
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (error) *error = "failed to write OHOS registry: " + reg_path;
            return false;
        }
        out << "# FinnOS OpenHarmony 6.1 LTS installed app registry\n";
        for (const auto& a : apps) {
            std::string hsps;
            for (std::size_t i = 0; i < a.shared_packages.size(); ++i) {
                if (i > 0) hsps += ";";
                hsps += sanitize_pipe_field(a.shared_packages[i]);
            }
            out << sanitize_pipe_field(a.bundle_name) << "|"
                << sanitize_pipe_field(a.name) << "|"
                << sanitize_pipe_field(a.version_name) << "|"
                << a.version_code << "|"
                << a.target_api_version << "|"
                << a.min_api_version << "|"
                << sanitize_pipe_field(a.main_ability) << "|"
                << sanitize_pipe_field(a.module_type) << "|"
                << sanitize_pipe_field(a.install_dir) << "|"
                << sanitize_pipe_field(a.package_path) << "|"
                << sanitize_pipe_field(a.icon_path) << "|"
                << hsps << "\n";
        }
        if (!out) {
            if (error) *error = "write error while saving OHOS registry";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error) *error = "failed to commit OHOS registry: " + ec.message();
        return false;
    }
    return true;
}

int run_tool(const std::vector<std::string>& argv) {
    if (argv.empty()) return -1;
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        ::execvp(cargv[0], cargv.data());
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool decode_chunked_body(std::string_view raw, std::string* decoded) {
    decoded->clear();
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t crlf = raw.find("\r\n", pos);
        if (crlf == std::string_view::npos) return false;
        std::string size_str(raw.substr(pos, crlf - pos));
        if (const std::size_t semi = size_str.find(';'); semi != std::string::npos) {
            size_str.resize(semi);
        }
        char* end = nullptr;
        const unsigned long chunk_len = std::strtoul(size_str.c_str(), &end, 16);
        if (!end || end == size_str.c_str()) return false;
        pos = crlf + 2;
        if (chunk_len == 0) return true;
        if (pos + chunk_len + 2 > raw.size()) return false;
        decoded->append(raw.data() + pos, chunk_len);
        pos += chunk_len + 2;
    }
    return true;
}

bool download_http_native(const std::string& url, const std::string& dest_path,
                          int redirects_left, std::string* error) {
    if (redirects_left < 0) {
        if (error) *error = "too many HTTP redirects";
        return false;
    }
    if (starts_with_ci(url, "https://")) {
        const int rc = run_tool({"curl", "-fsSL", "-o", dest_path, url});
        if (rc != 0) {
            if (error) *error = "HTTPS download failed via curl (exit " + std::to_string(rc) + ")";
            return false;
        }
        return true;
    }
    if (!starts_with_ci(url, "http://")) {
        if (error) *error = "unsupported URL scheme: " + url;
        return false;
    }

    const std::string_view rest = std::string_view(url).substr(7);
    const std::size_t slash = rest.find('/');
    const std::string_view authority =
        (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    const std::string path =
        (slash == std::string_view::npos) ? "/" : std::string(rest.substr(slash));

    std::string host;
    std::string port = "80";
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t rb = authority.find(']');
        if (rb == std::string_view::npos) {
            if (error) *error = "malformed IPv6 host in URL";
            return false;
        }
        host = std::string(authority.substr(1, rb - 1));
        if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
            port = std::string(authority.substr(rb + 2));
        }
    } else if (const std::size_t colon = authority.rfind(':');
               colon != std::string_view::npos) {
        host = std::string(authority.substr(0, colon));
        port = std::string(authority.substr(colon + 1));
    } else {
        host = std::string(authority);
    }

    if (host.empty()) {
        if (error) *error = "missing host in HTTP URL";
        return false;
    }

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "failed to resolve host: " + host;
        return false;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        if (error) *error = "failed to connect to " + host + ":" + port;
        return false;
    }

    const std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + std::string(authority) + "\r\n"
        "User-Agent: finn-ohos/1.0\r\n"
        "Connection: close\r\n\r\n";

    std::size_t sent = 0;
    while (sent < req.size()) {
        const ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, 0);
        if (n <= 0) {
            ::close(fd);
            if (error) *error = "failed to send HTTP request";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }

    constexpr std::size_t kMaxDownloadBytes = 256u * 1024u * 1024u;
    std::string response;
    char buf[16384];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            ::close(fd);
            if (error) *error = "socket read error during HTTP download";
            return false;
        }
        if (n == 0) break;
        if (response.size() + static_cast<std::size_t>(n) > kMaxDownloadBytes) {
            ::close(fd);
            if (error) *error = "HTTP response exceeds 256 MiB safety limit";
            return false;
        }
        response.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);

    const std::size_t hdr_end = response.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        if (error) *error = "malformed HTTP response (missing header terminator)";
        return false;
    }

    const std::string_view headers(response.data(), hdr_end);
    const std::size_t line_end = headers.find("\r\n");
    const std::string_view status_line =
        (line_end == std::string_view::npos) ? headers : headers.substr(0, line_end);
    const std::size_t sp1 = status_line.find(' ');
    if (sp1 == std::string_view::npos) {
        if (error) *error = "malformed HTTP status line";
        return false;
    }
    const int status_code = std::atoi(std::string(status_line.substr(sp1 + 1)).c_str());

    bool chunked = false;
    std::string location;
    std::size_t pos = (line_end == std::string_view::npos) ? headers.size() : line_end + 2;
    while (pos < headers.size()) {
        const std::size_t nl = headers.find("\r\n", pos);
        const std::string_view line =
            (nl == std::string_view::npos) ? headers.substr(pos)
                                           : headers.substr(pos, nl - pos);
        if (const std::size_t c = line.find(':'); c != std::string_view::npos) {
            std::string_view k = line.substr(0, c);
            std::string_view v = line.substr(c + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
            if (starts_with_ci(k, "location") && k.size() == 8) {
                location = std::string(v);
            } else if (starts_with_ci(k, "transfer-encoding") && k.size() == 17 &&
                       v.find("chunked") != std::string_view::npos) {
                chunked = true;
            }
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 2;
    }

    if ((status_code == 301 || status_code == 302 || status_code == 303 ||
         status_code == 307 || status_code == 308) &&
        !location.empty()) {
        std::string next_url = location;
        if (!location.empty() && location[0] == '/') {
            next_url = "http://" + std::string(authority) + location;
        }
        return download_http_native(next_url, dest_path, redirects_left - 1, error);
    }

    if (status_code != 200) {
        if (error) {
            *error = "HTTP download returned status " + std::to_string(status_code) +
                     " for " + url;
        }
        return false;
    }

    const std::string_view raw_body(response.data() + hdr_end + 4,
                                    response.size() - (hdr_end + 4));
    std::string decoded_body;
    std::string_view final_body = raw_body;
    if (chunked) {
        if (!decode_chunked_body(raw_body, &decoded_body)) {
            if (error) *error = "invalid chunked HTTP body";
            return false;
        }
        final_body = decoded_body;
    }

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "unable to create download file: " + dest_path;
        return false;
    }
    if (!final_body.empty()) {
        out.write(final_body.data(), static_cast<std::streamsize>(final_body.size()));
    }
    if (!out) {
        if (error) *error = "write error while saving downloaded package";
        return false;
    }
    return true;
}

void inspect_hap_zip_into_report(const ZipReader& zip, OhosDiagnosticReport* rep) {
    std::string mjson;
    if (zip.read_entry_text("module.json", &mjson)) {
        rep->manifest = parse_module_json(mjson);
    } else {
        rep->error = "package is missing module.json";
        return;
    }
    if (!rep->manifest.valid) {
        rep->error = rep->manifest.error;
        return;
    }

    // Check resources.index header if present
    std::vector<std::uint8_t> res_idx;
    if (zip.read_entry("resources.index", &res_idx)) {
        rep->resource_index = parse_resources_index_header(res_idx);
    }

    // Check ets/modules.abc
    std::vector<std::uint8_t> abc_bytes;
    std::vector<std::string> kit_scan_strings;
    kit_scan_strings.push_back(mjson);
    if (zip.read_entry("ets/modules.abc", &abc_bytes)) {
        rep->abc_present = true;
        const AbcFile abc = parse_abc_bytes(abc_bytes.data(), abc_bytes.size());
        rep->abc_valid = abc.valid;
        rep->abc_checksum_valid = abc.header.checksum_valid;
        rep->abc_version = abc.header.version_string;
        rep->abc_class_count = abc.classes.size();
        for (const auto& cls : abc.classes) rep->abc_method_count += cls.methods.size();
        for (const auto& s : abc.strings) kit_scan_strings.push_back(s);
        if (!abc.valid) {
            rep->warnings.push_back("ets/modules.abc invalid: " + abc.error);
        } else if (!abc.header.checksum_valid) {
            rep->warnings.push_back("ets/modules.abc Adler-32 checksum mismatch");
        }
    } else {
        rep->warnings.push_back("ets/modules.abc not found in package");
    }

    // Inspect native libs/<abi>/*.so ELF headers
    bool any_native = false;
    bool any_compatible_native = false;
    for (const auto& e : zip.entries()) {
        if (e.is_directory) continue;
        if (e.name.rfind("libs/", 0) == 0 && ends_with_ci(e.name, ".so")) {
            any_native = true;
            std::vector<std::uint8_t> so_bytes;
            if (zip.read_entry(e.name, &so_bytes)) {
                OhosNativeLibInfo lib = inspect_elf_so_bytes(e.name, so_bytes);
                if (lib.host_compatible) any_compatible_native = true;
                rep->native_libs.push_back(std::move(lib));
            }
        }
    }
    if (any_native && !any_compatible_native) {
        rep->warnings.push_back(
            "native .so libraries do not match host ABI (" + rep->host_abi + ")");
    }

    // Audit OpenHarmony kits
    const KitAudit kits = audit_ohos_kits(kit_scan_strings);
    rep->supported_kits = kits.supported;
    rep->unsupported_kits = kits.unsupported;
    rep->unknown_kits = kits.unknown;
    if (!kits.unknown.empty()) {
        rep->warnings.push_back(
            "unrecognised kit import(s) reported as unknown, not implemented: " +
            kits.unknown.front() +
            (kits.unknown.size() > 1
                 ? " (+" + std::to_string(kits.unknown.size() - 1) + " more)"
                 : ""));
    }

    // API compatibility check (OpenHarmony 6.1 LTS = API 23)
    const int min_api = rep->manifest.min_api_version;
    rep->api_compatible = (min_api <= kOhosSupportedApiLevel);
    if (!rep->api_compatible) {
        rep->warnings.push_back(
            "minAPIVersion (" + std::to_string(min_api) +
            ") exceeds supported OpenHarmony 6.1 LTS (API " +
            std::to_string(kOhosSupportedApiLevel) + ")");
    }

    rep->valid = true;
    rep->runnable = rep->api_compatible &&
                    (!rep->abc_present || rep->abc_valid) &&
                    (!any_native || any_compatible_native) &&
                    rep->unsupported_kits.empty();

    if (rep->runnable) {
        rep->summary = "Compatible with FinnOS OpenHarmony 6.1 LTS (API 23) runtime on " +
                       rep->host_abi;
    } else if (!rep->api_compatible) {
        rep->summary = "Requires API " + std::to_string(min_api) +
                       " (finn-ohos supports up to OpenHarmony 6.1 LTS / API 23)";
    } else if (!rep->unsupported_kits.empty()) {
        rep->summary = "Uses unimplemented system kit(s): " + rep->unsupported_kits[0] +
                       (rep->unsupported_kits.size() > 1
                            ? " (+" + std::to_string(rep->unsupported_kits.size() - 1) + " more)"
                            : "");
    } else if (any_native && !any_compatible_native) {
        rep->summary = "Native NAPI .so ABI mismatch (host requires " + rep->host_abi + ")";
    } else {
        rep->summary = "Package has bytecode or manifest warnings";
    }
}

// Shared pre-flight: a package that imports a known hardware/proprietary kit
// is refused rather than installed or launched with the kit silently missing.
std::string missing_kit_error(const std::vector<std::string>& kit_scan_strings) {
    const KitAudit audit = audit_ohos_kits(kit_scan_strings);
    if (audit.unsupported.empty()) return {};
    return "requires unimplemented system kit(s): " + audit.unsupported.front() +
           (audit.unsupported.size() > 1
                ? " (+" + std::to_string(audit.unsupported.size() - 1) + " more)"
                : "");
}

OhosInstallResult install_single_hap_zip(const ZipReader& zip,
                                         const std::string& src_hap_path,
                                         const std::string& ohos_root) {
    OhosInstallResult res;
    std::string mjson;
    if (!zip.read_entry_text("module.json", &mjson)) {
        res.error = "OpenHarmony package is missing module.json";
        return res;
    }
    const OhosModuleManifest m = parse_module_json(mjson);
    if (!m.valid) {
        res.error = m.error;
        return res;
    }

    std::vector<std::string> kit_strings{mjson};
    std::vector<std::uint8_t> abc_bytes;
    if (zip.read_entry("ets/modules.abc", &abc_bytes)) {
        kit_strings.emplace_back(abc_bytes.begin(), abc_bytes.end());
    }
    if (const std::string kit_err = missing_kit_error(kit_strings); !kit_err.empty()) {
        res.log.push_back("Refusing install: " + kit_err);
        res.error = kit_err;
        return res;
    }

    const fs::path apps_dir(default_ohos_apps_dir(ohos_root));
    const fs::path icons_dir(default_ohos_icons_dir(ohos_root));
    const std::string safe_bundle = sanitize_bundle_dirname(m.bundle_name);
    const fs::path install_dir = apps_dir / safe_bundle;

    std::error_code ec;
    fs::create_directories(install_dir, ec);
    if (ec) {
        res.error = "failed to create install directory: " + ec.message();
        return res;
    }

    const bool is_hsp = (m.module_type == "shared" || ends_with_ci(src_hap_path, ".hsp"));
    const fs::path extract_dir =
        is_hsp ? (install_dir / "hsp" / sanitize_bundle_dirname(m.module_name))
               : install_dir;
    fs::create_directories(extract_dir, ec);

    std::string ext_err;
    if (!zip.extract_all(extract_dir.string(), nullptr, &ext_err)) {
        res.error = "failed to extract package contents: " + ext_err;
        return res;
    }

    const fs::path stored_pkg =
        extract_dir / (is_hsp ? (sanitize_bundle_dirname(m.module_name) + ".hsp")
                              : "entry.hap");
    if (!src_hap_path.empty() && fs::is_regular_file(src_hap_path, ec)) {
        fs::copy_file(src_hap_path, stored_pkg, fs::copy_options::overwrite_existing, ec);
    }

    const std::string icon_path = extract_icon_from_zip(zip, m, icons_dir.string());
    if (!icon_path.empty()) {
        res.log.push_back("Extracted icon -> " + icon_path);
    }

    // Verify any host-compatible native .so libraries
    const std::string host_abi = host_ohos_abi();
    for (const auto& e : zip.entries()) {
        if (!e.is_directory && e.name.rfind("libs/", 0) == 0 && ends_with_ci(e.name, ".so")) {
            std::vector<std::uint8_t> so_bytes;
            if (zip.read_entry(e.name, &so_bytes)) {
                const OhosNativeLibInfo lib = inspect_elf_so_bytes(e.name, so_bytes);
                res.log.push_back(
                    "Native lib " + e.name + " (" + lib.elf_machine + ", " +
                    (lib.host_compatible ? "matches " + host_abi : "non-host ABI") + ")");
            }
        }
    }

    InstalledOhosApp app;
    app.id = "ohos:" + slugify_bundle(m.bundle_name);
    app.name = friendly_app_name(m);
    app.bundle_name = m.bundle_name;
    app.version_name = m.version_name;
    app.version_code = m.version_code;
    app.target_api_version = m.target_api_version;
    app.min_api_version = m.min_api_version;
    app.main_ability = !m.main_element.empty()
                           ? m.main_element
                           : (!m.abilities.empty() ? m.abilities[0].name : "EntryAbility");
    app.module_type = m.module_type.empty() ? "entry" : m.module_type;
    app.install_dir = install_dir.string();
    app.package_path = stored_pkg.string();
    app.icon_path = icon_path;
    if (is_hsp) app.shared_packages.push_back(stored_pkg.string());

    if (is_hsp) {
        res.log.push_back("Installed shared module " + m.module_name + " (" +
                          app.bundle_name + ", API " +
                          std::to_string(app.target_api_version) + ") -> " +
                          extract_dir.string());
    } else {
        res.log.push_back("Installed " + app.name + " (" + app.bundle_name +
                          ", API " + std::to_string(app.target_api_version) +
                          ") -> " + install_dir.string());
    }
    res.app = std::move(app);
    res.ok = true;
    return res;
}

}  // namespace

std::string default_ohos_root_dir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/finn/ohos";
    }
    return "/tmp/finn-ohos";
}

std::string default_ohos_apps_dir(const std::string& ohos_root) {
    const std::string root = ohos_root.empty() ? default_ohos_root_dir() : ohos_root;
    return (fs::path(root) / "apps").string();
}

std::string default_ohos_icons_dir(const std::string& ohos_root) {
    const std::string root = ohos_root.empty() ? default_ohos_root_dir() : ohos_root;
    return (fs::path(root) / "icons").string();
}

std::string default_ohos_registry_path(const std::string& ohos_root) {
    const std::string root = ohos_root.empty() ? default_ohos_root_dir() : ohos_root;
    return (fs::path(root) / "registry.conf").string();
}

bool is_http_url(std::string_view value) {
    return starts_with_ci(value, "http://") || starts_with_ci(value, "https://");
}

OhosPackageKind detect_ohos_package_kind(const std::string& path) {
    std::error_code ec;
    // A macOS .app bundle is a directory containing Contents/Info.plist — never an OHOS pack.
    if (fs::is_directory(path, ec)) return OhosPackageKind::Unknown;
    if (!ZipReader::has_zip_magic(path)) return OhosPackageKind::Unknown;

    ZipReader zip;
    if (!zip.open_file(path)) return OhosPackageKind::Unknown;

    if (ends_with_ci(path, ".hap")) {
        if (zip.has_entry("module.json") || zip.has_entry("config.json")) {
            return OhosPackageKind::Hap;
        }
        return OhosPackageKind::Unknown;
    }
    if (ends_with_ci(path, ".hsp")) {
        if (zip.has_entry("module.json")) return OhosPackageKind::Hsp;
        return OhosPackageKind::Unknown;
    }
    if (ends_with_ci(path, ".app")) {
        if (zip.has_entry("pack.info")) return OhosPackageKind::AppPack;
        for (const auto& e : zip.entries()) {
            if (!e.is_directory &&
                (ends_with_ci(e.name, ".hap") || ends_with_ci(e.name, ".hsp"))) {
                return OhosPackageKind::AppPack;
            }
        }
        return OhosPackageKind::Unknown;
    }

    // Extensionless or temp download file: inspect ZIP entries directly
    if (zip.has_entry("pack.info")) return OhosPackageKind::AppPack;
    for (const auto& e : zip.entries()) {
        if (!e.is_directory && ends_with_ci(e.name, ".hap")) {
            return OhosPackageKind::AppPack;
        }
    }
    if (zip.has_entry("module.json")) {
        std::string mj;
        if (zip.read_entry_text("module.json", &mj)) {
            const OhosModuleManifest m = parse_module_json(mj);
            if (m.valid && m.module_type == "shared") return OhosPackageKind::Hsp;
            if (m.valid) return OhosPackageKind::Hap;
        }
    }
    return OhosPackageKind::Unknown;
}

OhosModuleManifest parse_module_json(std::string_view json_text) {
    OhosModuleManifest m;
    std::string err;
    auto doc = parse_json5(json_text, &err);
    if (!doc || doc->kind != JsonValue::Kind::Object) {
        m.error = err.empty() ? "module.json is not a valid JSON/JSON5 object" : err;
        return m;
    }

    if (const JsonValue* app = doc->get("app"); app && app->kind == JsonValue::Kind::Object) {
        m.bundle_name = app->get_string("bundleName");
        m.bundle_type = app->get_string("bundleType", "app");
        m.vendor = app->get_string("vendor");
        m.version_code = static_cast<std::uint32_t>(std::max(0, app->get_int("versionCode", 0)));
        m.version_name = app->get_string("versionName");
        m.app_icon = app->get_string("icon");
        m.app_label = app->get_string("label");
        m.min_api_version = app->get_int("minAPIVersion", 0);
        m.target_api_version = app->get_int("targetAPIVersion", m.min_api_version);
        m.api_release_type = app->get_string("apiReleaseType", "Release");
    }

    if (const JsonValue* mod = doc->get("module"); mod && mod->kind == JsonValue::Kind::Object) {
        m.module_name = mod->get_string("name");
        m.module_type = mod->get_string("type", "entry");
        m.main_element = mod->get_string("mainElement");
        m.pages = mod->get_string("pages");

        if (const JsonValue* dt = mod->get("deviceTypes");
            dt && dt->kind == JsonValue::Kind::Array) {
            for (const auto& item : dt->array_val) {
                if (item.kind == JsonValue::Kind::String) {
                    m.device_types.push_back(item.str_val);
                }
            }
        }

        if (const JsonValue* abs = mod->get("abilities");
            abs && abs->kind == JsonValue::Kind::Array) {
            for (const auto& ab : abs->array_val) {
                if (ab.kind != JsonValue::Kind::Object) continue;
                OhosAbilityInfo info;
                info.name = ab.get_string("name");
                info.src_entry = ab.get_string("srcEntry");
                info.description = ab.get_string("description");
                info.icon = ab.get_string("icon");
                info.label = ab.get_string("label");
                info.start_window_icon = ab.get_string("startWindowIcon");
                info.start_window_background = ab.get_string("startWindowBackground");
                info.launch_type = ab.get_string("launchType", "singleton");
                info.exported = ab.get_bool("exported", false);
                info.allow_self_redirect = ab.get_bool("allowSelfRedirect", false);

                if (const JsonValue* skills = ab.get("skills");
                    skills && skills->kind == JsonValue::Kind::Array) {
                    for (const auto& sk : skills->array_val) {
                        if (sk.kind != JsonValue::Kind::Object) continue;
                        OhosSkill skill;
                        if (const JsonValue* ent = sk.get("entities");
                            ent && ent->kind == JsonValue::Kind::Array) {
                            for (const auto& v : ent->array_val) {
                                if (v.kind == JsonValue::Kind::String) {
                                    skill.entities.push_back(v.str_val);
                                }
                            }
                        }
                        if (const JsonValue* act = sk.get("actions");
                            act && act->kind == JsonValue::Kind::Array) {
                            for (const auto& v : act->array_val) {
                                if (v.kind == JsonValue::Kind::String) {
                                    skill.actions.push_back(v.str_val);
                                }
                            }
                        }
                        if (const JsonValue* uris = sk.get("uris");
                            uris && uris->kind == JsonValue::Kind::Array) {
                            for (const auto& v : uris->array_val) {
                                if (v.kind == JsonValue::Kind::String) {
                                    skill.uris.push_back(v.str_val);
                                } else if (v.kind == JsonValue::Kind::Object) {
                                    const std::string sch = v.get_string("scheme");
                                    const std::string host = v.get_string("host");
                                    if (!sch.empty()) {
                                        skill.uris.push_back(sch + "://" + host);
                                    }
                                }
                            }
                        }
                        info.skills.push_back(std::move(skill));
                    }
                }
                m.abilities.push_back(std::move(info));
            }
        }

        if (const JsonValue* perms = mod->get("requestPermissions");
            perms && perms->kind == JsonValue::Kind::Array) {
            for (const auto& p : perms->array_val) {
                if (p.kind == JsonValue::Kind::String) {
                    m.request_permissions.push_back(p.str_val);
                } else if (p.kind == JsonValue::Kind::Object) {
                    if (const std::string pn = p.get_string("name"); !pn.empty()) {
                        m.request_permissions.push_back(pn);
                    }
                }
            }
        }

        if (const JsonValue* deps = mod->get("dependencies");
            deps && deps->kind == JsonValue::Kind::Array) {
            for (const auto& d : deps->array_val) {
                if (d.kind != JsonValue::Kind::Object) continue;
                OhosDependency dep;
                dep.bundle_name = d.get_string("bundleName");
                dep.module_name = d.get_string("moduleName");
                dep.version_code = static_cast<std::uint32_t>(
                    std::max(0, d.get_int("versionCode", 0)));
                if (!dep.module_name.empty() || !dep.bundle_name.empty()) {
                    m.dependencies.push_back(std::move(dep));
                }
            }
        }
    }

    if (m.bundle_name.empty() || m.module_name.empty()) {
        m.error = "module.json missing required app.bundleName or module.name";
        return m;
    }
    m.valid = true;
    return m;
}

OhosPackInfo parse_pack_info(std::string_view json_text) {
    OhosPackInfo p;
    std::string err;
    auto doc = parse_json5(json_text, &err);
    if (!doc || doc->kind != JsonValue::Kind::Object) {
        p.error = err.empty() ? "pack.info is not a valid JSON object" : err;
        return p;
    }

    if (const JsonValue* summary = doc->get("summary");
        summary && summary->kind == JsonValue::Kind::Object) {
        if (const JsonValue* app = summary->get("app");
            app && app->kind == JsonValue::Kind::Object) {
            p.bundle_name = app->get_string("bundleName");
            p.bundle_type = app->get_string("bundleType", "app");
            p.min_api_version = app->get_int("minAPIVersion", 0);
            p.target_api_version = app->get_int("targetAPIVersion", 0);
            if (const JsonValue* ver = app->get("version");
                ver && ver->kind == JsonValue::Kind::Object) {
                p.version_code = static_cast<std::uint32_t>(
                    std::max(0, ver->get_int("code", 0)));
                p.version_name = ver->get_string("name");
            }
        }
        if (const JsonValue* mods = summary->get("modules");
            mods && mods->kind == JsonValue::Kind::Array) {
            for (const auto& mod : mods->array_val) {
                if (mod.kind != JsonValue::Kind::Object) continue;
                if (const JsonValue* api = mod.get("apiVersion");
                    api && api->kind == JsonValue::Kind::Object) {
                    if (p.min_api_version == 0) p.min_api_version = api->get_int("compatible", 0);
                    if (p.target_api_version == 0) p.target_api_version = api->get_int("target", 0);
                }
            }
        }
    }

    if (const JsonValue* pkgs = doc->get("packages");
        pkgs && pkgs->kind == JsonValue::Kind::Array) {
        for (const auto& item : pkgs->array_val) {
            if (item.kind != JsonValue::Kind::Object) continue;
            OhosPackModuleEntry entry;
            entry.name = item.get_string("name");
            entry.module_name = item.get_string("moduleName");
            entry.module_type = item.get_string("moduleType");
            if (const JsonValue* dt = item.get("deviceType");
                dt && dt->kind == JsonValue::Kind::Array) {
                for (const auto& v : dt->array_val) {
                    if (v.kind == JsonValue::Kind::String) entry.device_types.push_back(v.str_val);
                }
            }
            if (!entry.name.empty()) p.packages.push_back(std::move(entry));
        }
    }

    if (p.bundle_name.empty() && p.packages.empty()) {
        p.error = "pack.info missing summary.app.bundleName and packages";
        return p;
    }
    p.valid = true;
    return p;
}

OhosResourceIndexInfo parse_resources_index_header(const std::vector<std::uint8_t>& bytes) {
    OhosResourceIndexInfo info;
    info.present = !bytes.empty();
    if (bytes.size() < 136) return info;

    std::size_t vlen = 0;
    while (vlen < 128 && bytes[vlen] != 0u) ++vlen;
    info.version.assign(reinterpret_cast<const char*>(bytes.data()), vlen);
    info.file_size = read_le32(bytes.data() + 128);
    info.limit_key_count = read_le32(bytes.data() + 132);
    info.valid = (!info.version.empty() && info.file_size >= 136);
    return info;
}

OhosNativeLibInfo inspect_elf_so_bytes(const std::string& archive_path,
                                       const std::vector<std::uint8_t>& bytes) {
    OhosNativeLibInfo info;
    info.archive_path = archive_path;
    info.soname = fs::path(archive_path).filename().string();

    // Extract ABI directory segment: libs/<abi>/libfoo.so
    if (archive_path.rfind("libs/", 0) == 0) {
        const std::size_t slash2 = archive_path.find('/', 5);
        if (slash2 != std::string::npos) {
            info.abi = archive_path.substr(5, slash2 - 5);
        }
    }

    if (bytes.size() < 20 || std::memcmp(bytes.data(), kElfMagic, 4) != 0) {
        info.elf_machine = "invalid";
        return info;
    }

    info.valid_elf = true;
    const std::uint16_t machine = read_le16(bytes.data() + 18);
    std::string abi_from_elf;
    switch (machine) {
        case kElfMachineAarch64:
            info.elf_machine = "aarch64";
            abi_from_elf = "arm64-v8a";
            break;
        case kElfMachineX86_64:
            info.elf_machine = "x86_64";
            abi_from_elf = "x86_64";
            break;
        case kElfMachineArm:
            info.elf_machine = "arm";
            abi_from_elf = "armeabi-v7a";
            break;
        case kElfMachine386:
            info.elf_machine = "x86";
            abi_from_elf = "x86";
            break;
        case kElfMachineRiscv:
            info.elf_machine = "riscv64";
            abi_from_elf = "riscv64";
            break;
        default:
            info.elf_machine = "unknown(" + std::to_string(machine) + ")";
            break;
    }

    const std::string host_abi = host_ohos_abi();
    info.host_compatible = (!abi_from_elf.empty() && abi_from_elf == host_abi);
    return info;
}

OhosDiagnosticReport diagnose_ohos_package(const std::string& path) {
    OhosDiagnosticReport rep;
    rep.target_path = path;
    rep.host_cpu = host_cpu_machine();
    rep.host_abi = host_ohos_abi();
    rep.kind = detect_ohos_package_kind(path);

    if (rep.kind == OhosPackageKind::Unknown) {
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            rep.error = "path is a directory (macOS .app bundle), not an OpenHarmony ZIP package";
        } else {
            rep.error = "not a recognized OpenHarmony .hap, .hsp, or .app ZIP package: " + path;
        }
        return rep;
    }

    ZipReader zip;
    std::string zerr;
    if (!zip.open_file(path, &zerr)) {
        rep.error = zerr;
        return rep;
    }

    if (rep.kind == OhosPackageKind::AppPack) {
        std::string pinfo_text;
        if (zip.read_entry_text("pack.info", &pinfo_text)) {
            rep.pack_info = parse_pack_info(pinfo_text);
        }

        // Find primary entry .hap inside the .app pack
        std::string entry_hap;
        for (const auto& p : rep.pack_info.packages) {
            if (p.module_type == "entry" && ends_with_ci(p.name, ".hap") &&
                zip.has_entry(p.name)) {
                entry_hap = p.name;
                break;
            }
        }
        if (entry_hap.empty()) {
            for (const auto& e : zip.entries()) {
                if (!e.is_directory && ends_with_ci(e.name, ".hap")) {
                    entry_hap = e.name;
                    break;
                }
            }
        }
        if (entry_hap.empty()) {
            rep.error = "OpenHarmony .app pack contains no .hap module";
            return rep;
        }
        std::vector<std::uint8_t> hap_bytes;
        if (!zip.read_entry(entry_hap, &hap_bytes, &zerr)) {
            rep.error = "failed to read nested .hap (" + entry_hap + "): " + zerr;
            return rep;
        }
        ZipReader inner;
        if (!inner.open_memory(hap_bytes.data(), hap_bytes.size(), &zerr)) {
            rep.error = "corrupt nested .hap (" + entry_hap + "): " + zerr;
            return rep;
        }
        inspect_hap_zip_into_report(inner, &rep);
        return rep;
    }

    inspect_hap_zip_into_report(zip, &rep);
    return rep;
}

bool download_ohos_package(const std::string& url, const std::string& dest_path,
                           std::string* error) {
    return download_http_native(url, dest_path, 5, error);
}

OhosInstallResult install_ohos_package(const std::string& url_or_path,
                                       const std::string& ohos_root) {
    OhosInstallResult res;
    std::string local_path = url_or_path;
    fs::path temp_download;

    if (is_http_url(url_or_path)) {
        std::string ext = ".hap";
        std::string clean_url = url_or_path;
        if (const std::size_t q = clean_url.find_first_of("?#"); q != std::string::npos) {
            clean_url.resize(q);
        }
        if (ends_with_ci(clean_url, ".app")) ext = ".app";
        else if (ends_with_ci(clean_url, ".hsp")) ext = ".hsp";

        temp_download = fs::temp_directory_path() /
                        ("finn-ohos-dl-" + std::to_string(::getpid()) + ext);
        res.log.push_back("Downloading " + url_or_path + " ...");
        std::string dl_err;
        if (!download_ohos_package(url_or_path, temp_download.string(), &dl_err)) {
            std::error_code ec;
            fs::remove(temp_download, ec);
            res.error = dl_err;
            return res;
        }
        res.log.push_back("Downloaded -> " + temp_download.string());
        local_path = temp_download.string();
    }

    auto cleanup_temp = [&]() {
        if (!temp_download.empty()) {
            std::error_code ec;
            fs::remove(temp_download, ec);
        }
    };

    const OhosPackageKind kind = detect_ohos_package_kind(local_path);
    if (kind == OhosPackageKind::Unknown) {
        cleanup_temp();
        res.error = "not a valid OpenHarmony .hap, .hsp, or .app ZIP package: " + url_or_path;
        return res;
    }

    ZipReader zip;
    std::string zerr;
    if (!zip.open_file(local_path, &zerr)) {
        cleanup_temp();
        res.error = zerr;
        return res;
    }

    if (kind == OhosPackageKind::AppPack) {
        res.log.push_back("Unpacking OpenHarmony .app pack ...");
        std::string pinfo_text;
        if (zip.read_entry_text("pack.info", &pinfo_text)) {
            const OhosPackInfo pi = parse_pack_info(pinfo_text);
            if (pi.valid) {
                res.log.push_back("pack.info bundle: " + pi.bundle_name +
                                  " (" + std::to_string(pi.packages.size()) + " package(s))");
            }
        }

        const fs::path stage = fs::temp_directory_path() /
                               ("finn-ohos-pack-" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(stage, ec);
        fs::create_directories(stage, ec);

        std::vector<std::string> haps;
        std::vector<std::string> hsps;
        for (const auto& e : zip.entries()) {
            if (e.is_directory) continue;
            if (!ends_with_ci(e.name, ".hap") && !ends_with_ci(e.name, ".hsp")) continue;
            std::vector<std::uint8_t> bytes;
            if (!zip.read_entry(e.name, &bytes, &zerr)) {
                fs::remove_all(stage, ec);
                cleanup_temp();
                res.error = "failed reading inner package " + e.name + ": " + zerr;
                return res;
            }
            const fs::path out_file = stage / fs::path(e.name).filename();
            std::ofstream out(out_file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (ends_with_ci(e.name, ".hsp")) hsps.push_back(out_file.string());
            else haps.push_back(out_file.string());
        }

        if (haps.empty() && hsps.empty()) {
            fs::remove_all(stage, ec);
            cleanup_temp();
            res.error = "OpenHarmony .app pack contained no .hap or .hsp files";
            return res;
        }

        bool have_primary = false;
        std::vector<std::string> installed_hsps;

        for (const auto& hap_path : haps) {
            ZipReader hz;
            if (!hz.open_file(hap_path, &zerr)) continue;
            OhosInstallResult sub = install_single_hap_zip(hz, hap_path, ohos_root);
            for (auto& l : sub.log) res.log.push_back(std::move(l));
            if (sub.ok && (!have_primary || sub.app.module_type == "entry")) {
                res.app = std::move(sub.app);
                have_primary = true;
            }
        }
        for (const auto& hsp_path : hsps) {
            ZipReader sz;
            if (!sz.open_file(hsp_path, &zerr)) continue;
            OhosInstallResult sub = install_single_hap_zip(sz, hsp_path, ohos_root);
            for (auto& l : sub.log) res.log.push_back(std::move(l));
            if (sub.ok) {
                installed_hsps.push_back(sub.app.package_path);
                if (!have_primary) {
                    res.app = std::move(sub.app);
                    have_primary = true;
                }
            }
        }
        fs::remove_all(stage, ec);
        cleanup_temp();

        if (!have_primary) {
            res.error = "failed to install any module from .app pack";
            return res;
        }
        for (const auto& p : installed_hsps) {
            if (std::find(res.app.shared_packages.begin(),
                          res.app.shared_packages.end(), p) == res.app.shared_packages.end()) {
                res.app.shared_packages.push_back(p);
            }
        }
    } else {
        OhosInstallResult sub = install_single_hap_zip(zip, local_path, ohos_root);
        cleanup_temp();
        if (!sub.ok) {
            res.error = sub.error;
            return res;
        }
        for (auto& l : sub.log) res.log.push_back(std::move(l));
        res.app = std::move(sub.app);
    }

    // Persist in registry.conf (upsert by bundle_name)
    auto existing = discover_installed_ohos_apps(ohos_root);
    bool updated = false;
    for (auto& e : existing) {
        if (e.bundle_name == res.app.bundle_name) {
            // Preserve previously installed shared packages if adding an HSP/HAP
            for (const auto& sp : e.shared_packages) {
                if (std::find(res.app.shared_packages.begin(),
                              res.app.shared_packages.end(), sp) ==
                    res.app.shared_packages.end()) {
                    res.app.shared_packages.push_back(sp);
                }
            }
            e = res.app;
            updated = true;
            break;
        }
    }
    if (!updated) existing.push_back(res.app);

    std::string reg_err;
    if (!write_registry_file(default_ohos_registry_path(ohos_root), existing, &reg_err)) {
        res.error = reg_err;
        return res;
    }

    res.ok = true;
    return res;
}

bool uninstall_ohos_app(const std::string& bundle_or_id,
                        const std::string& ohos_root,
                        std::string* error) {
    auto apps = discover_installed_ohos_apps(ohos_root);
    auto it = std::find_if(apps.begin(), apps.end(), [&](const InstalledOhosApp& a) {
        return a.bundle_name == bundle_or_id || a.id == bundle_or_id;
    });
    if (it == apps.end()) {
        if (error) *error = "OpenHarmony app not installed: " + bundle_or_id;
        return false;
    }

    std::error_code ec;
    if (!it->install_dir.empty()) {
        fs::remove_all(it->install_dir, ec);
    }
    if (!it->icon_path.empty()) {
        fs::remove(it->icon_path, ec);
    }
    apps.erase(it);
    return write_registry_file(default_ohos_registry_path(ohos_root), apps, error);
}

std::vector<InstalledOhosApp> discover_installed_ohos_apps(const std::string& ohos_root) {
    std::vector<InstalledOhosApp> out;
    std::ifstream in(default_ohos_registry_path(ohos_root));
    if (!in) return out;

    std::string line;
    std::error_code ec;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto f = split_pipe_line(line);
        if (f.size() < 11) continue;
        if (!fs::is_directory(f[8], ec)) continue;

        InstalledOhosApp a;
        a.bundle_name = f[0];
        a.id = "ohos:" + slugify_bundle(a.bundle_name);
        a.name = f[1];
        a.version_name = f[2];
        a.version_code = static_cast<std::uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10));
        a.target_api_version = std::atoi(f[4].c_str());
        a.min_api_version = std::atoi(f[5].c_str());
        a.main_ability = f[6];
        a.module_type = f[7];
        a.install_dir = f[8];
        a.package_path = f[9];
        if (!f[10].empty() && fs::is_regular_file(f[10], ec)) {
            a.icon_path = f[10];
        }
        if (f.size() >= 12 && !f[11].empty()) {
            std::size_t start = 0;
            while (start <= f[11].size()) {
                const std::size_t semi = f[11].find(';', start);
                const std::string item = (semi == std::string::npos)
                                             ? f[11].substr(start)
                                             : f[11].substr(start, semi - start);
                if (!item.empty()) a.shared_packages.push_back(item);
                if (semi == std::string::npos) break;
                start = semi + 1;
            }
        }
        out.push_back(std::move(a));
    }

    std::sort(out.begin(), out.end(),
              [](const InstalledOhosApp& a, const InstalledOhosApp& b) {
                  return a.name < b.name;
              });
    return out;
}

int run_ohos_target(const std::string& target, int argc, char** argv,
                    bool headless, const std::string& ohos_root,
                    AbilityRunReport* report_out, std::string* error_out) {
    fs::path app_dir;
    fs::path temp_stage;
    std::error_code ec;

    // 1. Check installed apps by bundle_name or ohos:<slug>
    for (const auto& installed : discover_installed_ohos_apps(ohos_root)) {
        if (installed.bundle_name == target || installed.id == target) {
            app_dir = installed.install_dir;
            break;
        }
    }

    // 2. Otherwise check direct .hap/.hsp/.app file path
    if (app_dir.empty()) {
        const OhosPackageKind kind = detect_ohos_package_kind(target);
        if (kind == OhosPackageKind::Unknown) {
            if (error_out) {
                *error_out = "target is neither an installed OpenHarmony bundle nor a valid .hap/.app package: " +
                             target;
            }
            return 1;
        }
        temp_stage = fs::temp_directory_path() /
                     ("finn-ohos-run-" + std::to_string(::getpid()));
        fs::remove_all(temp_stage, ec);
        fs::create_directories(temp_stage, ec);

        ZipReader zip;
        std::string zerr;
        if (!zip.open_file(target, &zerr)) {
            fs::remove_all(temp_stage, ec);
            if (error_out) *error_out = zerr;
            return 1;
        }
        if (kind == OhosPackageKind::AppPack) {
            std::string entry_hap;
            for (const auto& e : zip.entries()) {
                if (!e.is_directory && ends_with_ci(e.name, ".hap")) {
                    entry_hap = e.name;
                    break;
                }
            }
            std::vector<std::uint8_t> hap_bytes;
            if (entry_hap.empty() || !zip.read_entry(entry_hap, &hap_bytes, &zerr)) {
                fs::remove_all(temp_stage, ec);
                if (error_out) *error_out = "failed to read entry .hap inside .app pack";
                return 1;
            }
            ZipReader inner;
            if (!inner.open_memory(hap_bytes.data(), hap_bytes.size(), &zerr) ||
                !inner.extract_all(temp_stage.string(), nullptr, &zerr)) {
                fs::remove_all(temp_stage, ec);
                if (error_out) *error_out = zerr;
                return 1;
            }
        } else {
            if (!zip.extract_all(temp_stage.string(), nullptr, &zerr)) {
                fs::remove_all(temp_stage, ec);
                if (error_out) *error_out = zerr;
                return 1;
            }
        }
        app_dir = temp_stage;
    }

    auto cleanup = [&]() {
        if (!temp_stage.empty()) {
            std::error_code ce;
            fs::remove_all(temp_stage, ce);
        }
    };

    // Read module.json
    const fs::path manifest_path = app_dir / "module.json";
    std::ifstream min(manifest_path);
    if (!min) {
        cleanup();
        if (error_out) *error_out = "missing module.json in " + app_dir.string();
        return 1;
    }
    std::ostringstream mss;
    mss << min.rdbuf();
    const OhosModuleManifest manifest = parse_module_json(mss.str());
    if (!manifest.valid) {
        cleanup();
        if (error_out) *error_out = manifest.error;
        return 1;
    }
    std::vector<std::string> kit_strings{mss.str()};

    if (manifest.min_api_version > kOhosSupportedApiLevel) {
        cleanup();
        if (error_out) {
            *error_out = "app requires minAPIVersion " +
                         std::to_string(manifest.min_api_version) +
                         " (finn-ohos supports up to API 23 / OpenHarmony 6.1 LTS)";
        }
        return 2;
    }

    ArkUiEngine arkui;
    arkui.set_title(friendly_app_name(manifest));
    arkui.set_resource_root(app_dir.string());

    AbcRuntime runtime;
    runtime.set_bundle_context(manifest.bundle_name, app_dir.string(), ohos_root);
    runtime.set_arkui(&arkui);

    // Load ets/modules.abc if present
    const fs::path abc_path = app_dir / "ets" / "modules.abc";
    if (fs::is_regular_file(abc_path, ec)) {
        AbcFile abc = parse_abc_file(abc_path.string());
        kit_strings.emplace_back(abc.raw_bytes.begin(), abc.raw_bytes.end());
        std::string aerr;
        if (!runtime.load_abc(std::move(abc), &aerr)) {
            cleanup();
            if (error_out) *error_out = "failed to load ets/modules.abc: " + aerr;
            return 1;
        }
    }

    if (const std::string kit_err = missing_kit_error(kit_strings); !kit_err.empty()) {
        cleanup();
        if (error_out) *error_out = kit_err;
        return 2;
    }

    // Attempt to load host-compatible native NAPI .so modules if present
    const fs::path native_dir = app_dir / "libs" / host_ohos_abi();
    if (fs::is_directory(native_dir, ec)) {
        for (const auto& ent : fs::directory_iterator(native_dir, ec)) {
            if (ec) break;
            if (ent.is_regular_file(ec) && ends_with_ci(ent.path().string(), ".so")) {
                std::string nerr;
                if (!runtime.load_native_module(ent.path().string(), &nerr)) {
                    runtime.log("[napi] skipped " + ent.path().filename().string() + ": " + nerr);
                }
            }
        }
    }

    AbilityRunReport report = runtime.run_ability_lifecycle(
        manifest, argc, argv, headless, /*auto_close=*/true);
    cleanup();

    if (report_out) *report_out = report;
    if (!report.ok) {
        if (error_out) *error_out = report.error;
        return 1;
    }
    return 0;
}

}  // namespace finn::ohos
