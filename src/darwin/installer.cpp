#include "darwin/installer.hpp"

#include "compat/desktop_entry.hpp"
#include "darwin/icns.hpp"
#include "darwin/plist.hpp"

#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::darwin {
namespace {

std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t read_u64_be(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(read_u32_be(p)) << 32) | read_u32_be(p + 4);
}

std::string slugify(std::string_view s) {
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
    return out.empty() ? "macos-app" : out;
}

bool zlib_decompress(const std::uint8_t* src, std::size_t src_len,
                     int window_bits, std::vector<std::uint8_t>* out,
                     std::size_t max_bytes = 256u * 1024u * 1024u) {
    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    strm.avail_in = static_cast<uInt>(src_len);
    if (::inflateInit2(&strm, window_bits) != Z_OK) return false;

    out->clear();
    std::uint8_t chunk[65536];
    int rc = Z_OK;
    while (rc == Z_OK) {
        strm.next_out = chunk;
        strm.avail_out = sizeof(chunk);
        rc = ::inflate(&strm, Z_NO_FLUSH);
        const std::size_t have = sizeof(chunk) - strm.avail_out;
        if (out->size() + have > max_bytes) {
            ::inflateEnd(&strm);
            return false;
        }
        out->insert(out->end(), chunk, chunk + have);
    }
    ::inflateEnd(&strm);
    return rc == Z_STREAM_END;
}

std::string extract_xml_tag_text(std::string_view xml, std::string_view tag,
                                 std::size_t start_pos, std::size_t limit_pos) {
    const std::string open = "<" + std::string(tag);
    const std::string close = "</" + std::string(tag) + ">";
    const std::size_t p = xml.find(open, start_pos);
    if (p == std::string_view::npos || p >= limit_pos) return {};
    const std::size_t gt = xml.find('>', p + open.size());
    if (gt == std::string_view::npos || gt >= limit_pos) return {};
    const std::size_t end = xml.find(close, gt + 1);
    if (end == std::string_view::npos || end > limit_pos) return {};
    return std::string(xml.substr(gt + 1, end - gt - 1));
}

std::string extract_encoding_style(std::string_view xml, std::size_t start_pos,
                                   std::size_t limit_pos) {
    const std::size_t p = xml.find("<encoding ", start_pos);
    if (p == std::string_view::npos || p >= limit_pos) return {};
    const std::size_t st = xml.find("style=\"", p);
    if (st == std::string_view::npos || st >= limit_pos) return {};
    const std::size_t q = xml.find('"', st + 7);
    if (q == std::string_view::npos || q > limit_pos) return {};
    return std::string(xml.substr(st + 7, q - (st + 7)));
}

