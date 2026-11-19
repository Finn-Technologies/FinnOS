#include "compat/windows_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;

namespace finn::compat {
namespace {

std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    return ascii_lower(haystack).find(ascii_lower(needle)) != std::string::npos;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    return ascii_lower(s.substr(0, prefix.size())) == ascii_lower(prefix);
}

std::string strip_quotes(std::string_view s) {
    const std::string_view t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        return std::string(t.substr(1, t.size() - 2));
    }
    return std::string(t);
}

// .reg files escape backslashes and quotes inside quoted strings.
std::string decode_reg_string(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char next = s[i + 1];
            if (next == '\\' || next == '"') {
                out.push_back(next);
                ++i;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::uint32_t parse_hex_u32(std::string_view s) {
    std::uint32_t value = 0;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == 'x' || c == 'X') continue;
        const int d = hex_digit(c);
        if (d < 0) return 0;
        value = (value << 4) | static_cast<std::uint32_t>(d);
    }
    return value;
}

std::vector<std::uint8_t> parse_hex_bytes(std::string_view s) {
    std::vector<std::uint8_t> out;
    int high = -1;
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\\') continue;
        const int d = hex_digit(c);
        if (d < 0) return {};
        if (high < 0) high = d;
        else {
            out.push_back(static_cast<std::uint8_t>((high << 4) | d));
            high = -1;
        }
    }
    return out;  // a trailing nibble is malformed and dropped
}

std::vector<std::string_view> reg_lines(std::string_view content) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= content.size()) {
        const std::size_t end = content.find('\n', start);
        std::string_view line = content.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return lines;
}

bool is_guid(std::string_view key) {
    if (key.size() != 38 || key.front() != '{' || key.back() != '}') return false;
    for (std::size_t i = 1; i + 1 < key.size(); ++i) {
        if (key[i] == '-') continue;
        if (hex_digit(key[i]) < 0) return false;
    }
    return true;
}

// ---- minimal IShellLink helpers -------------------------------------------

constexpr std::uint8_t kShellLinkClsid[16] = {
    0x00, 0x21, 0x20, 0x60, 0x2C, 0x72, 0x1B, 0x46,
    0xAE, 0xD9, 0x1A, 0x4B, 0x9D, 0x5F, 0x8C, 0x8A,
};

bool have(std::string_view b, std::size_t off, std::size_t len) {
    return off <= b.size() && len <= b.size() - off;
}

std::uint16_t u16(std::string_view b, std::size_t off) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[off + 1])) << 8);
}

std::uint32_t u32(std::string_view b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[off + 3])) << 24);
}

std::string cstring_at(std::string_view b, std::size_t off, std::size_t limit) {
    if (off >= limit || limit > b.size()) return {};
    std::size_t end = off;
    while (end < limit && b[end] != '\0') ++end;
    return std::string(b.substr(off, end - off));
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

std::string utf16le_to_utf8(std::string_view bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const std::uint16_t unit = u16(bytes, i);
        std::uint32_t cp = unit;
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < bytes.size()) {
            const std::uint16_t low = u16(bytes, i + 2);
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + ((static_cast<std::uint32_t>(unit) - 0xd800) << 10) +
                     (static_cast<std::uint32_t>(low) - 0xdc00);
                i += 2;
            } else {
                cp = 0xfffd;
            }
        } else if (unit >= 0xd800 && unit <= 0xdfff) {
            cp = 0xfffd;
        }
        append_utf8(out, cp);
    }
    return out;
}

// Reads one CountCharacters + characters block from StringData.
bool read_link_string(std::string_view bytes, std::size_t& off, bool unicode,
                      std::string& out) {
    if (!have(bytes, off, 2)) return false;
    const std::uint16_t count = u16(bytes, off);
    off += 2;
    const std::size_t byte_len = unicode ? static_cast<std::size_t>(count) * 2u
                                         : static_cast<std::size_t>(count);
    if (!have(bytes, off, byte_len)) return false;
    out = unicode ? utf16le_to_utf8(bytes.substr(off, byte_len))
                  : std::string(bytes.substr(off, byte_len));
    off += byte_len;
    return true;
}

}  // namespace

