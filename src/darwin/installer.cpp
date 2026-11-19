#include "darwin/installer.hpp"

#include "compat/desktop_entry.hpp"
#include "darwin/icns.hpp"
#include "darwin/macho_loader.hpp"
#include "darwin/plist.hpp"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace finn::darwin {
namespace {

std::atomic<std::uint64_t> g_stage_seq{0};

fs::path make_unique_stage_dir(const std::string& prefix) {
    const std::uint64_t seq = g_stage_seq.fetch_add(1, std::memory_order_relaxed);
    return fs::temp_directory_path() /
           (prefix + "-" + std::to_string(::getpid()) + "-" + std::to_string(seq));
}

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

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

bool equals_ci(std::string_view a, std::string_view b) {
    return a.size() == b.size() && starts_with_ci(a, b);
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

fs::path safe_extract_path(const fs::path& dest_root, std::string_view raw) {
    fs::path rel;
    for (const auto& part : fs::path(raw)) {
        const std::string s = part.string();
        if (s.empty() || s == "." || s == ".." || s == "/") continue;
        rel /= part;
    }
    return dest_root / rel;
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

    while (pos + 6 <= cpio.size()) {
        const std::string_view magic(
            reinterpret_cast<const char*>(cpio.data() + pos), 6);
        std::uint64_t mode = 0;
        std::uint64_t namesize = 0;
        std::uint64_t filesize = 0;
        std::size_t header_len = 0;
        std::size_t align = 1;

        if (magic == "070707") {
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

        const fs::path target = safe_extract_path(dest_root, name);
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

// Clean-room ZIP archive extractor (Stored + Deflate via zlib) preserving Unix modes & symlinks.
bool extract_zip_native(const std::string& zip_path, const fs::path& dest_root,
                        std::string* error) {
    std::ifstream in(zip_path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (error) *error = "unable to open .zip archive: " + zip_path;
        return false;
    }
    const std::streamoff fsize = in.tellg();
    if (fsize < 22) {
        if (error) *error = "file too small to be a valid ZIP archive";
        return false;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(fsize));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(buf.data()), fsize);
    if (in.gcount() != fsize) {
        if (error) *error = "truncated read of .zip archive";
        return false;
    }

    // Locate End of Central Directory (EOCD) record (signature 0x06054b50)
    const std::size_t search_min = (buf.size() > 65557) ? (buf.size() - 65557) : 0;
    std::size_t eocd = std::string::npos;
    for (std::size_t i = buf.size() - 22; ; --i) {
        if (read_u32_le(buf.data() + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
        if (i == search_min) break;
    }
    if (eocd == std::string::npos) {
        if (error) *error = "ZIP End of Central Directory record not found";
        return false;
    }

    const std::uint16_t entry_count = read_u16_le(buf.data() + eocd + 10);
    const std::uint32_t cd_size = read_u32_le(buf.data() + eocd + 12);
    const std::uint32_t cd_off = read_u32_le(buf.data() + eocd + 16);
    if (static_cast<std::uint64_t>(cd_off) + cd_size > buf.size()) {
        if (error) *error = "ZIP Central Directory exceeds file bounds";
        return false;
    }

    std::size_t pos = cd_off;
    for (std::uint16_t i = 0; i < entry_count; ++i) {
        if (pos + 46 > buf.size() || read_u32_le(buf.data() + pos) != 0x02014b50u) {
            if (error) *error = "corrupted ZIP Central Directory header";
            return false;
        }
        const std::uint16_t ver_made = read_u16_le(buf.data() + pos + 4);
        const std::uint16_t method = read_u16_le(buf.data() + pos + 10);
        const std::uint32_t comp_sz = read_u32_le(buf.data() + pos + 20);
        const std::uint16_t name_len = read_u16_le(buf.data() + pos + 28);
        const std::uint16_t extra_len = read_u16_le(buf.data() + pos + 30);
        const std::uint16_t comment_len = read_u16_le(buf.data() + pos + 32);
        const std::uint32_t ext_attr = read_u32_le(buf.data() + pos + 38);
        const std::uint32_t local_off = read_u32_le(buf.data() + pos + 42);

        if (pos + 46 + name_len + extra_len + comment_len > buf.size()) {
            if (error) *error = "truncated ZIP Central Directory entry";
            return false;
        }
        const std::string name(reinterpret_cast<const char*>(buf.data() + pos + 46), name_len);
        pos += 46 + name_len + extra_len + comment_len;

        // Skip macOS Finder AppleDouble archive metadata
        if (name.rfind("__MACOSX/", 0) == 0 || name == "__MACOSX") continue;

        if (local_off + 30 > buf.size() || read_u32_le(buf.data() + local_off) != 0x04034b50u) {
            if (error) *error = "invalid ZIP Local File Header";
            return false;
        }
        const std::uint16_t l_name_len = read_u16_le(buf.data() + local_off + 26);
        const std::uint16_t l_extra_len = read_u16_le(buf.data() + local_off + 28);
        const std::size_t data_off =
            static_cast<std::size_t>(local_off) + 30 + l_name_len + l_extra_len;
        if (data_off + comp_sz > buf.size()) {
            if (error) *error = "truncated ZIP entry data";
            return false;
        }

        const fs::path target = safe_extract_path(dest_root, name);
        const bool is_unix = ((ver_made >> 8) == 3u);
        const std::uint32_t mode = is_unix ? (ext_attr >> 16) : 0u;
        const bool is_dir = (!name.empty() && name.back() == '/') ||
                            (is_unix && (mode & 0170000u) == 0040000u);
        const bool is_symlink = is_unix && (mode & 0170000u) == 0120000u;

        std::error_code ec;
        if (is_dir) {
            fs::create_directories(target, ec);
            continue;
        }

        std::vector<std::uint8_t> payload;
        if (method == 0) {
            payload.assign(buf.data() + data_off, buf.data() + data_off + comp_sz);
        } else if (method == 8) {
            if (!zlib_decompress(buf.data() + data_off, comp_sz, -MAX_WBITS, &payload)) {
                if (error) *error = "zlib inflate failed on ZIP entry: " + name;
                return false;
            }
        } else {
            if (error) *error = "unsupported ZIP compression method " + std::to_string(method);
            return false;
        }

        fs::create_directories(target.parent_path(), ec);
        if (is_symlink && !payload.empty()) {
            const std::string link_target(reinterpret_cast<const char*>(payload.data()),
                                          payload.size());
            fs::remove(target, ec);
            fs::create_symlink(link_target, target, ec);
        } else {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) {
                if (error) *error = "failed to create extracted file: " + target.string();
                return false;
            }
            if (!payload.empty()) {
                out.write(reinterpret_cast<const char*>(payload.data()),
                          static_cast<std::streamsize>(payload.size()));
            }
            const bool exec_bit = (is_unix && (mode & 0111u) != 0) ||
                                  (name.find("/Contents/MacOS/") != std::string::npos);
            if (exec_bit) {
                fs::permissions(target,
                                fs::perms::owner_exec | fs::perms::group_exec |
                                    fs::perms::others_exec,
                                fs::perm_options::add, ec);
            }
        }
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

// Clean-room POSIX HTTP/1.1 GET fallback (for http:// when curl is absent).
bool download_http_posix(const std::string& url, const fs::path& dst, std::string* error,
                         int redirect_depth = 0) {
    if (redirect_depth > 5) {
        if (error) *error = "too many HTTP redirects";
        return false;
    }
    if (!starts_with_ci(url, "http://")) {
        if (error) *error = "HTTPS download requires curl installed";
        return false;
    }
    const std::string rest = url.substr(7);
    const std::size_t slash = rest.find('/');
    const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    const std::string req_path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    std::string host = authority;
    std::string port = "80";
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t rb = authority.find(']');
        if (rb != std::string::npos) {
            host = authority.substr(1, rb - 1);
            if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
                port = authority.substr(rb + 2);
            }
        }
    } else if (const std::size_t col = authority.rfind(':'); col != std::string::npos) {
        host = authority.substr(0, col);
        port = authority.substr(col + 1);
    }

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "DNS resolution failed for host: " + host;
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
        if (error) *error = "failed to connect to " + authority;
        return false;
    }

    const std::string req =
        "GET " + req_path + " HTTP/1.1\r\n"
        "Host: " + authority + "\r\n"
        "User-Agent: finn-darwin/0.1\r\n"
        "Connection: close\r\n\r\n";
    std::size_t sent = 0;
    while (sent < req.size()) {
        const ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, 0);
        if (n <= 0) {
            ::close(fd);
            if (error) *error = "failed sending HTTP request";
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }

    std::vector<std::uint8_t> raw;
    std::uint8_t chunk[16384];
    for (;;) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        raw.insert(raw.end(), chunk, chunk + n);
    }
    ::close(fd);

    const std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    const std::size_t hdr_end = sv.find("\r\n\r\n");
    if (hdr_end == std::string_view::npos) {
        if (error) *error = "malformed HTTP response";
        return false;
    }
    const std::string_view headers = sv.substr(0, hdr_end);
    const std::size_t line_end = headers.find("\r\n");
    const std::string_view status_line =
        (line_end == std::string_view::npos) ? headers : headers.substr(0, line_end);
    const std::size_t sp = status_line.find(' ');
    const int status = (sp != std::string_view::npos)
                           ? std::atoi(std::string(status_line.substr(sp + 1)).c_str())
                           : 0;

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        std::istringstream hs{std::string(headers)};
        std::string line;
        while (std::getline(hs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (starts_with_ci(line, "location:")) {
                std::string loc = line.substr(9);
                while (!loc.empty() && loc.front() == ' ') loc.erase(loc.begin());
                return download_http_posix(loc, dst, error, redirect_depth + 1);
            }
        }
    }

    if (status < 200 || status >= 300) {
        if (error) *error = "HTTP server returned status " + std::to_string(status);
        return false;
    }

    bool chunked = false;
    {
        std::istringstream hs{std::string(headers)};
        std::string line;
        while (std::getline(hs, line)) {
            if (starts_with_ci(line, "transfer-encoding:") &&
                line.find("chunked") != std::string::npos) {
                chunked = true;
            }
        }
    }

    const std::size_t body_off = hdr_end + 4;
    std::vector<std::uint8_t> body;
    if (chunked) {
        std::size_t p = body_off;
        while (p < raw.size()) {
            const std::size_t crlf = sv.find("\r\n", p);
            if (crlf == std::string_view::npos) break;
            const std::string hex_len(sv.substr(p, crlf - p));
            const std::size_t clen = std::strtoull(hex_len.c_str(), nullptr, 16);
            p = crlf + 2;
            if (clen == 0 || p + clen > raw.size()) break;
            body.insert(body.end(), raw.data() + p, raw.data() + p + clen);
            p += clen + 2;
        }
    } else {
        body.assign(raw.data() + body_off, raw.data() + raw.size());
    }

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "failed to write downloaded file: " + dst.string();
        return false;
    }
    if (!body.empty()) {
        out.write(reinterpret_cast<const char*>(body.data()),
                  static_cast<std::streamsize>(body.size()));
    }
    return static_cast<bool>(out);
}

std::string filename_from_url(std::string_view url) {
    const std::size_t q = url.find_first_of("?#");
    const std::string_view clean = (q == std::string_view::npos) ? url : url.substr(0, q);
    const std::size_t slash = clean.rfind('/');
    std::string name =
        (slash == std::string_view::npos || slash + 1 >= clean.size())
            ? "package.bin"
            : std::string(clean.substr(slash + 1));
    std::string safe;
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '-' || c == '_') safe.push_back(c);
    }
    if (safe.empty() || safe == "." || safe == "..") safe = "package.bin";
    return safe;
}