// Safely parses POSIX cpio archives (odc "070707" and SVR4 newc "070701"/"070702").
bool extract_cpio_bytes(const std::vector<std::uint8_t>& cpio,
                        const fs::path& dest_root,
                        std::string* error) {
    std::size_t pos = 0;
    const auto safe_path = [&](std::string_view raw) -> fs::path {
        fs::path rel;
        for (const auto& part : fs::path(raw)) {
            const std::string s = part.string();
            if (s.empty() || s == "." || s == ".." || s == "/") continue;
            rel /= part;
        }
        return dest_root / rel;
    };

    while (pos + 6 <= cpio.size()) {
        const std::string_view magic(
            reinterpret_cast<const char*>(cpio.data() + pos), 6);
        std::uint64_t mode = 0;
        std::uint64_t namesize = 0;
        std::uint64_t filesize = 0;
        std::size_t header_len = 0;
        std::size_t align = 1;

        if (magic == "070707") {
            // Portable ASCII cpio (odc): 76-byte header, octal fields
            if (pos + 76 > cpio.size()) break;
            const auto parse_oct = [&](std::size_t off, std::size_t len) -> std::uint64_t {
                std::string s(reinterpret_cast<const char*>(cpio.data() + pos + off), len);
                return std::strtoull(s.c_str(), nullptr, 8);
            };
            mode = parse_oct(18, 6);
            namesize = parse_oct(59, 6);
            filesize = parse_oct(65, 11);
            header_len = 76;
            align = 1;
        } else if (magic == "070701" || magic == "070702") {
            // SVR4 newc cpio: 110-byte header, hex fields, 4-byte aligned
            if (pos + 110 > cpio.size()) break;
            const auto parse_hex = [&](std::size_t off, std::size_t len) -> std::uint64_t {
                std::string s(reinterpret_cast<const char*>(cpio.data() + pos + off), len);
                return std::strtoull(s.c_str(), nullptr, 16);
            };
            mode = parse_hex(14, 8);
            filesize = parse_hex(54, 8);
            namesize = parse_hex(94, 8);
            header_len = 110;
            align = 4;
        } else {
            if (error) *error = "unsupported cpio archive format";
            return false;
        }

        const std::size_t name_off = pos + header_len;
        if (namesize == 0 || name_off + namesize > cpio.size()) break;
        std::string name(
            reinterpret_cast<const char*>(cpio.data() + name_off),
            static_cast<std::size_t>(namesize - 1));

        std::size_t data_off = name_off + static_cast<std::size_t>(namesize);
        if (align > 1) data_off = (data_off + align - 1) & ~(align - 1);
        if (data_off + filesize > cpio.size()) break;

        if (name == "TRAILER!!!") return true;

        const fs::path target = safe_path(name);
        std::error_code ec;
        const std::uint64_t ftype = mode & 0170000u;
        if (ftype == 0040000u) {
            fs::create_directories(target, ec);
        } else if (ftype == 0100000u || ftype == 0) {
            fs::create_directories(target.parent_path(), ec);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (out && filesize > 0) {
                out.write(reinterpret_cast<const char*>(cpio.data() + data_off),
                          static_cast<std::streamsize>(filesize));
            }
            if ((mode & 0111u) != 0) {
                fs::permissions(target,
                                fs::perms::owner_exec | fs::perms::group_exec |
                                    fs::perms::others_exec,
                                fs::perm_options::add, ec);
            }
        } else if (ftype == 0120000u && filesize > 0) {
            fs::create_directories(target.parent_path(), ec);
            std::string link_target(
                reinterpret_cast<const char*>(cpio.data() + data_off),
                static_cast<std::size_t>(filesize));
            fs::remove(target, ec);
            fs::create_symlink(link_target, target, ec);
        }

        pos = data_off + static_cast<std::size_t>(filesize);
        if (align > 1) pos = (pos + align - 1) & ~(align - 1);
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

InstalledMacosApp read_installed_app_meta(const fs::path& bundle_path,
                                          const fs::path& icons_dir) {
    InstalledMacosApp app;
    app.bundle_path = bundle_path.string();
    const fs::path plist_path = bundle_path / "Contents" / "Info.plist";
    auto plist = parse_plist_file(plist_path.string());
    if (!plist) return app;

    app.name = plist->get_string("CFBundleDisplayName");
    if (app.name.empty()) app.name = plist->get_string("CFBundleName");
    if (app.name.empty()) app.name = bundle_path.stem().string();
    app.bundle_id = plist->get_string("CFBundleIdentifier");
    app.version = plist->get_string("CFBundleShortVersionString");
    if (app.version.empty()) app.version = plist->get_string("CFBundleVersion");

    const std::string slug = slugify(app.bundle_id.empty() ? app.name : app.bundle_id);
    app.id = "macos:" + slug;

    const std::string exe_name = plist->get_string("CFBundleExecutable");
    if (!exe_name.empty()) {
        const fs::path exe = bundle_path / "Contents" / "MacOS" / exe_name;
        std::error_code ec;
        if (fs::is_regular_file(exe, ec)) app.executable_path = exe.string();
    }

    // Extract .icns -> cached PNG icon if not already cached
    std::string icon_file = plist->get_string("CFBundleIconFile");
    if (!icon_file.empty()) {
        if (icon_file.size() < 5 || icon_file.substr(icon_file.size() - 5) != ".icns") {
            icon_file += ".icns";
        }
        const fs::path icns = bundle_path / "Contents" / "Resources" / icon_file;
        std::error_code ec;
        if (fs::is_regular_file(icns, ec) && !icons_dir.empty()) {
            const fs::path out_png = icons_dir / (slug + ".png");
            if (fs::is_regular_file(out_png, ec) ||
                extract_icns_to_png(icns.string(), out_png.string())) {
                app.icon_path = out_png.string();
            }
        }
    }
    return app;
}

void find_app_bundles_recursive(const fs::path& dir, std::vector<fs::path>* out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        const fs::path p = entry.path();
        if (p.extension() == ".app" &&
            fs::is_regular_file(p / "Contents" / "Info.plist", ec)) {
            out->push_back(p);
        } else {
            find_app_bundles_recursive(p, out);
        }
    }
}

}  // namespace