const RegistryValue* RegistryKey::find(std::string_view name) const {
    const std::string wanted = ascii_lower(name);
    for (const auto& value : values) {
        if (ascii_lower(value.name) == wanted) return &value;
    }
    return nullptr;
}

std::string RegistryKey::string_value(std::string_view name) const {
    const RegistryValue* value = find(name);
    return value ? value->text : std::string{};
}

std::vector<RegistryKey> parse_wine_reg_file(std::string_view content) {
    std::vector<RegistryKey> keys;
    RegistryKey* current = nullptr;
    const std::vector<std::string_view> lines = reg_lines(content);

    for (std::size_t line_i = 0; line_i < lines.size(); ++line_i) {
        std::string_view raw = trim(lines[line_i]);
        if (raw.empty() || raw.front() == ';' || raw.front() == '#') continue;
        if (raw.front() == '[' && raw.back() == ']') {
            std::string_view path = trim(raw.substr(1, raw.size() - 2));
            if (!path.empty() && path.front() == '-') {
                current = nullptr;  // deleted key
                continue;
            }
            if (path.empty()) {
                current = nullptr;
                continue;
            }
            keys.push_back(RegistryKey{std::string(path), {}});
            current = &keys.back();
            continue;
        }
        if (current == nullptr) continue;

        // Value name: @ (default) or a quoted string.
        std::string name;
        std::size_t i = 0;
        if (raw[i] == '@') {
            i = 1;
        } else if (raw[i] == '"') {
            ++i;
            while (i < raw.size()) {
                if (raw[i] == '\\' && i + 1 < raw.size() &&
                    (raw[i + 1] == '"' || raw[i + 1] == '\\')) {
                    name.push_back(raw[i + 1]);
                    i += 2;
                } else if (raw[i] == '"') {
                    ++i;
                    break;
                } else {
                    name.push_back(raw[i++]);
                }
            }
        } else {
            continue;
        }
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
        if (i >= raw.size() || raw[i] != '=') continue;
        std::string_view rest = trim(raw.substr(i + 1));
        if (rest == "-") continue;  // deleted value

        RegistryValue value;
        value.name = name;
        if (!rest.empty() && rest.front() == '"') {
            std::size_t q = 1;
            std::string decoded;
            while (q < rest.size()) {
                if (rest[q] == '\\' && q + 1 < rest.size() &&
                    (rest[q + 1] == '"' || rest[q + 1] == '\\')) {
                    decoded.push_back(rest[q + 1]);
                    q += 2;
                } else if (rest[q] == '"') {
                    break;
                } else {
                    decoded.push_back(rest[q++]);
                }
            }
            value.kind = RegistryValue::Kind::String;
            value.text = decoded;
        } else if (starts_with_ci(rest, "dword:")) {
            value.kind = RegistryValue::Kind::Dword;
            value.text = std::string(trim(rest.substr(6)));
            value.dword = parse_hex_u32(rest.substr(6));
        } else if (starts_with_ci(rest, "hex")) {
            const std::size_t colon = rest.find(':');
            if (colon == std::string_view::npos) continue;
            std::string data(trim(rest.substr(colon + 1)));
            // A trailing '\' continues a hex value on the following line.
            while (!data.empty() && data.back() == '\\' && line_i + 1 < lines.size()) {
                data.pop_back();
                const std::string_view head = trim(data);
                data.assign(head);
                data += std::string(trim(lines[++line_i]));
            }
            value.kind = RegistryValue::Kind::Binary;
            value.bytes = parse_hex_bytes(data);
        } else if (!rest.empty()) {
            value.kind = RegistryValue::Kind::Unknown;
            value.text = decode_reg_string(rest);
        } else {
            value.kind = RegistryValue::Kind::String;
        }
        current->values.push_back(std::move(value));
    }
    return keys;
}