enum class PackageMagic { Unknown, Pkg, Dmg, Zip };

PackageMagic detect_package_magic(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return PackageMagic::Unknown;
    const std::streamoff sz = in.tellg();
    if (sz < 4) return PackageMagic::Unknown;

    std::uint8_t head[4] = {};
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(head), 4);
    if (in.gcount() == 4) {
        if (std::memcmp(head, "xar!", 4) == 0) return PackageMagic::Pkg;
        if (head[0] == 'P' && head[1] == 'K' &&
            ((head[2] == 0x03u && head[3] == 0x04u) ||
             (head[2] == 0x05u && head[3] == 0x06u))) {
            return PackageMagic::Zip;
        }
    }
    if (sz >= 512) {
        std::uint8_t tail[4] = {};
        in.seekg(sz - 512, std::ios::beg);
        in.read(reinterpret_cast<char*>(tail), 4);
        if (in.gcount() == 4 && std::memcmp(tail, "koly", 4) == 0) {
            return PackageMagic::Dmg;
        }
    }
    return PackageMagic::Unknown;
}

void attach_macho_diagnoses(InstallResult* result) {
    if (!result || !result->ok) return;
    MachoLoader loader;
    for (auto& app : result->installed_apps) {
        const auto d = loader.diagnose(app.bundle_path);
        if (d.valid) {
            app.runnable = d.runnable;
            app.diagnosis_summary = d.summary;
            result->log.push_back("Compatibility (" + app.name + "): " + d.summary);
            if (!d.missing_frameworks.empty()) {
                std::string line = "  unresolved frameworks:";
                for (const auto& name : d.missing_frameworks) line += " " + name;
                result->log.push_back(std::move(line));
            }
            if (!d.framework_gaps.empty()) {
                std::string line = "  unimplemented frameworks:";
                for (const auto& g : d.framework_gaps) {
                    line += " " + g.name + " (" + g.reason + ")";
                }
                result->log.push_back(std::move(line));
            }
            if (!d.missing_symbols.empty()) {
                const std::size_t show = std::min<std::size_t>(d.missing_symbols.size(), 8);
                std::string line = "  missing symbols: " +
                                   std::to_string(d.missing_symbols.size()) + " [";
                for (std::size_t i = 0; i < show; ++i) {
                    if (i) line += ", ";
                    line += d.missing_symbols[i];
                }
                if (d.missing_symbols.size() > show) {
                    line += ", ... +" +
                            std::to_string(d.missing_symbols.size() - show) + " more";
                }
                line += "]";
                result->log.push_back(std::move(line));
            }
        } else {
            app.runnable = false;
            app.diagnosis_summary = d.error;
            result->log.push_back("Compatibility (" + app.name + "): " + d.error);
        }
    }
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
    std::error_code iter_ec;
    if (!fs::is_directory(dir, iter_ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, iter_ec)) {
        // Each query needs its own error_code. Reusing one across iterations
        // makes a self-referential symlink (ELOOP) abort the whole scan before
        // a sibling .app bundle is ever visited.
        std::error_code entry_ec;
        // Never follow symlinks: a link such as Applications -> . would loop
        // forever. Real .app bundles are directories, not links.
        if (entry.is_symlink(entry_ec)) continue;
        if (!entry.is_directory(entry_ec)) continue;
        const fs::path p = entry.path();
        if (p.extension() == ".app") {
            std::error_code plist_ec;
            if (fs::is_regular_file(p / "Contents" / "Info.plist", plist_ec)) {
                out->push_back(p);
                continue;
            }
        }
        find_app_bundles_recursive(p, out);
    }
}

}  // namespace