std::string default_macos_apps_dir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/finn/macos/Applications";
    }
    return "/tmp/finn-macos-apps";
}

std::string default_macos_icons_dir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::string(home) + "/.local/share/finn/macos/icons";
    }
    return "/tmp/finn-macos-icons";
}

DmgInspection inspect_dmg_native(const std::string& dmg_path) {
    DmgInspection res;
    std::ifstream in(dmg_path, std::ios::binary | std::ios::ate);
    if (!in) {
        res.error = "unable to open .dmg file: " + dmg_path;
        return res;
    }
    const std::streamoff size = in.tellg();
    if (size < 512) {
        res.error = "file smaller than 512-byte UDIF koly trailer";
        return res;
    }

    std::uint8_t koly[512] = {};
    in.seekg(size - 512, std::ios::beg);
    in.read(reinterpret_cast<char*>(koly), 512);
    if (in.gcount() != 512 || std::memcmp(koly, "koly", 4) != 0) {
        res.error = "missing Apple UDIF 'koly' trailer";
        return res;
    }

    res.data_fork_length = read_u64_be(koly + 32);
    const std::uint64_t xml_off = read_u64_be(koly + 216);
    const std::uint64_t xml_len = read_u64_be(koly + 224);
    res.sector_count = read_u64_be(koly + 492);
    res.valid = true;

    if (xml_len > 0 && xml_len < 16u * 1024u * 1024u &&
        xml_off + xml_len <= static_cast<std::uint64_t>(size)) {
        std::string xml(static_cast<std::size_t>(xml_len), '\0');
        in.seekg(static_cast<std::streamoff>(xml_off), std::ios::beg);
        in.read(xml.data(), static_cast<std::streamsize>(xml_len));
        if (auto plist = parse_plist_bytes(xml)) {
            if (const PlistValue* rf = plist->get("resource-fork")) {
                if (const PlistValue* blkx = rf->get("blkx")) {
                    for (const auto& entry : blkx->array_val) {
                        DmgPartitionSummary part;
                        part.name = entry.get_string("Name", "Partition");
                        if (const PlistValue* data = entry.get("Data");
                            data && data->kind == PlistValue::Kind::Data &&
                            data->data_val.size() >= 204 &&
                            std::memcmp(data->data_val.data(), "mish", 4) == 0) {
                            part.sectors = read_u64_be(data->data_val.data() + 16);
                            const std::uint32_t run_count =
                                read_u32_be(data->data_val.data() + 200);
                            bool has_zlib = false;
                            bool has_bz2 = false;
                            bool has_lzfse = false;
                            bool has_raw = false;
                            for (std::uint32_t r = 0; r < run_count; ++r) {
                                const std::size_t roff = 204 + static_cast<std::size_t>(r) * 40;
                                if (roff + 40 > data->data_val.size()) break;
                                const std::uint32_t rtype =
                                    read_u32_be(data->data_val.data() + roff);
                                const std::uint64_t clen =
                                    read_u64_be(data->data_val.data() + roff + 32);
                                part.compressed_bytes += clen;
                                if (rtype == 0x80000005u) has_zlib = true;
                                else if (rtype == 0x80000006u) has_bz2 = true;
                                else if (rtype == 0x80000007u) has_lzfse = true;
                                else if (rtype == 0x00000001u) has_raw = true;
                            }
                            if (has_lzfse) part.compression = "LZFSE";
                            else if (has_bz2) part.compression = "UDBZ (bzip2)";
                            else if (has_zlib) part.compression = "UDZO (zlib)";
                            else if (has_raw) part.compression = "RAW";
                            else part.compression = "ZERO/EMPTY";
                        }
                        res.partitions.push_back(std::move(part));
                    }
                }
            }
        }
    }
    return res;
}