std::vector<UninstallEntry> collect_uninstall_entries(
    const std::vector<RegistryKey>& keys) {
    std::vector<UninstallEntry> entries;
    for (const auto& key : keys) {
        const std::string lower = ascii_lower(key.path);
        const std::size_t pos = lower.rfind("\\uninstall");
        if (pos == std::string_view::npos) continue;
        const std::size_t after = pos + 10;
        if (after < lower.size() && lower[after] != '\\') continue;
        const std::size_t slash = key.path.rfind('\\');
        const std::string name = slash == std::string::npos
                                     ? key.path
                                     : key.path.substr(slash + 1);
        if (ascii_lower(name) == "uninstall") continue;  // parent key, not an app

        UninstallEntry entry;
        entry.key = name;
        entry.key_path = key.path;
        entry.display_name = key.string_value("DisplayName");
        entry.publisher = key.string_value("Publisher");
        entry.display_version = key.string_value("DisplayVersion");
        entry.icon_path = key.string_value("DisplayIcon");
        entry.install_location = key.string_value("InstallLocation");
        entry.uninstall_string = key.string_value("UninstallString");
        entry.quiet_uninstall_string = key.string_value("QuietUninstallString");
        if (entry.display_name.empty() && entry.uninstall_string.empty() &&
            entry.icon_path.empty() && entry.install_location.empty()) {
            continue;  // a key with no usable metadata is not an app claim
        }
        entry.is_msi = is_guid(name) || contains_ci(entry.uninstall_string, "msiexec");
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<UninstallEntry> collect_uninstall_entries_from_file(
    const std::string& path, std::string* reason) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (reason) *reason = "cannot read registry file: " + path;
        return {};
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    auto entries = collect_uninstall_entries(parse_wine_reg_file(content));
    for (auto& entry : entries) entry.source_reg_file = path;
    if (reason) {
        *reason = entries.empty() ? "no Uninstall keys in " + path
                                  : "parsed " + std::to_string(entries.size()) +
                                        " Uninstall key(s) from " + path;
    }
    return entries;
}

std::vector<UninstallEntry> collect_uninstall_entries_from_prefix(
    const std::string& prefix, std::string* reason) {
    std::vector<UninstallEntry> all;
    if (prefix.empty()) {
        if (reason) *reason = "empty Wine prefix path";
        return all;
    }
    std::error_code dir_ec;
    if (!fs::is_directory(prefix, dir_ec)) {
        if (reason) *reason = "Wine prefix does not exist: " + prefix;
        return all;
    }

    std::vector<fs::path> files;
    for (const char* name : {"user.reg", "system.reg", "userdef.reg"}) {
        files.push_back(fs::path(prefix) / name);
    }
    // Other .reg files left directly in the prefix are also worth reading.
    std::error_code iter_ec;
    fs::directory_iterator it(prefix, iter_ec);
    const fs::directory_iterator end;
    if (!iter_ec) {
        for (; it != end; it.increment(iter_ec)) {
            if (iter_ec) break;
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec)) continue;
            if (ascii_lower(it->path().extension().string()) != ".reg") continue;
            if (std::find(files.begin(), files.end(), it->path()) == files.end()) {
                files.push_back(it->path());
            }
        }
    }

    int readable = 0;
    for (const auto& file : files) {
        std::error_code reg_ec;
        if (!fs::is_regular_file(file, reg_ec)) continue;
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::string content;
        content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        ++readable;
        auto entries = collect_uninstall_entries(parse_wine_reg_file(content));
        for (auto& entry : entries) {
            entry.source_reg_file = file.string();
            const bool duplicate =
                std::any_of(all.begin(), all.end(), [&](const UninstallEntry& prior) {
                    return prior.key_path == entry.key_path &&
                           prior.display_name == entry.display_name;
                });
            if (!duplicate) all.push_back(std::move(entry));
        }
    }

    if (reason) {
        if (readable == 0) {
            *reason = "no text .reg files found in Wine prefix: " + prefix;
        } else if (all.empty()) {
            *reason = "no Uninstall keys found in " + std::to_string(readable) +
                      " registry file(s) of prefix " + prefix;
        } else {
            *reason = "found " + std::to_string(all.size()) + " Uninstall key(s) in " +
                      prefix;
        }
    }
    return all;
}

std::string uninstall_icon_path(const UninstallEntry& entry) {
    if (entry.icon_path.empty()) return {};
    std::string icon = strip_quotes(entry.icon_path);
    const std::size_t comma = icon.rfind(',');
    if (comma != std::string_view::npos &&
        icon.find_first_not_of("0123456789 ", comma + 1) == std::string::npos) {
        icon.resize(comma);
    }
    return strip_quotes(icon);
}