std::vector<std::string> discover_app_bundles_in_tree(const std::string& root) {
    std::vector<fs::path> found;
    find_app_bundles_recursive(fs::path(root), &found);
    std::vector<std::string> out;
    out.reserve(found.size());
    for (const auto& p : found) out.push_back(p.string());
    return out;
}

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

        const std::size_t dstart = xml.find("<data>", fstart);
        const std::size_t dend = xml.find("</data>", fstart);
        if (dstart != std::string_view::npos && dend != std::string_view::npos &&
            dend < fend) {
            PkgFileEntry entry;
            // XAR producers disagree on <name> placement: some write it before
            // <data>, Apple's installer TOC writes it after </data>. Accept
            // both; without the second lookup the real Payload is dropped.
            entry.name = extract_xml_tag_text(xml, "name", fstart, dstart);
            if (entry.name.empty()) {
                entry.name = extract_xml_tag_text(xml, "name", dend, fend);
            }
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

    const fs::path stage = make_unique_stage_dir("finn-pkg-stage");
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
        // Report what actually happened. Claiming "7z not installed" here was
        // wrong whenever 7z existed but the TOC parser had dropped Payload.
        const bool has_7z = finn::compat::executable_exists("7z") ||
                            finn::compat::executable_exists("7zz");
        result.error = "XAR .pkg parsed (" + std::to_string(pkg.entries.size()) +
                       " entries), but no payload was extracted (supported: gzip+cpio, "
                       "raw cpio" +
                       (has_7z ? ", pbzx via 7z" : "; pbzx needs 7z/p7zip installed") + ").";
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

    const fs::path stage = make_unique_stage_dir("finn-dmg-stage");
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

InstallResult install_zip_package(const std::string& zip_path,
                                  const std::string& apps_dir,
                                  const std::string& icons_dir) {
    InstallResult result;
    if (detect_package_magic(fs::path(zip_path)) != PackageMagic::Zip) {
        result.error = "file is not a valid ZIP archive (missing PK magic): " + zip_path;
        return result;
    }

    const fs::path stage = make_unique_stage_dir("finn-zip-stage");
    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);

    std::string zip_err;
    bool extracted = extract_zip_native(zip_path, stage, &zip_err);
    if (extracted) {
        result.log.push_back("Extracted .zip archive natively via zlib");
    } else {
        fs::remove_all(stage, ec);
        fs::create_directories(stage, ec);
        if (finn::compat::executable_exists("ditto")) {
            if (run_tool({"ditto", "-x", "-k", zip_path, stage.string()}) == 0) {
                extracted = true;
                result.log.push_back("Extracted .zip archive via ditto");
            }
        } else if (finn::compat::executable_exists("unzip")) {
            if (run_tool({"unzip", "-q", "-o", zip_path, "-d", stage.string()}) == 0) {
                extracted = true;
                result.log.push_back("Extracted .zip archive via unzip");
            }
        } else if (finn::compat::executable_exists("7z") ||
                   finn::compat::executable_exists("7zz")) {
            const std::string bin = finn::compat::executable_exists("7z") ? "7z" : "7zz";
            if (run_tool({bin, "x", "-y", "-o" + stage.string(), zip_path}) == 0) {
                extracted = true;
                result.log.push_back("Extracted .zip archive via " + bin);
            }
        }
    }

    if (!extracted) {
        fs::remove_all(stage, ec);
        result.error = "failed to extract .zip archive: " + zip_err;
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
        result.error = "no macOS .app bundle found inside " + zip_path;
        return result;
    }
    result.ok = true;
    return result;
}