PkgInspection inspect_pkg_native(const std::string& pkg_path) {
    PkgInspection res;
    std::ifstream in(pkg_path, std::ios::binary);
    if (!in) {
        res.error = "unable to open .pkg file: " + pkg_path;
        return res;
    }

    std::uint8_t hdr[28] = {};
    in.read(reinterpret_cast<char*>(hdr), 28);
    if (in.gcount() != 28 || std::memcmp(hdr, "xar!", 4) != 0) {
        res.error = "missing XAR 'xar!' header";
        return res;
    }

    const std::uint16_t hdr_size = read_u16_be(hdr + 4);
    const std::uint64_t toc_comp = read_u64_be(hdr + 8);
    const std::uint64_t toc_uncomp = read_u64_be(hdr + 16);
    if (hdr_size < 28 || toc_comp == 0 || toc_comp > 32u * 1024u * 1024u) {
        res.error = "invalid XAR header dimensions";
        return res;
    }

    res.toc_length_compressed = static_cast<std::uint32_t>(toc_comp);
    res.toc_length_uncompressed = static_cast<std::uint32_t>(toc_uncomp);
    res.heap_offset = static_cast<std::uint64_t>(hdr_size) + toc_comp;

    std::vector<std::uint8_t> comp_buf(static_cast<std::size_t>(toc_comp));
    in.seekg(hdr_size, std::ios::beg);
    in.read(reinterpret_cast<char*>(comp_buf.data()), static_cast<std::streamsize>(toc_comp));
    if (static_cast<std::size_t>(in.gcount()) != comp_buf.size()) {
        res.error = "truncated XAR Table of Contents";
        return res;
    }

    std::vector<std::uint8_t> toc_bytes;
    if (!zlib_decompress(comp_buf.data(), comp_buf.size(), MAX_WBITS, &toc_bytes)) {
        res.error = "failed to zlib-decompress XAR TOC";
        return res;
    }

    const std::string_view xml(
        reinterpret_cast<const char*>(toc_bytes.data()), toc_bytes.size());
    std::size_t pos = 0;
    while (pos < xml.size()) {
        const std::size_t fstart = xml.find("<file ", pos);
        if (fstart == std::string_view::npos) break;
        const std::size_t fend = xml.find("</file>", fstart);
        if (fend == std::string_view::npos) break;

        // Only inspect leaf files that contain a <data> block
        const std::size_t dstart = xml.find("<data>", fstart);
        const std::size_t dend = xml.find("</data>", fstart);
        if (dstart != std::string_view::npos && dend != std::string_view::npos &&
            dend < fend) {
            PkgFileEntry entry;
            entry.name = extract_xml_tag_text(xml, "name", fstart, dstart);
            const std::string off_s = extract_xml_tag_text(xml, "offset", dstart, dend);
            const std::string len_s = extract_xml_tag_text(xml, "length", dstart, dend);
            const std::string sz_s = extract_xml_tag_text(xml, "size", dstart, dend);
            entry.encoding = extract_encoding_style(xml, dstart, dend);
            try {
                if (!off_s.empty()) entry.offset = std::stoull(off_s);
                if (!len_s.empty()) entry.length = std::stoull(len_s);
                if (!sz_s.empty()) entry.extracted_size = std::stoull(sz_s);
            } catch (...) {
            }
            if (!entry.name.empty() && entry.length > 0) {
                res.entries.push_back(std::move(entry));
            }
        }
        pos = fstart + 6;
    }

    res.valid = true;
    return res;
}