std::string uninstall_exe_path(const UninstallEntry& entry) {
    const std::string icon = uninstall_icon_path(entry);
    if (ascii_lower(icon).size() >= 4 &&
        ascii_lower(icon).substr(icon.size() - 4) == ".exe") {
        return icon;
    }
    const std::string_view text = trim(entry.uninstall_string);
    if (text.empty()) return {};
    std::string_view token;
    if (text.front() == '"') {
        const std::size_t close = text.find('"', 1);
        if (close == std::string_view::npos) return {};
        token = text.substr(1, close - 1);
    } else {
        const std::size_t space = text.find_first_of(" \t");
        token = space == std::string_view::npos ? text : text.substr(0, space);
    }
    const std::string exe = strip_quotes(token);
    const std::string lower = ascii_lower(exe);
    return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".exe" ? exe : std::string{};
}

WindowsShortcutInfo parse_windows_shortcut_bytes(std::string_view bytes) {
    WindowsShortcutInfo info;
    if (bytes.size() < 76) {
        info.error = "too short for a ShellLinkHeader";
        return info;
    }
    // Compare as unsigned bytes: comparing uint8_t against a possibly signed
    // char makes CLSIDs with a high bit set fail on platforms with signed char.
    bool clsid_ok = true;
    for (std::size_t i = 0; i < sizeof(kShellLinkClsid); ++i) {
        clsid_ok = clsid_ok &&
                   static_cast<std::uint8_t>(bytes[i]) == kShellLinkClsid[i];
    }
    if (!clsid_ok) {
        info.error = "not a Windows shortcut (bad ShellLink CLSID)";
        return info;
    }

    const std::uint32_t flags = u32(bytes, 20);
    const bool unicode = (flags & 0x80u) != 0;
    std::size_t off = 76;

    if ((flags & 0x1u) != 0) {  // HasLinkTargetIDList
        if (!have(bytes, off, 2)) {
            info.error = "truncated LinkTargetIDList size";
            return info;
        }
        const std::size_t idl_size = u16(bytes, off);
        off += 2;
        if (!have(bytes, off, idl_size)) {
            info.error = "truncated LinkTargetIDList";
            return info;
        }
        off += idl_size;
        info.warnings.push_back("Link target ID List is not resolved; only LinkInfo and RelativePath are used.");
    }

    std::string relative_path;
    if ((flags & 0x2u) != 0) {  // HasLinkInfo
        if (!have(bytes, off, 4)) {
            info.error = "truncated LinkInfo size";
            return info;
        }
        const std::size_t info_size = u32(bytes, off);
        if (info_size < 0x1cu || !have(bytes, off, info_size)) {
            info.error = "invalid LinkInfo size";
            return info;
        }
        const std::string_view link_info = bytes.substr(off, info_size);
        const std::uint32_t info_flags = u32(link_info, 8);
        if ((info_flags & 0x1u) != 0) {  // VolumeIDAndLocalBasePath
            const std::size_t base_off = u32(link_info, 16);
            const std::size_t suffix_off = u32(link_info, 24);
            if (base_off < info_size) {
                info.target_path = cstring_at(link_info, base_off, info_size);
            }
            if (suffix_off < info_size) {
                const std::string suffix = cstring_at(link_info, suffix_off, info_size);
                if (!suffix.empty()) {
                    if (!info.target_path.empty() && suffix.front() != '\\') {
                        info.target_path.push_back('\\');
                    }
                    info.target_path += suffix;
                }
            }
        } else {
            info.warnings.push_back("LinkInfo has no local base path (network shortcuts are not resolved).");
        }
        off += info_size;
    }

    // StringData order is fixed by the format: NAME, RELATIVE_PATH, WORKING_DIR,
    // COMMAND_LINE_ARGUMENTS, ICON_LOCATION.
    std::string ignored_name;
    if ((flags & 0x4u) != 0 && !read_link_string(bytes, off, unicode, ignored_name)) {
        info.error = "truncated NAME_STRING";
        return info;
    }
    if ((flags & 0x8u) != 0 && !read_link_string(bytes, off, unicode, relative_path)) {
        info.error = "truncated RELATIVE_PATH";
        return info;
    }
    if ((flags & 0x10u) != 0 && !read_link_string(bytes, off, unicode, info.working_dir)) {
        info.error = "truncated WORKING_DIR";
        return info;
    }
    if ((flags & 0x20u) != 0 && !read_link_string(bytes, off, unicode, info.arguments)) {
        info.error = "truncated COMMAND_LINE_ARGUMENTS";
        return info;
    }
    if ((flags & 0x40u) != 0 && !read_link_string(bytes, off, unicode, info.icon_location)) {
        info.error = "truncated ICON_LOCATION";
        return info;
    }

    if (info.target_path.empty() && !relative_path.empty()) {
        info.target_path = relative_path;  // honest fallback, not ID List resolution
    }
    if (!unicode && (flags & 0x7fu) != 0) {
        info.warnings.push_back("ANSI strings were decoded byte-for-byte; the system code page was not applied.");
    }
    if (!info.icon_location.empty()) {
        info.warnings.push_back("Icon extraction (.ico) is not implemented; only the icon path is reported.");
    }
    info.warnings.push_back("ExtraData blocks are ignored.");

    if (info.target_path.empty()) {
        info.error = "no target path in LinkInfo or RelativePath (ID List resolution not implemented)";
        return info;
    }
    info.valid = true;
    return info;
}