InstallResult install_from_url_or_path(const std::string& url_or_path,
                                       const std::string& apps_dir,
                                       const std::string& icons_dir) {
    InstallResult result;
    std::error_code ec;

    if (starts_with_ci(url_or_path, "http://") || starts_with_ci(url_or_path, "https://")) {
        const fs::path dl_dir = make_unique_stage_dir("finn-dl");
        fs::create_directories(dl_dir, ec);
        const fs::path local_file = dl_dir / filename_from_url(url_or_path);

        result.log.push_back("Downloading " + url_or_path + "...");
        bool ok_dl = false;
        std::string dl_err;
        if (finn::compat::executable_exists("curl")) {
            const int rc = run_tool({"curl", "-fsSL", "--max-redirs", "10",
                                     "-o", local_file.string(), url_or_path});
            ok_dl = (rc == 0 && fs::is_regular_file(local_file, ec));
            if (!ok_dl) dl_err = "curl failed with exit code " + std::to_string(rc);
        } else {
            ok_dl = download_http_posix(url_or_path, local_file, &dl_err);
        }

        if (!ok_dl) {
            fs::remove_all(dl_dir, ec);
            result.error = "download failed: " + dl_err;
            return result;
        }

        const PackageMagic magic = detect_package_magic(local_file);
        InstallResult sub;
        if (magic == PackageMagic::Pkg) {
            result.log.push_back("Verified downloaded XAR .pkg magic ('xar!')");
            sub = install_pkg_native(local_file.string(), apps_dir, icons_dir);
        } else if (magic == PackageMagic::Zip) {
            result.log.push_back("Verified downloaded ZIP magic ('PK\\x03\\x04')");
            sub = install_zip_package(local_file.string(), apps_dir, icons_dir);
        } else if (magic == PackageMagic::Dmg) {
            result.log.push_back("Verified downloaded UDIF .dmg magic ('koly')");
            sub = install_dmg_package(local_file.string(), apps_dir, icons_dir);
        } else {
            fs::remove_all(dl_dir, ec);
            result.error =
                "downloaded file is not a recognized macOS package (.dmg koly, .pkg xar!, or .zip PK)";
            return result;
        }

        fs::remove_all(dl_dir, ec);
        result.ok = sub.ok;
        result.error = std::move(sub.error);
        for (auto& line : sub.log) result.log.push_back(std::move(line));
        for (auto& app : sub.installed_apps) result.installed_apps.push_back(std::move(app));
        attach_macho_diagnoses(&result);
        return result;
    }

    // Local filesystem target
    if (fs::is_directory(url_or_path, ec) &&
        (ends_with_ci(url_or_path, ".app") ||
         fs::is_regular_file(fs::path(url_or_path) / "Contents" / "Info.plist", ec))) {
        result = install_app_bundle(url_or_path, apps_dir, icons_dir);
        attach_macho_diagnoses(&result);
        return result;
    }

    if (!fs::is_regular_file(url_or_path, ec)) {
        result.error = "package or .app target not found: " + url_or_path;
        return result;
    }

    const PackageMagic magic = detect_package_magic(fs::path(url_or_path));
    if (magic == PackageMagic::Pkg || ends_with_ci(url_or_path, ".pkg") ||
        ends_with_ci(url_or_path, ".mpkg")) {
        result = install_pkg_native(url_or_path, apps_dir, icons_dir);
    } else if (magic == PackageMagic::Zip || ends_with_ci(url_or_path, ".zip")) {
        result = install_zip_package(url_or_path, apps_dir, icons_dir);
    } else if (magic == PackageMagic::Dmg || ends_with_ci(url_or_path, ".dmg")) {
        result = install_dmg_package(url_or_path, apps_dir, icons_dir);
    } else {
        result.error = "unsupported macOS package format: " + url_or_path;
        return result;
    }

    attach_macho_diagnoses(&result);
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

bool uninstall_macos_app(const std::string& query,
                         std::string* removed_desc_out,
                         std::string* error,
                         const std::string& apps_dir,
                         const std::string& icons_dir) {
    std::error_code ec;
    const fs::path direct(query);
    if (fs::is_directory(direct, ec) && direct.extension() == ".app") {
        if (!remove_installed_macos_app(direct.string(), error)) return false;
        if (removed_desc_out) *removed_desc_out = direct.filename().string() + " (" + direct.string() + ")";
        return true;
    }

    const auto apps = discover_installed_macos_apps(apps_dir, icons_dir);
    for (const auto& app : apps) {
        const std::string bundle_base = fs::path(app.bundle_path).filename().string();
        if (equals_ci(app.id, query) || equals_ci(app.bundle_id, query) ||
            equals_ci(app.name, query) || equals_ci(bundle_base, query)) {
            if (!remove_installed_macos_app(app.bundle_path, error)) return false;
            if (!app.icon_path.empty()) fs::remove(app.icon_path, ec);
            if (removed_desc_out) {
                *removed_desc_out = app.name + " (" + app.id + ")";
            }
            return true;
        }
    }

    if (error) *error = "no installed macOS app matched '" + query + "'";
    return false;
}

}  // namespace finn::darwin