InstallResult install_app_bundle(const std::string& app_bundle_path,
                                 const std::string& apps_dir,
                                 const std::string& icons_dir) {
    InstallResult result;
    std::error_code ec;
    const fs::path src(app_bundle_path);
    if (!fs::is_directory(src, ec) ||
        !fs::is_regular_file(src / "Contents" / "Info.plist", ec)) {
        result.error = "not a valid macOS .app bundle: " + app_bundle_path;
        return result;
    }

    const fs::path dst_root(apps_dir.empty() ? default_macos_apps_dir() : apps_dir);
    const fs::path ico_root(icons_dir.empty() ? default_macos_icons_dir() : icons_dir);
    fs::create_directories(dst_root, ec);
    fs::create_directories(ico_root, ec);

    const fs::path dst = dst_root / src.filename();
    if (fs::weakly_canonical(src, ec) != fs::weakly_canonical(dst, ec)) {
        fs::remove_all(dst, ec);
        fs::copy(src, dst,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks |
                     fs::copy_options::overwrite_existing,
                 ec);
        if (ec) {
            result.error = "failed to copy .app bundle: " + ec.message();
            return result;
        }
    }

    InstalledMacosApp app = read_installed_app_meta(dst, ico_root);
    if (app.name.empty()) {
        result.error = "failed to parse Info.plist inside " + dst.string();
        return result;
    }
    result.log.push_back("Installed " + app.name + " -> " + dst.string());
    if (!app.icon_path.empty()) {
        result.log.push_back("Extracted .icns icon -> " + app.icon_path);
    }
    result.installed_apps.push_back(std::move(app));
    result.ok = true;
    return result;
}

InstallResult install_pkg_native(const std::string& pkg_path,
                                 const std::string& apps_dir,
                                 const std::string& icons_dir) {
    InstallResult result;
    const PkgInspection pkg = inspect_pkg_native(pkg_path);
    if (!pkg.valid) {
        result.error = pkg.error;
        return result;
    }

    const fs::path stage = fs::temp_directory_path() /
                           ("finn-pkg-stage-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);

    std::ifstream in(pkg_path, std::ios::binary);
    bool extracted_any_payload = false;

    for (const auto& e : pkg.entries) {
        if (e.name != "Payload" && e.name.find("Payload") == std::string::npos) continue;
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(e.length));
        in.seekg(static_cast<std::streamoff>(pkg.heap_offset + e.offset), std::ios::beg);
        in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (static_cast<std::size_t>(in.gcount()) != raw.size()) continue;

        std::vector<std::uint8_t> cpio_bytes;
        if (raw.size() >= 2 && raw[0] == 0x1fu && raw[1] == 0x8bu) {
            // gzip-compressed cpio payload (standard macOS component .pkg)
            if (zlib_decompress(raw.data(), raw.size(), 16 + MAX_WBITS, &cpio_bytes)) {
                std::string cpio_err;
                if (extract_cpio_bytes(cpio_bytes, stage, &cpio_err)) {
                    extracted_any_payload = true;
                    result.log.push_back("Extracted gzip+cpio Payload (" +
                                         std::to_string(cpio_bytes.size()) + " bytes)");
                }
            }
        } else if (raw.size() >= 6 &&
                   (std::memcmp(raw.data(), "070707", 6) == 0 ||
                    std::memcmp(raw.data(), "070701", 6) == 0)) {
            std::string cpio_err;
            if (extract_cpio_bytes(raw, stage, &cpio_err)) {
                extracted_any_payload = true;
                result.log.push_back("Extracted raw cpio Payload");
            }
        } else if (raw.size() >= 4 && std::memcmp(raw.data(), "pbzx", 4) == 0) {
            result.log.push_back("Payload uses pbzx/lzma; checking host 7z fallback");
            if (finn::compat::executable_exists("7z") ||
                finn::compat::executable_exists("7zz")) {
                const std::string bin =
                    finn::compat::executable_exists("7z") ? "7z" : "7zz";
                const fs::path payload_file = stage / "Payload.pbzx";
                std::ofstream pf(payload_file, std::ios::binary);
                pf.write(reinterpret_cast<const char*>(raw.data()),
                         static_cast<std::streamsize>(raw.size()));
                pf.close();
                if (run_tool({bin, "x", "-y", "-o" + stage.string(),
                              payload_file.string()}) == 0) {
                    extracted_any_payload = true;
                }
            }
        }
    }

    std::vector<fs::path> found_apps;
    find_app_bundles_recursive(stage, &found_apps);
    for (const auto& bundle : found_apps) {
        auto sub = install_app_bundle(bundle.string(), apps_dir, icons_dir);
        for (auto& line : sub.log) result.log.push_back(std::move(line));
        for (auto& a : sub.installed_apps) result.installed_apps.push_back(std::move(a));
    }

    fs::remove_all(stage, ec);

    if (!result.installed_apps.empty()) {
        result.ok = true;
        return result;
    }
    if (!extracted_any_payload) {
        result.error =
            "XAR .pkg parsed (" + std::to_string(pkg.entries.size()) +
            " entries), but payload uses pbzx/xz compression without 7z installed.";
    } else {
        result.error = "extracted .pkg payload contained no macOS .app bundle";
    }
    return result;
}