WindowsShortcutInfo parse_windows_shortcut_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        WindowsShortcutInfo info;
        info.error = "cannot open shortcut: " + path;
        return info;
    }
    std::string bytes;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (bytes.size() > 1024u * 1024u) bytes.resize(1024u * 1024u);
    return parse_windows_shortcut_bytes(bytes);
}

bool windows_registry_self_test() {
    const std::string reg_text =
        "Windows Registry Editor Version 5.00\n"
        "\n"
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TestApp]\n"
        "\"DisplayName\"=\"Test App\"\n"
        "\"Publisher\"=\"Finn\"\n"
        "\"DisplayVersion\"=\"1.2\"\n"
        "\"DisplayIcon\"=\"C:\\\\Program Files\\\\TestApp\\\\app.exe,0\"\n"
        "\"InstallLocation\"=\"C:\\\\Program Files\\\\TestApp\"\n"
        "\"UninstallString\"=\"msiexec /x {11111111-2222-3333-4444-555555555555} /qn\"\n";
    const auto keys = parse_wine_reg_file(reg_text);
    if (keys.size() != 1) return false;
    const auto entries = collect_uninstall_entries(keys);
    if (entries.size() != 1) return false;
    const auto& entry = entries[0];
    if (entry.key != "TestApp" || entry.display_name != "Test App") return false;
    if (entry.publisher != "Finn" || entry.display_version != "1.2") return false;
    if (!entry.is_msi) return false;
    if (uninstall_icon_path(entry) != "C:\\Program Files\\TestApp\\app.exe") return false;
    if (uninstall_exe_path(entry) != "C:\\Program Files\\TestApp\\app.exe") return false;

    // Build a minimal Unicode .lnk with RelativePath, WorkingDir and Arguments.
    std::string lnk(76, '\0');
    std::copy(std::begin(kShellLinkClsid), std::end(kShellLinkClsid), lnk.begin());
    const std::uint32_t link_flags = 0x08u | 0x10u | 0x20u | 0x80u;
    for (unsigned i = 0; i < 4; ++i) {
        lnk[20 + i] = static_cast<char>((link_flags >> (8 * i)) & 0xffu);
    }
    const auto append_utf16 = [](std::string& out, std::string_view text) {
        const std::uint16_t count = static_cast<std::uint16_t>(text.size());
        out.push_back(static_cast<char>(count & 0xffu));
        out.push_back(static_cast<char>((count >> 8) & 0xffu));
        for (char c : text) {
            out.push_back(c);
            out.push_back('\0');
        }
    };
    append_utf16(lnk, "app.exe");
    append_utf16(lnk, "D:\\apps");
    append_utf16(lnk, "--flag");

    const auto shortcut = parse_windows_shortcut_bytes(lnk);
    if (!shortcut.valid) return false;
    if (shortcut.target_path != "app.exe") return false;
    if (shortcut.working_dir != "D:\\apps") return false;
    if (shortcut.arguments != "--flag") return false;

    std::string bad = lnk;  // byte 0 of the CLSID is already zero; corrupt byte 1
    bad[1] = '\0';
    if (parse_windows_shortcut_bytes(bad).valid) return false;
    return true;
}

}  // namespace finn::compat