InstallResult install_dmg_package(const std::string& dmg_path,
                                  const std::string& apps_dir,
                                  const std::string& icons_dir) {
    InstallResult result;
    const DmgInspection dmg = inspect_dmg_native(dmg_path);
    if (!dmg.valid) {
        result.error = dmg.error;
        return result;
    }

    result.log.push_back("Verified UDIF .dmg (" + std::to_string(dmg.sector_count) +
                         " sectors, " + std::to_string(dmg.partitions.size()) +
                         " partition(s))");
    for (const auto& p : dmg.partitions) {
        result.log.push_back("  partition: " + p.name + " [" + p.compression + "]");
    }

    const fs::path stage = fs::temp_directory_path() /
                           ("finn-dmg-stage-" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);

    bool extracted = false;
    if (finn::compat::executable_exists("7z") || finn::compat::executable_exists("7zz")) {
        const std::string bin = finn::compat::executable_exists("7z") ? "7z" : "7zz";
        result.log.push_back("Extracting .dmg filesystem via " + bin + "...");
        if (run_tool({bin, "x", "-y", "-o" + stage.string(), dmg_path}) == 0) {
            extracted = true;
        }
    }

    if (!extracted) {
        fs::remove_all(stage, ec);
        result.error =
            "UDIF .dmg verified, but extracting HFS+/APFS contents requires 7z (p7zip/7zip) installed.";
        return result;
    }

    std::vector<fs::path> found_apps;
    find_app_bundles_recursive(stage, &found_apps);
    for (const auto& bundle : found_apps) {
        auto sub = install_app_bundle(bundle.string(), apps_dir, icons_dir);
        for (auto& line : sub.log) result.log.push_back(std::move(line));
        for (auto& a : sub.installed_apps) result.installed_apps.push_back(std::move(a));
    }

    fs::remove_all(stage, ec);

    if (result.installed_apps.empty()) {
        result.error = "no .app bundle found inside " + dmg_path;
        return result;
    }
    result.ok = true;
    return result;
}

std::vector<InstalledMacosApp> discover_installed_macos_apps(const std::string& apps_dir,
                                                             const std::string& icons_dir) {
    std::vector<InstalledMacosApp> out;
    std::vector<fs::path> search_dirs;
    const fs::path ico_dir(icons_dir.empty() ? default_macos_icons_dir() : icons_dir);

    if (!apps_dir.empty()) {
        search_dirs.emplace_back(apps_dir);
    } else {
        search_dirs.emplace_back(default_macos_apps_dir());
        if (const char* home = std::getenv("HOME"); home && *home) {
            search_dirs.emplace_back(fs::path(home) / "Applications");
        }
    }

    for (const auto& dir : search_dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            const fs::path p = entry.path();
            if (p.extension() != ".app") continue;
            if (!fs::is_regular_file(p / "Contents" / "Info.plist", ec)) continue;
            InstalledMacosApp app = read_installed_app_meta(p, ico_dir);
            if (!app.name.empty()) out.push_back(std::move(app));
        }
    }

    std::sort(out.begin(), out.end(),
              [](const InstalledMacosApp& a, const InstalledMacosApp& b) {
                  return a.name < b.name;
              });
    return out;
}

bool remove_installed_macos_app(const std::string& bundle_path, std::string* error) {
    std::error_code ec;
    const fs::path p(bundle_path);
    if (!fs::is_directory(p, ec) || p.extension() != ".app") {
        if (error) *error = "not a .app bundle directory: " + bundle_path;
        return false;
    }
    fs::remove_all(p, ec);
    if (ec) {
        if (error) *error = "failed to remove .app bundle: " + ec.message();
        return false;
    }
    return true;
}

}  // namespace finn::darwin
