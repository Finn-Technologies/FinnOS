#include "darwin/libsystem.hpp"

#include "darwin/objc_runtime.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <thread>

namespace finn::darwin {
namespace {

template <typename Fn>
void* fn_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

int g_argc = 0;
char** g_argv = nullptr;
std::string g_exec_path;

// Darwin standard streams & stack protector globals
FILE* g_darwin_stdin = nullptr;
FILE* g_darwin_stdout = nullptr;
FILE* g_darwin_stderr = nullptr;
std::uintptr_t g_stack_chk_guard = 0x595a66696e6e4f53ull;
thread_local int g_darwin_errno = 0;

// Maps host Linux/POSIX errno to Darwin ABI errno numbers.
int to_darwin_errno(int e) {
#if defined(__APPLE__)
    return e;
#else
    if (e == EAGAIN) return 35;        // Darwin EAGAIN/EWOULDBLOCK = 35
    if (e == EINPROGRESS) return 36;   // Darwin EINPROGRESS = 36
    if (e == EALREADY) return 37;      // Darwin EALREADY = 37
    if (e == ENOTSOCK) return 38;      // Darwin ENOTSOCK = 38
    if (e == EMSGSIZE) return 40;      // Darwin EMSGSIZE = 40
    if (e == EPROTONOSUPPORT) return 43;
    if (e == EOPNOTSUPP) return 102;   // Darwin EOPNOTSUPP = 102
    if (e == EADDRINUSE) return 48;    // Darwin EADDRINUSE = 48
    if (e == EADDRNOTAVAIL) return 49; // Darwin EADDRNOTAVAIL = 49
    if (e == ECONNRESET) return 54;    // Darwin ECONNRESET = 54
    if (e == ENOTCONN) return 57;      // Darwin ENOTCONN = 57
    if (e == ETIMEDOUT) return 60;     // Darwin ETIMEDOUT = 60
    if (e == ECONNREFUSED) return 61;  // Darwin ECONNREFUSED = 61
    if (e == ELOOP) return 62;         // Darwin ELOOP = 62
    if (e == ENAMETOOLONG) return 63;  // Darwin ENAMETOOLONG = 63
    if (e == ENOTEMPTY) return 66;     // Darwin ENOTEMPTY = 66
    return e;
#endif
}

// Translates Darwin open(2)/fcntl(2) status flags onto host OS flags.
int translate_darwin_open_flags(int dflags) {
#if defined(__APPLE__)
    return dflags;
#else
    int h = dflags & 0x3;  // O_RDONLY=0, O_WRONLY=1, O_RDWR=2
    if (dflags & 0x0004) h |= O_NONBLOCK;
    if (dflags & 0x0008) h |= O_APPEND;
    if (dflags & 0x0200) h |= O_CREAT;
    if (dflags & 0x0400) h |= O_TRUNC;
    if (dflags & 0x0800) h |= O_EXCL;
    if (dflags & 0x1000000) h |= O_CLOEXEC;
    return h;
#endif
}

#if !defined(__APPLE__)
int translate_host_open_flags_to_darwin(int hflags) {
    int d = hflags & O_ACCMODE;
    if (hflags & O_NONBLOCK) d |= 0x0004;
    if (hflags & O_APPEND) d |= 0x0008;
    if (hflags & O_CLOEXEC) d |= 0x1000000;
    return d;
}
#endif

// Darwin 64-bit struct stat64 (144 bytes, used by _stat$INODE64 / arm64 _stat).
struct DarwinStat64 {
    std::int32_t st_dev;
    std::uint16_t st_mode;
    std::uint16_t st_nlink;
    std::uint64_t st_ino;
    std::uint32_t st_uid;
    std::uint32_t st_gid;
    std::int32_t st_rdev;
    std::int32_t pad0;
    std::int64_t st_atime_sec;
    std::int64_t st_atime_nsec;
    std::int64_t st_mtime_sec;
    std::int64_t st_mtime_nsec;
    std::int64_t st_ctime_sec;
    std::int64_t st_ctime_nsec;
    std::int64_t st_birthtime_sec;
    std::int64_t st_birthtime_nsec;
    std::int64_t st_size;
    std::int64_t st_blocks;
    std::int32_t st_blksize;
    std::uint32_t st_flags;
    std::uint32_t st_gen;
    std::int32_t st_lspare;
    std::int64_t st_qspare[2];
};

void fill_darwin_stat(const struct stat& st, DarwinStat64* out) {
    std::memset(out, 0, sizeof(*out));
    out->st_dev = static_cast<std::int32_t>(st.st_dev);
    out->st_mode = static_cast<std::uint16_t>(st.st_mode);
    out->st_nlink = static_cast<std::uint16_t>(st.st_nlink);
    out->st_ino = static_cast<std::uint64_t>(st.st_ino);
    out->st_uid = static_cast<std::uint32_t>(st.st_uid);
    out->st_gid = static_cast<std::uint32_t>(st.st_gid);
    out->st_rdev = static_cast<std::int32_t>(st.st_rdev);
    out->st_size = static_cast<std::int64_t>(st.st_size);
    out->st_blocks = static_cast<std::int64_t>(st.st_blocks);
    out->st_blksize = static_cast<std::int32_t>(st.st_blksize);
#if defined(__APPLE__)
    out->st_atime_sec = st.st_atimespec.tv_sec;
    out->st_atime_nsec = st.st_atimespec.tv_nsec;
    out->st_mtime_sec = st.st_mtimespec.tv_sec;
    out->st_mtime_nsec = st.st_mtimespec.tv_nsec;
    out->st_ctime_sec = st.st_ctimespec.tv_sec;
    out->st_ctime_nsec = st.st_ctimespec.tv_nsec;
#else
    out->st_atime_sec = st.st_atim.tv_sec;
    out->st_atime_nsec = st.st_atim.tv_nsec;
    out->st_mtime_sec = st.st_mtim.tv_sec;
    out->st_mtime_nsec = st.st_mtim.tv_nsec;
    out->st_ctime_sec = st.st_ctim.tv_sec;
    out->st_ctime_nsec = st.st_ctim.tv_nsec;
#endif
}

// Darwin 64-bit struct dirent (1048 bytes, used by _readdir$INODE64 / arm64 _readdir).
struct DarwinDirent64 {
    std::uint64_t d_ino;
    std::uint64_t d_seekoff;
    std::uint16_t d_reclen;
    std::uint16_t d_namlen;
    std::uint8_t d_type;
    char d_name[1024];
};
#if defined(__APPLE__)
static_assert(offsetof(DarwinDirent64, d_name) == offsetof(struct dirent, d_name));
static_assert(sizeof(DarwinDirent64) == sizeof(struct dirent));
#endif

struct DarwinDirHandle {
    DIR* host_dir;
    DarwinDirent64 current;
};

void* shim_opendir(const char* name) {
    DIR* d = ::opendir(name);
    if (!d) return nullptr;
    auto* h = new DarwinDirHandle();
    h->host_dir = d;
    std::memset(&h->current, 0, sizeof(h->current));
    return h;
}

DarwinDirent64* shim_readdir(void* dirp) {
    if (!dirp) return nullptr;
    auto* h = static_cast<DarwinDirHandle*>(dirp);
    struct dirent* ent = ::readdir(h->host_dir);
    if (!ent) return nullptr;
    std::memset(&h->current, 0, sizeof(h->current));
    h->current.d_ino = static_cast<std::uint64_t>(ent->d_ino);
#if defined(__APPLE__)
    h->current.d_seekoff = static_cast<std::uint64_t>(ent->d_seekoff);
#else
    h->current.d_seekoff = static_cast<std::uint64_t>(ent->d_off);
#endif
    h->current.d_type = static_cast<std::uint8_t>(ent->d_type);
    const std::size_t nlen = std::min<std::size_t>(std::strlen(ent->d_name), sizeof(h->current.d_name) - 1);
    std::memcpy(h->current.d_name, ent->d_name, nlen);
    h->current.d_name[nlen] = '\0';
    h->current.d_namlen = static_cast<std::uint16_t>(nlen);
    h->current.d_reclen = static_cast<std::uint16_t>(offsetof(DarwinDirent64, d_name) + nlen + 1);
    return &h->current;
}

int shim_closedir(void* dirp) {
    if (!dirp) {
        errno = EBADF;
        return -1;
    }
    auto* h = static_cast<DarwinDirHandle*>(dirp);
    const int rc = ::closedir(h->host_dir);
    delete h;
    return rc;
}

int* shim_errno() {
    g_darwin_errno = to_darwin_errno(errno);
    return &g_darwin_errno;
}

void shim_stack_chk_fail() {
    std::fprintf(stderr, "finn-darwin: stack smashing detected\n");
    std::abort();
}

int shim_stat(const char* path, DarwinStat64* out) {
    struct stat st{};
    const int rc = ::stat(path, &st);
    if (rc == 0 && out) fill_darwin_stat(st, out);
    return rc;
}

int shim_lstat(const char* path, DarwinStat64* out) {
    struct stat st{};
    const int rc = ::lstat(path, &st);
    if (rc == 0 && out) fill_darwin_stat(st, out);
    return rc;
}

int shim_fstat(int fd, DarwinStat64* out) {
    struct stat st{};
    const int rc = ::fstat(fd, &st);
    if (rc == 0 && out) fill_darwin_stat(st, out);
    return rc;
}

// --- mmap / mprotect / sysconf / clock_gettime ABI translation ---
void* shim_mmap(void* addr, std::size_t len, int prot, int flags, int fd, off_t offset) {
#if defined(__APPLE__)
    return ::mmap(addr, len, prot, flags, fd, offset);
#else
    int hflags = 0;
    if (flags & 0x0001) hflags |= MAP_SHARED;
    if (flags & 0x0002) hflags |= MAP_PRIVATE;
    if (flags & 0x0010) hflags |= MAP_FIXED;
    if (flags & 0x1000) hflags |= MAP_ANONYMOUS;
    // Strip Darwin-only MAP_JIT (0x0800), MAP_NOCACHE (0x0400), etc.
    return ::mmap(addr, len, prot, hflags, fd, offset);
#endif
}

long shim_sysconf(int darwin_name) {
#if defined(__APPLE__)
    return ::sysconf(darwin_name);
#else
    switch (darwin_name) {
        case 1: return ::sysconf(_SC_ARG_MAX);
        case 2: return ::sysconf(_SC_CHILD_MAX);
        case 3: return ::sysconf(_SC_CLK_TCK);
        case 4: return ::sysconf(_SC_NGROUPS_MAX);
        case 5: return ::sysconf(_SC_OPEN_MAX);
        case 15: return ::sysconf(_SC_LINE_MAX);
        case 29: return ::sysconf(_SC_PAGESIZE);  // Darwin _SC_PAGESIZE = 29
        case 57: return ::sysconf(_SC_NPROCESSORS_CONF);
        case 58: return ::sysconf(_SC_NPROCESSORS_ONLN);
        case 70: return ::sysconf(_SC_GETPW_R_SIZE_MAX);
        case 71: return ::sysconf(_SC_GETGR_R_SIZE_MAX);
        default: return ::sysconf(darwin_name);
    }
#endif
}

int shim_clock_gettime(int darwin_clk, struct timespec* tp) {
#if defined(__APPLE__)
    return ::clock_gettime(static_cast<clockid_t>(darwin_clk), tp);
#else
    clockid_t clk = CLOCK_REALTIME;
    switch (darwin_clk) {
        case 0: clk = CLOCK_REALTIME; break;
        case 4: clk = CLOCK_MONOTONIC_RAW; break;
        case 6: clk = CLOCK_MONOTONIC; break;          // Darwin CLOCK_MONOTONIC = 6
        case 8: clk = CLOCK_MONOTONIC_RAW; break;      // Darwin CLOCK_UPTIME_RAW = 8
        case 12: clk = CLOCK_PROCESS_CPUTIME_ID; break;
        case 16: clk = CLOCK_THREAD_CPUTIME_ID; break;
        default: clk = static_cast<clockid_t>(darwin_clk); break;
    }
    return ::clock_gettime(clk, tp);
#endif
}

// --- BSD Sockets & getaddrinfo ABI translation ---
struct DarwinAddrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    std::uint32_t ai_addrlen;
    char* ai_canonname;
    void* ai_addr;
    DarwinAddrinfo* ai_next;
};
#if defined(__APPLE__)
static_assert(offsetof(DarwinAddrinfo, ai_canonname) == offsetof(struct addrinfo, ai_canonname));
static_assert(offsetof(DarwinAddrinfo, ai_addr) == offsetof(struct addrinfo, ai_addr));
static_assert(sizeof(DarwinAddrinfo) == sizeof(struct addrinfo));
#endif

#if !defined(__APPLE__)
int darwin_af_to_host(int af) {
    if (af == 30) return AF_INET6;  // Darwin AF_INET6 = 30
    if (af == 1) return AF_UNIX;
    if (af == 2) return AF_INET;
    return af;
}

int host_af_to_darwin(int af) {
    if (af == AF_INET6) return 30;
    if (af == AF_UNIX) return 1;
    if (af == AF_INET) return 2;
    return af;
}

bool darwin_sockaddr_to_host(const void* src, socklen_t len,
                             struct sockaddr_storage* dst, socklen_t* dst_len) {
    if (!src || len < 2 || len > sizeof(struct sockaddr_storage)) return false;
    std::memset(dst, 0, sizeof(*dst));
    std::memcpy(dst, src, len);
    const auto* raw = static_cast<const std::uint8_t*>(src);
    const sa_family_t hf = static_cast<sa_family_t>(darwin_af_to_host(raw[1]));
    dst->ss_family = hf;
    *dst_len = len;
    return true;
}

void host_sockaddr_to_darwin(const struct sockaddr* src, socklen_t src_len,
                             void* dst, socklen_t* dst_len) {
    if (!src || !dst || !dst_len || *dst_len < 2) return;
    const socklen_t copy = std::min(src_len, *dst_len);
    std::memcpy(dst, src, copy);
    auto* raw = static_cast<std::uint8_t*>(dst);
    raw[0] = static_cast<std::uint8_t>(std::min<socklen_t>(src_len, 255));
    raw[1] = static_cast<std::uint8_t>(host_af_to_darwin(src->sa_family));
    *dst_len = src_len;
}

int darwin_sockopt_to_host(int dlevel, int dopt, int* hlevel, int* hopt, bool* noop) {
    *noop = false;
    if (dlevel == 0xffff) {
        *hlevel = SOL_SOCKET;
        switch (dopt) {
            case 0x0004: *hopt = SO_REUSEADDR; return 0;
            case 0x0008: *hopt = SO_KEEPALIVE; return 0;
            case 0x0020: *hopt = SO_BROADCAST; return 0;
            case 0x0200: *hopt = SO_REUSEPORT; return 0;
            case 0x1001: *hopt = SO_SNDBUF; return 0;
            case 0x1002: *hopt = SO_RCVBUF; return 0;
            case 0x1005: *hopt = SO_SNDTIMEO; return 0;
            case 0x1006: *hopt = SO_RCVTIMEO; return 0;
            case 0x1007: *hopt = SO_ERROR; return 0;
            case 0x1008: *hopt = SO_TYPE; return 0;
            case 0x1022: *noop = true; return 0;  // Darwin SO_NOSIGPIPE
            default: *hopt = dopt; return 0;
        }
    }
    *hlevel = dlevel;
    *hopt = dopt;
    return 0;
}
#endif

int shim_socket(int domain, int type, int protocol) {
#if defined(__APPLE__)
    return ::socket(domain, type, protocol);
#else
    return ::socket(darwin_af_to_host(domain), type, protocol);
#endif
}

int shim_connect(int fd, const void* addr, socklen_t len) {
#if defined(__APPLE__)
    return ::connect(fd, static_cast<const struct sockaddr*>(addr), len);
#else
    struct sockaddr_storage ss{};
    socklen_t slen = 0;
    if (!darwin_sockaddr_to_host(addr, len, &ss, &slen)) {
        errno = EINVAL;
        return -1;
    }
    return ::connect(fd, reinterpret_cast<const struct sockaddr*>(&ss), slen);
#endif
}

int shim_bind(int fd, const void* addr, socklen_t len) {
#if defined(__APPLE__)
    return ::bind(fd, static_cast<const struct sockaddr*>(addr), len);
#else
    struct sockaddr_storage ss{};
    socklen_t slen = 0;
    if (!darwin_sockaddr_to_host(addr, len, &ss, &slen)) {
        errno = EINVAL;
        return -1;
    }
    return ::bind(fd, reinterpret_cast<const struct sockaddr*>(&ss), slen);
#endif
}

int shim_accept(int fd, void* addr, socklen_t* len) {
#if defined(__APPLE__)
    return ::accept(fd, static_cast<struct sockaddr*>(addr), len);
#else
    if (!addr || !len) return ::accept(fd, nullptr, nullptr);
    struct sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    const int rc = ::accept(fd, reinterpret_cast<struct sockaddr*>(&ss), &slen);
    if (rc >= 0) {
        host_sockaddr_to_darwin(reinterpret_cast<const struct sockaddr*>(&ss), slen, addr, len);
    }
    return rc;
#endif
}

int shim_getsockname(int fd, void* addr, socklen_t* len) {
#if defined(__APPLE__)
    return ::getsockname(fd, static_cast<struct sockaddr*>(addr), len);
#else
    if (!addr || !len) return ::getsockname(fd, nullptr, nullptr);
    struct sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    const int rc = ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&ss), &slen);
    if (rc == 0) {
        host_sockaddr_to_darwin(reinterpret_cast<const struct sockaddr*>(&ss), slen, addr, len);
    }
    return rc;
#endif
}

int shim_getpeername(int fd, void* addr, socklen_t* len) {
#if defined(__APPLE__)
    return ::getpeername(fd, static_cast<struct sockaddr*>(addr), len);
#else
    if (!addr || !len) return ::getpeername(fd, nullptr, nullptr);
    struct sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    const int rc = ::getpeername(fd, reinterpret_cast<struct sockaddr*>(&ss), &slen);
    if (rc == 0) {
        host_sockaddr_to_darwin(reinterpret_cast<const struct sockaddr*>(&ss), slen, addr, len);
    }
    return rc;
#endif
}

ssize_t shim_sendto(int fd, const void* buf, std::size_t len, int flags,
                    const void* dest_addr, socklen_t addrlen) {
#if defined(__APPLE__)
    return ::sendto(fd, buf, len, flags,
                    static_cast<const struct sockaddr*>(dest_addr), addrlen);
#else
    if (!dest_addr || addrlen == 0) return ::send(fd, buf, len, flags);
    struct sockaddr_storage ss{};
    socklen_t slen = 0;
    if (!darwin_sockaddr_to_host(dest_addr, addrlen, &ss, &slen)) {
        errno = EINVAL;
        return -1;
    }
    return ::sendto(fd, buf, len, flags, reinterpret_cast<const struct sockaddr*>(&ss), slen);
#endif
}

ssize_t shim_recvfrom(int fd, void* buf, std::size_t len, int flags,
                      void* src_addr, socklen_t* addrlen) {
#if defined(__APPLE__)
    return ::recvfrom(fd, buf, len, flags,
                      static_cast<struct sockaddr*>(src_addr), addrlen);
#else
    if (!src_addr || !addrlen) return ::recv(fd, buf, len, flags);
    struct sockaddr_storage ss{};
    socklen_t slen = sizeof(ss);
    const ssize_t rc = ::recvfrom(fd, buf, len, flags,
                                  reinterpret_cast<struct sockaddr*>(&ss), &slen);
    if (rc >= 0) {
        host_sockaddr_to_darwin(reinterpret_cast<const struct sockaddr*>(&ss), slen,
                                src_addr, addrlen);
    }
    return rc;
#endif
}

int shim_setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
#if defined(__APPLE__)
    return ::setsockopt(fd, level, optname, optval, optlen);
#else
    int hlevel = level;
    int hopt = optname;
    bool noop = false;
    darwin_sockopt_to_host(level, optname, &hlevel, &hopt, &noop);
    if (noop) return 0;
    return ::setsockopt(fd, hlevel, hopt, optval, optlen);
#endif
}

int shim_getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen) {
#if defined(__APPLE__)
    return ::getsockopt(fd, level, optname, optval, optlen);
#else
    int hlevel = level;
    int hopt = optname;
    bool noop = false;
    darwin_sockopt_to_host(level, optname, &hlevel, &hopt, &noop);
    if (noop) return 0;
    return ::getsockopt(fd, hlevel, hopt, optval, optlen);
#endif
}

int shim_getaddrinfo(const char* node, const char* service,
                     const DarwinAddrinfo* hints, DarwinAddrinfo** res) {
#if defined(__APPLE__)
    return ::getaddrinfo(node, service,
                         reinterpret_cast<const struct addrinfo*>(hints),
                         reinterpret_cast<struct addrinfo**>(res));
#else
    struct addrinfo hhints{};
    const struct addrinfo* hptr = nullptr;
    if (hints) {
        hhints.ai_flags = hints->ai_flags;
        hhints.ai_family = darwin_af_to_host(hints->ai_family);
        hhints.ai_socktype = hints->ai_socktype;
        hhints.ai_protocol = hints->ai_protocol;
        hptr = &hhints;
    }
    struct addrinfo* hres = nullptr;
    const int rc = ::getaddrinfo(node, service, hptr, &hres);
    if (rc != 0) return rc;

    DarwinAddrinfo* head = nullptr;
    DarwinAddrinfo** tail = &head;
    for (struct addrinfo* cur = hres; cur != nullptr; cur = cur->ai_next) {
        auto* n = static_cast<DarwinAddrinfo*>(std::calloc(1, sizeof(DarwinAddrinfo)));
        if (!n) break;
        n->ai_flags = cur->ai_flags;
        n->ai_family = host_af_to_darwin(cur->ai_family);
        n->ai_socktype = cur->ai_socktype;
        n->ai_protocol = cur->ai_protocol;
        n->ai_addrlen = static_cast<std::uint32_t>(cur->ai_addrlen);
        if (cur->ai_canonname) n->ai_canonname = ::strdup(cur->ai_canonname);
        if (cur->ai_addr && cur->ai_addrlen > 0) {
            n->ai_addr = std::calloc(1, cur->ai_addrlen);
            if (n->ai_addr) {
                socklen_t dlen = cur->ai_addrlen;
                host_sockaddr_to_darwin(cur->ai_addr, cur->ai_addrlen, n->ai_addr, &dlen);
            }
        }
        *tail = n;
        tail = &n->ai_next;
    }
    ::freeaddrinfo(hres);
    if (res) *res = head;
    return 0;
#endif
}

void shim_freeaddrinfo(DarwinAddrinfo* res) {
#if defined(__APPLE__)
    ::freeaddrinfo(reinterpret_cast<struct addrinfo*>(res));
#else
    while (res) {
        DarwinAddrinfo* next = res->ai_next;
        std::free(res->ai_canonname);
        std::free(res->ai_addr);
        std::free(res);
        res = next;
    }
#endif
}

const char* shim_inet_ntop(int af, const void* src, char* dst, socklen_t size) {
#if defined(__APPLE__)
    return ::inet_ntop(af, src, dst, size);
#else
    return ::inet_ntop(darwin_af_to_host(af), src, dst, size);
#endif
}

int shim_inet_pton(int af, const char* src, void* dst) {
#if defined(__APPLE__)
    return ::inet_pton(af, src, dst);
#else
    return ::inet_pton(darwin_af_to_host(af), src, dst);
#endif
}

// --- Cryptographic PRNG: arc4random* ---
void shim_arc4random_buf(void* buf, std::size_t nbytes) {
    if (!buf || nbytes == 0) return;
    auto* out = static_cast<std::uint8_t*>(buf);
#if defined(__APPLE__)
    ::arc4random_buf(out, nbytes);
#elif defined(__linux__)
    std::size_t done = 0;
    while (done < nbytes) {
        const ssize_t r = ::getrandom(out + done, nbytes - done, 0);
        if (r > 0) {
            done += static_cast<std::size_t>(r);
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (done == nbytes) return;
    std::random_device rd;
    for (; done < nbytes; ++done) out[done] = static_cast<std::uint8_t>(rd() & 0xffu);
#else
    std::random_device rd;
    for (std::size_t i = 0; i < nbytes; ++i) out[i] = static_cast<std::uint8_t>(rd() & 0xffu);
#endif
}

std::uint32_t shim_arc4random() {
    std::uint32_t v = 0;
    shim_arc4random_buf(&v, sizeof(v));
    return v;
}

std::uint32_t shim_arc4random_uniform(std::uint32_t upper_bound) {
    if (upper_bound < 2) return 0;
    const std::uint32_t min_val = (0u - upper_bound) % upper_bound;
    for (;;) {
        const std::uint32_t r = shim_arc4random();
        if (r >= min_val) return r % upper_bound;
    }
}

// --- Clean-room CommonCrypto SHA-256 (FIPS 180-4) ---
struct DarwinSha256Ctx {
    std::uint32_t count[2];
    std::uint32_t hash[8];
    std::uint32_t wbuf[16];
};
static_assert(sizeof(DarwinSha256Ctx) == 104);

inline std::uint32_t rotr32(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

void sha256_transform(std::uint32_t state[8], const std::uint8_t block[64]) {
    static constexpr std::uint32_t kK[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

int shim_cc_sha256_init(DarwinSha256Ctx* ctx) {
    if (!ctx) return 0;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
    ctx->hash[0] = 0x6a09e667u;
    ctx->hash[1] = 0xbb67ae85u;
    ctx->hash[2] = 0x3c6ef372u;
    ctx->hash[3] = 0xa54ff53au;
    ctx->hash[4] = 0x510e527fu;
    ctx->hash[5] = 0x9b05688cu;
    ctx->hash[6] = 0x1f83d9abu;
    ctx->hash[7] = 0x5be0cd19u;
    std::memset(ctx->wbuf, 0, sizeof(ctx->wbuf));
    return 1;
}

int shim_cc_sha256_update(DarwinSha256Ctx* ctx, const void* data, std::uint32_t len) {
    if (!ctx) return 0;
    if (!data || len == 0) return 1;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t total_bytes =
        (static_cast<std::uint64_t>(ctx->count[1]) << 32) | ctx->count[0];
    std::size_t buf_used = static_cast<std::size_t>(total_bytes & 63u);
    total_bytes += len;
    ctx->count[0] = static_cast<std::uint32_t>(total_bytes & 0xffffffffu);
    ctx->count[1] = static_cast<std::uint32_t>(total_bytes >> 32);

    auto* buf = reinterpret_cast<std::uint8_t*>(ctx->wbuf);
    std::size_t off = 0;
    if (buf_used > 0) {
        const std::size_t take = std::min<std::size_t>(64 - buf_used, len);
        std::memcpy(buf + buf_used, bytes, take);
        buf_used += take;
        off += take;
        if (buf_used == 64) {
            sha256_transform(ctx->hash, buf);
            buf_used = 0;
        }
    }
    while (off + 64 <= len) {
        sha256_transform(ctx->hash, bytes + off);
        off += 64;
    }
    if (off < len) {
        std::memcpy(buf, bytes + off, len - off);
    }
    return 1;
}

int shim_cc_sha256_final(unsigned char* md, DarwinSha256Ctx* ctx) {
    if (!ctx || !md) return 0;
    const std::uint64_t total_bytes =
        (static_cast<std::uint64_t>(ctx->count[1]) << 32) | ctx->count[0];
    const std::uint64_t total_bits = total_bytes * 8u;
    std::size_t buf_used = static_cast<std::size_t>(total_bytes & 63u);
    auto* buf = reinterpret_cast<std::uint8_t*>(ctx->wbuf);

    buf[buf_used++] = 0x80u;
    if (buf_used > 56) {
        std::memset(buf + buf_used, 0, 64 - buf_used);
        sha256_transform(ctx->hash, buf);
        buf_used = 0;
    }
    std::memset(buf + buf_used, 0, 56 - buf_used);
    for (int i = 0; i < 8; ++i) {
        buf[56 + i] = static_cast<std::uint8_t>((total_bits >> (56 - i * 8)) & 0xffu);
    }
    sha256_transform(ctx->hash, buf);

    for (int i = 0; i < 8; ++i) {
        md[i * 4]     = static_cast<unsigned char>((ctx->hash[i] >> 24) & 0xffu);
        md[i * 4 + 1] = static_cast<unsigned char>((ctx->hash[i] >> 16) & 0xffu);
        md[i * 4 + 2] = static_cast<unsigned char>((ctx->hash[i] >> 8) & 0xffu);
        md[i * 4 + 3] = static_cast<unsigned char>(ctx->hash[i] & 0xffu);
    }
    return 1;
}

unsigned char* shim_cc_sha256(const void* data, std::uint32_t len, unsigned char* md) {
    static thread_local unsigned char fallback_md[32];
    unsigned char* dst = md ? md : fallback_md;
    DarwinSha256Ctx ctx{};
    shim_cc_sha256_init(&ctx);
    shim_cc_sha256_update(&ctx, data, len);
    shim_cc_sha256_final(dst, &ctx);
    return dst;
}

// --- Fortified memory & string operations ---
void shim_bzero(void* s, std::size_t n) {
    if (s && n > 0) std::memset(s, 0, n);
}

void shim_bcopy(const void* src, void* dst, std::size_t n) {
    if (n > 0) std::memmove(dst, src, n);
}

int shim_bcmp(const void* a, const void* b, std::size_t n) {
    return std::memcmp(a, b, n);
}

void* shim_memcpy_chk(void* dst, const void* src, std::size_t n, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && n > dstlen) {
        std::fprintf(stderr, "finn-darwin: __memcpy_chk buffer overflow detected\n");
        std::abort();
    }
    return std::memcpy(dst, src, n);
}

void* shim_memmove_chk(void* dst, const void* src, std::size_t n, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && n > dstlen) {
        std::fprintf(stderr, "finn-darwin: __memmove_chk buffer overflow detected\n");
        std::abort();
    }
    return std::memmove(dst, src, n);
}

void* shim_memset_chk(void* dst, int c, std::size_t n, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && n > dstlen) {
        std::fprintf(stderr, "finn-darwin: __memset_chk buffer overflow detected\n");
        std::abort();
    }
    return std::memset(dst, c, n);
}

int shim_memset_s(void* s, std::size_t smax, int c, std::size_t n) {
    if (!s) return EINVAL;
    const std::size_t count = std::min(smax, n);
    volatile unsigned char* p = static_cast<volatile unsigned char*>(s);
    for (std::size_t i = 0; i < count; ++i) p[i] = static_cast<unsigned char>(c);
    return (n > smax) ? EOVERFLOW : 0;
}

void shim_memset_pattern(void* b, const void* pat, std::size_t pat_sz, std::size_t len) {
    if (!b || !pat || pat_sz == 0) return;
    auto* dst = static_cast<std::uint8_t*>(b);
    const auto* p = static_cast<const std::uint8_t*>(pat);
    for (std::size_t i = 0; i < len; ++i) dst[i] = p[i % pat_sz];
}

void shim_memset_pattern4(void* b, const void* pat4, std::size_t len) {
    shim_memset_pattern(b, pat4, 4, len);
}
void shim_memset_pattern8(void* b, const void* pat8, std::size_t len) {
    shim_memset_pattern(b, pat8, 8, len);
}
void shim_memset_pattern16(void* b, const void* pat16, std::size_t len) {
    shim_memset_pattern(b, pat16, 16, len);
}

char* shim_strcpy_chk(char* dst, const char* src, std::size_t dstlen) {
    const std::size_t len = std::strlen(src) + 1;
    if (dstlen != static_cast<std::size_t>(-1) && len > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strcpy_chk buffer overflow detected\n");
        std::abort();
    }
    return std::strcpy(dst, src);
}

char* shim_strncpy_chk(char* dst, const char* src, std::size_t n, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && n > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strncpy_chk buffer overflow detected\n");
        std::abort();
    }
    return std::strncpy(dst, src, n);
}

char* shim_strcat_chk(char* dst, const char* src, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) &&
        std::strlen(dst) + std::strlen(src) + 1 > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strcat_chk buffer overflow detected\n");
        std::abort();
    }
    return std::strcat(dst, src);
}

char* shim_strncat_chk(char* dst, const char* src, std::size_t n, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) &&
        std::strlen(dst) + ::strnlen(src, n) + 1 > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strncat_chk buffer overflow detected\n");
        std::abort();
    }
    return std::strncat(dst, src, n);
}

char* shim_stpcpy_chk(char* dst, const char* src, std::size_t dstlen) {
    const std::size_t len = std::strlen(src);
    if (dstlen != static_cast<std::size_t>(-1) && len + 1 > dstlen) {
        std::fprintf(stderr, "finn-darwin: __stpcpy_chk buffer overflow detected\n");
        std::abort();
    }
    std::memcpy(dst, src, len + 1);
    return dst + len;
}

std::size_t shim_strlcpy(char* dst, const char* src, std::size_t siz) {
    const std::size_t srclen = std::strlen(src);
    if (siz > 0) {
        const std::size_t copy = (srclen >= siz) ? (siz - 1) : srclen;
        std::memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return srclen;
}

std::size_t shim_strlcpy_chk(char* dst, const char* src, std::size_t siz, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && siz > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strlcpy_chk buffer overflow detected\n");
        std::abort();
    }
    return shim_strlcpy(dst, src, siz);
}

std::size_t shim_strlcat(char* dst, const char* src, std::size_t siz) {
    const std::size_t dlen = std::strlen(dst);
    const std::size_t slen = std::strlen(src);
    if (dlen >= siz) return siz + slen;
    const std::size_t copy = std::min(slen, siz - dlen - 1);
    std::memcpy(dst + dlen, src, copy);
    dst[dlen + copy] = '\0';
    return dlen + slen;
}

std::size_t shim_strlcat_chk(char* dst, const char* src, std::size_t siz, std::size_t dstlen) {
    if (dstlen != static_cast<std::size_t>(-1) && siz > dstlen) {
        std::fprintf(stderr, "finn-darwin: __strlcat_chk buffer overflow detected\n");
        std::abort();
    }
    return shim_strlcat(dst, src, siz);
}

#if defined(__linux__) && defined(__aarch64__)
// Apple arm64 passes ALL variadic arguments (integers, pointers, and doubles)
// strictly on the stack at [sp+0], whereas Linux AAPCS64 va_arg inspects x0-x7
// and v0-v7 first. Consuming the remaining GP/FP register slots forces va_start
// to read directly from the caller's stack frame, matching Darwin arm64 ABI.
#define DARWIN_VA_REGS_1                                                                \
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,     \
        std::uintptr_t, std::uintptr_t, double, double, double, double, double, double, \
        double, double _finn_va_last
#define DARWIN_VA_REGS_2                                                                \
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,     \
        std::uintptr_t, double, double, double, double, double, double, double,         \
        double _finn_va_last
#define DARWIN_VA_REGS_3                                                                \
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,     \
        double, double, double, double, double, double, double, double _finn_va_last
#define DARWIN_VA_REGS_5                                                                \
    std::uintptr_t, std::uintptr_t, std::uintptr_t, double, double, double, double,     \
        double, double, double, double _finn_va_last

int shim_open(const char* path, int flags, DARWIN_VA_REGS_2, ...) {
    mode_t mode = 0;
    if (flags & 0x0200) {
        va_list ap;
        va_start(ap, _finn_va_last);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    return ::open(path, translate_darwin_open_flags(flags), mode);
}

int shim_fcntl(int fd, int cmd, DARWIN_VA_REGS_2, ...) {
    if (cmd == 3) {  // Darwin F_GETFL
        const int rc = ::fcntl(fd, F_GETFL);
        return (rc < 0) ? rc : translate_host_open_flags_to_darwin(rc);
    }
    if (cmd == 1) {  // F_GETFD
        return ::fcntl(fd, F_GETFD);
    }
    va_list ap;
    va_start(ap, _finn_va_last);
    const long arg = va_arg(ap, long);
    va_end(ap);
    if (cmd == 4) {  // Darwin F_SETFL
        return ::fcntl(fd, F_SETFL, translate_darwin_open_flags(static_cast<int>(arg)));
    }
    if (cmd == 67) {  // Darwin F_DUPFD_CLOEXEC
        return ::fcntl(fd, F_DUPFD_CLOEXEC, static_cast<int>(arg));
    }
    return ::fcntl(fd, cmd, arg);
}

int shim_darwin_printf(const char* fmt, DARWIN_VA_REGS_1, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

int shim_darwin_fprintf(FILE* stream, const char* fmt, DARWIN_VA_REGS_2, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_darwin_snprintf(char* buf, std::size_t maxlen, const char* fmt, DARWIN_VA_REGS_3, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_sprintf(char* buf, const char* fmt, DARWIN_VA_REGS_2, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vsnprintf(buf, 1u << 20, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_snprintf_chk(char* buf, std::size_t maxlen, int, std::size_t, const char* fmt,
                      DARWIN_VA_REGS_5, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_sprintf_chk(char* buf, int, std::size_t maxlen, const char* fmt,
                     std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                     double, double, double, double, double, double, double,
                     double _finn_va_last, ...) {
    va_list ap;
    va_start(ap, _finn_va_last);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}
#else
int shim_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & 0x0200) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    return ::open(path, translate_darwin_open_flags(flags), mode);
}

int shim_fcntl(int fd, int cmd, ...) {
#if defined(__APPLE__)
    if (cmd == F_GETFD || cmd == F_GETFL || cmd == F_GETOWN) {
        return ::fcntl(fd, cmd);
    }
    va_list ap;
    va_start(ap, cmd);
    const long arg = va_arg(ap, long);
    va_end(ap);
    return ::fcntl(fd, cmd, arg);
#else
    if (cmd == 3) {  // Darwin F_GETFL
        const int rc = ::fcntl(fd, F_GETFL);
        return (rc < 0) ? rc : translate_host_open_flags_to_darwin(rc);
    }
    if (cmd == 1) {  // F_GETFD
        return ::fcntl(fd, F_GETFD);
    }
    va_list ap;
    va_start(ap, cmd);
    const long arg = va_arg(ap, long);
    va_end(ap);
    if (cmd == 4) {  // Darwin F_SETFL
        return ::fcntl(fd, F_SETFL, translate_darwin_open_flags(static_cast<int>(arg)));
    }
    if (cmd == 67) {  // Darwin F_DUPFD_CLOEXEC
        return ::fcntl(fd, F_DUPFD_CLOEXEC, static_cast<int>(arg));
    }
    return ::fcntl(fd, cmd, arg);
#endif
}

int shim_darwin_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

int shim_darwin_fprintf(FILE* stream, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_darwin_snprintf(char* buf, std::size_t maxlen, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vsnprintf(buf, 1u << 20, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_snprintf_chk(char* buf, std::size_t maxlen, int, std::size_t, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}

int shim_sprintf_chk(char* buf, int, std::size_t maxlen, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int rc = std::vsnprintf(buf, maxlen, fmt, ap);
    va_end(ap);
    return rc;
}
#endif

// --- Mach time & os_unfair_lock & GCD (libdispatch) ---
struct DarwinMachTimebase {
    std::uint32_t numer;
    std::uint32_t denom;
};

std::uint64_t shim_mach_absolute_time() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

int shim_mach_timebase_info(DarwinMachTimebase* info) {
    if (info) {
        info->numer = 1;
        info->denom = 1;
    }
    return 0;
}

void shim_os_unfair_lock_lock(std::uint32_t* lock) {
    if (!lock) return;
    auto* a = reinterpret_cast<std::atomic<std::uint32_t>*>(lock);
    std::uint32_t expected = 0;
    while (!a->compare_exchange_weak(expected, 1, std::memory_order_acquire)) {
        expected = 0;
        std::this_thread::yield();
    }
}

void shim_os_unfair_lock_unlock(std::uint32_t* lock) {
    if (!lock) return;
    auto* a = reinterpret_cast<std::atomic<std::uint32_t>*>(lock);
    a->store(0, std::memory_order_release);
}

bool shim_os_unfair_lock_trylock(std::uint32_t* lock) {
    if (!lock) return false;
    auto* a = reinterpret_cast<std::atomic<std::uint32_t>*>(lock);
    std::uint32_t expected = 0;
    return a->compare_exchange_strong(expected, 1, std::memory_order_acquire);
}

int g_dispatch_main_q = 1;
int g_dispatch_global_q = 2;

void* shim_dispatch_get_main_queue() { return &g_dispatch_main_q; }
void* shim_dispatch_get_global_queue(long, unsigned long) { return &g_dispatch_global_q; }
void* shim_dispatch_queue_create(const char*, void*) { return &g_dispatch_global_q; }
void shim_dispatch_retain(void*) {}
void shim_dispatch_release(void*) {}

void shim_dispatch_once(std::intptr_t* predicate, void* block) {
    if (!predicate || !block) return;
    auto* pred = reinterpret_cast<std::atomic<std::intptr_t>*>(predicate);
    std::intptr_t expected = 0;
    if (pred->compare_exchange_strong(expected, -1, std::memory_order_acq_rel)) {
        const auto* blk = reinterpret_cast<const DarwinBlockLayout*>(block);
        if (blk->invoke) blk->invoke(block);
    }
}

void shim_dispatch_once_f(std::intptr_t* predicate, void* ctx, void (*fn)(void*)) {
    if (!predicate || !fn) return;
    auto* pred = reinterpret_cast<std::atomic<std::intptr_t>*>(predicate);
    std::intptr_t expected = 0;
    if (pred->compare_exchange_strong(expected, -1, std::memory_order_acq_rel)) {
        fn(ctx);
    }
}

void shim_dispatch_sync(void*, void* block) {
    if (!block) return;
    const auto* blk = reinterpret_cast<const DarwinBlockLayout*>(block);
    if (blk->invoke) blk->invoke(block);
}

void shim_dispatch_async(void*, void* block) {
    if (!block) return;
    const auto* blk = reinterpret_cast<const DarwinBlockLayout*>(block);
    if (blk->invoke) blk->invoke(block);
}

// --- Darwin process introspection & sysctlbyname ---
int* shim_ns_get_argc() { return &g_argc; }
char*** shim_ns_get_argv() { return &g_argv; }

int shim_ns_get_executable_path(char* buf, std::uint32_t* bufsize) {
    if (!bufsize) return -1;
    const std::uint32_t need = static_cast<std::uint32_t>(g_exec_path.size() + 1);
    if (!buf || *bufsize < need) {
        *bufsize = need;
        return -1;
    }
    std::memcpy(buf, g_exec_path.c_str(), need);
    return 0;
}

int shim_sysctlbyname(const char* name, void* oldp, std::size_t* oldlenp, void*, std::size_t) {
    if (!name || !oldlenp) return -1;
    auto write_str = [&](const char* val) -> int {
        const std::size_t len = std::strlen(val) + 1;
        if (!oldp) { *oldlenp = len; return 0; }
        if (*oldlenp < len) { *oldlenp = len; return -1; }
        std::memcpy(oldp, val, len);
        *oldlenp = len;
        return 0;
    };
    auto write_u32 = [&](std::uint32_t v) -> int {
        if (!oldp) { *oldlenp = sizeof(v); return 0; }
        if (*oldlenp < sizeof(v)) { *oldlenp = sizeof(v); return -1; }
        std::memcpy(oldp, &v, sizeof(v));
        *oldlenp = sizeof(v);
        return 0;
    };
    if (std::strcmp(name, "kern.osproductversion") == 0) return write_str("14.0");
    if (std::strcmp(name, "kern.ostype") == 0) return write_str("Darwin");
    if (std::strcmp(name, "hw.ncpu") == 0 || std::strcmp(name, "hw.logicalcpu") == 0 ||
        std::strcmp(name, "hw.physicalcpu") == 0) {
        return write_u32(std::max(1u, std::thread::hardware_concurrency()));
    }
    if (std::strcmp(name, "hw.machine") == 0) {
        struct utsname u{};
        if (::uname(&u) == 0) return write_str(u.machine);
        return write_str("arm64");
    }
    errno = ENOENT;
    return -1;
}

void* shim_os_log_create(const char*, const char*) {
    static int dummy_log = 1;
    return &dummy_log;
}
bool shim_os_log_type_enabled(void*, std::uint8_t) { return false; }
void shim_os_log_impl(void*, void*, std::uint8_t, const char*, std::uint8_t*, std::uint32_t) {}

int shim_cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void shim_dyld_stub_binder() {}

// Avoid overload ambiguity for C math functions
double m_sin(double x) { return std::sin(x); }
double m_cos(double x) { return std::cos(x); }
double m_tan(double x) { return std::tan(x); }
double m_sqrt(double x) { return std::sqrt(x); }
double m_pow(double a, double b) { return std::pow(a, b); }
double m_floor(double x) { return std::floor(x); }
double m_ceil(double x) { return std::ceil(x); }
double m_round(double x) { return std::round(x); }
double m_fabs(double x) { return std::fabs(x); }
double m_fmod(double a, double b) { return std::fmod(a, b); }
double m_log(double x) { return std::log(x); }
double m_log10(double x) { return std::log10(x); }
double m_exp(double x) { return std::exp(x); }
double m_atan2(double y, double x) { return std::atan2(y, x); }
double m_hypot(double x, double y) { return std::hypot(x, y); }

}  // namespace

void set_darwin_process_args(int argc, char** argv, const std::string& executable_path) {
    g_argc = argc;
    g_argv = argv;
    g_exec_path = executable_path;
}

void register_libsystem_symbols(std::unordered_map<std::string, void*>* table) {
    g_darwin_stdin = stdin;
    g_darwin_stdout = stdout;
    g_darwin_stderr = stderr;

    auto add_fn = [&](const char* name, auto fn) {
        (*table)[name] = fn_ptr(fn);
    };
    auto add_data = [&](const char* name, void* ptr) {
        (*table)[name] = ptr;
    };

    // Standard I/O
    add_data("___stdinp", &g_darwin_stdin);
    add_data("___stdoutp", &g_darwin_stdout);
    add_data("___stderrp", &g_darwin_stderr);
    add_data("___stack_chk_guard", &g_stack_chk_guard);
    add_fn("___stack_chk_fail", &shim_stack_chk_fail);
    add_fn("___error", &shim_errno);
    add_fn("___cxa_atexit", &shim_cxa_atexit);
    add_fn("dyld_stub_binder", &shim_dyld_stub_binder);
    add_fn("_dyld_stub_binder", &shim_dyld_stub_binder);

    add_fn("_printf", &shim_darwin_printf);
    add_fn("_fprintf", &shim_darwin_fprintf);
    add_fn("_vprintf", &::vprintf);
    add_fn("_vfprintf", &::vfprintf);
    add_fn("_sprintf", &shim_sprintf);
    add_fn("_snprintf", &shim_darwin_snprintf);
    add_fn("_vsnprintf", &::vsnprintf);
    add_fn("_puts", &::puts);
    add_fn("_fputs", &::fputs);
    add_fn("_putchar", &::putchar);
    add_fn("_fputc", &::fputc);
    add_fn("_fwrite", &::fwrite);
    add_fn("_fread", &::fread);
    add_fn("_fopen", &::fopen);
    add_fn("_fclose", &::fclose);
    add_fn("_fflush", &::fflush);
    add_fn("_fseek", &::fseek);
    add_fn("_ftell", &::ftell);
    add_fn("_rewind", &::rewind);
    add_fn("_fgets", &::fgets);
    add_fn("_perror", &::perror);
    add_fn("_sscanf", &::sscanf);

    // Memory & string operations (including fortified *_chk variants)
    add_fn("_malloc", &::malloc);
    add_fn("_calloc", &::calloc);
    add_fn("_realloc", &::realloc);
    add_fn("_free", &::free);
    add_fn("_posix_memalign", &::posix_memalign);
    add_fn("_memcpy", &::memcpy);
    add_fn("_memmove", &::memmove);
    add_fn("_memset", &::memset);
    add_fn("_memset_s", &shim_memset_s);
    add_fn("_memset_pattern4", &shim_memset_pattern4);
    add_fn("_memset_pattern8", &shim_memset_pattern8);
    add_fn("_memset_pattern16", &shim_memset_pattern16);
    add_fn("_memcmp", &::memcmp);
    add_fn("_memchr", static_cast<void*(*)(void*, int, std::size_t)>(&std::memchr));
    add_fn("_bzero", &shim_bzero);
    add_fn("___bzero", &shim_bzero);
    add_fn("_bcopy", &shim_bcopy);
    add_fn("_bcmp", &shim_bcmp);
    add_fn("___memcpy_chk", &shim_memcpy_chk);
    add_fn("___memmove_chk", &shim_memmove_chk);
    add_fn("___memset_chk", &shim_memset_chk);
    add_fn("___strcpy_chk", &shim_strcpy_chk);
    add_fn("___strncpy_chk", &shim_strncpy_chk);
    add_fn("___strcat_chk", &shim_strcat_chk);
    add_fn("___strncat_chk", &shim_strncat_chk);
    add_fn("___stpcpy_chk", &shim_stpcpy_chk);
    add_fn("___strlcpy_chk", &shim_strlcpy_chk);
    add_fn("___strlcat_chk", &shim_strlcat_chk);
    add_fn("___snprintf_chk", &shim_snprintf_chk);
    add_fn("___sprintf_chk", &shim_sprintf_chk);
    add_fn("_strlen", &::strlen);
    add_fn("_strnlen", &::strnlen);
    add_fn("_strcmp", &::strcmp);
    add_fn("_strncmp", &::strncmp);
    add_fn("_strcasecmp", &::strcasecmp);
    add_fn("_strncasecmp", &::strncasecmp);
    add_fn("_strcpy", &::strcpy);
    add_fn("_strncpy", &::strncpy);
    add_fn("_strlcpy", &shim_strlcpy);
    add_fn("_strcat", &::strcat);
    add_fn("_strncat", &::strncat);
    add_fn("_strlcat", &shim_strlcat);
    add_fn("_strchr", static_cast<char*(*)(char*, int)>(&std::strchr));
    add_fn("_strrchr", static_cast<char*(*)(char*, int)>(&std::strrchr));
    add_fn("_strstr", static_cast<char*(*)(char*, const char*)>(&std::strstr));
    add_fn("_strdup", &::strdup);
    add_fn("_strndup", &::strndup);
    add_fn("_strtok", &::strtok);
    add_fn("_strtok_r", &::strtok_r);
    add_fn("_strtol", &::strtol);
    add_fn("_strtoll", &::strtoll);
    add_fn("_strtoul", &::strtoul);
    add_fn("_strtoull", &::strtoull);
    add_fn("_strtod", &::strtod);
    add_fn("_atoi", &::atoi);
    add_fn("_atol", &::atol);
    add_fn("_atof", &::atof);
    add_fn("_qsort", &::qsort);
    add_fn("_bsearch", &::bsearch);

    // POSIX & Darwin ABI-translated syscalls, directories, memory mapping
    add_fn("_open", &shim_open);
    add_fn("_fcntl", &shim_fcntl);
    add_fn("_close", &::close);
    add_fn("_read", &::read);
    add_fn("_write", &::write);
    add_fn("_pread", &::pread);
    add_fn("_pwrite", &::pwrite);
    add_fn("_lseek", &::lseek);
    add_fn("_dup", &::dup);
    add_fn("_dup2", &::dup2);
    add_fn("_pipe", &::pipe);
    add_fn("_mmap", &shim_mmap);
    add_fn("_munmap", &::munmap);
    add_fn("_mprotect", &::mprotect);
    add_fn("_sysconf", &shim_sysconf);
    add_fn("_stat", &shim_stat);
    add_fn("_stat$INODE64", &shim_stat);
    add_fn("_lstat", &shim_lstat);
    add_fn("_lstat$INODE64", &shim_lstat);
    add_fn("_fstat", &shim_fstat);
    add_fn("_fstat$INODE64", &shim_fstat);
    add_fn("_realpath", &::realpath);
    add_fn("_realpath$DARWIN_EXTSN", &::realpath);
    add_fn("_opendir", &shim_opendir);
    add_fn("_opendir$INODE64", &shim_opendir);
    add_fn("_readdir", &shim_readdir);
    add_fn("_readdir$INODE64", &shim_readdir);
    add_fn("_closedir", &shim_closedir);
    add_fn("_mkstemp", &::mkstemp);
    add_fn("_chmod", &::chmod);
    add_fn("_symlink", &::symlink);
    add_fn("_readlink", &::readlink);
    add_fn("_access", &::access);
    add_fn("_mkdir", &::mkdir);
    add_fn("_rmdir", &::rmdir);
    add_fn("_unlink", &::unlink);
    add_fn("_rename", &::rename);
    add_fn("_getcwd", &::getcwd);
    add_fn("_chdir", &::chdir);
    add_fn("_isatty", &::isatty);
    add_fn("_getenv", &::getenv);
    add_fn("_setenv", &::setenv);
    add_fn("_unsetenv", &::unsetenv);
    add_fn("_exit", &::exit);
    add_fn("__exit", &::_exit);
    add_fn("_abort", &::abort);
    add_fn("_atexit", &::atexit);

    // BSD sockets & DNS resolution
    add_fn("_socket", &shim_socket);
    add_fn("_connect", &shim_connect);
    add_fn("_bind", &shim_bind);
    add_fn("_listen", &::listen);
    add_fn("_accept", &shim_accept);
    add_fn("_send", &::send);
    add_fn("_recv", &::recv);
    add_fn("_sendto", &shim_sendto);
    add_fn("_recvfrom", &shim_recvfrom);
    add_fn("_setsockopt", &shim_setsockopt);
    add_fn("_getsockopt", &shim_getsockopt);
    add_fn("_getsockname", &shim_getsockname);
    add_fn("_getpeername", &shim_getpeername);
    add_fn("_shutdown", &::shutdown);
    add_fn("_getaddrinfo", &shim_getaddrinfo);
    add_fn("_freeaddrinfo", &shim_freeaddrinfo);
    add_fn("_gai_strerror", &::gai_strerror);
    add_fn("_inet_ntop", &shim_inet_ntop);
    add_fn("_inet_pton", &shim_inet_pton);

    // CSPRNG & CommonCrypto SHA-256
    add_fn("_arc4random", &shim_arc4random);
    add_fn("_arc4random_buf", &shim_arc4random_buf);
    add_fn("_arc4random_uniform", &shim_arc4random_uniform);
    add_fn("_CC_SHA256", &shim_cc_sha256);
    add_fn("_CC_SHA256_Init", &shim_cc_sha256_init);
    add_fn("_CC_SHA256_Update", &shim_cc_sha256_update);
    add_fn("_CC_SHA256_Final", &shim_cc_sha256_final);

    // Time, process, Mach, pthreads, locks, GCD
    add_fn("_time", &::time);
    add_fn("_localtime", &::localtime);
    add_fn("_localtime_r", &::localtime_r);
    add_fn("_gmtime_r", &::gmtime_r);
    add_fn("_strftime", &::strftime);
    add_fn("_gettimeofday", &::gettimeofday);
    add_fn("_clock_gettime", &shim_clock_gettime);
    add_fn("_usleep", &::usleep);
    add_fn("_sleep", &::sleep);
    add_fn("_nanosleep", &::nanosleep);
    add_fn("_getpid", &::getpid);
    add_fn("_getppid", &::getppid);
    add_fn("_getuid", &::getuid);
    add_fn("_geteuid", &::geteuid);
    add_fn("_getgid", &::getgid);
    add_fn("_mach_absolute_time", &shim_mach_absolute_time);
    add_fn("_mach_timebase_info", &shim_mach_timebase_info);
    add_fn("_os_unfair_lock_lock", &shim_os_unfair_lock_lock);
    add_fn("_os_unfair_lock_unlock", &shim_os_unfair_lock_unlock);
    add_fn("_os_unfair_lock_trylock", &shim_os_unfair_lock_trylock);
    add_fn("_pthread_create", &::pthread_create);
    add_fn("_pthread_join", &::pthread_join);
    add_fn("_pthread_detach", &::pthread_detach);
    add_fn("_pthread_self", &::pthread_self);
    add_fn("_pthread_equal", &::pthread_equal);
    add_fn("_pthread_attr_init", &::pthread_attr_init);
    add_fn("_pthread_attr_destroy", &::pthread_attr_destroy);
    add_fn("_pthread_attr_setdetachstate", &::pthread_attr_setdetachstate);
    add_fn("_pthread_mutex_init", &::pthread_mutex_init);
    add_fn("_pthread_mutex_lock", &::pthread_mutex_lock);
    add_fn("_pthread_mutex_trylock", &::pthread_mutex_trylock);
    add_fn("_pthread_mutex_unlock", &::pthread_mutex_unlock);
    add_fn("_pthread_mutex_destroy", &::pthread_mutex_destroy);
    add_fn("_pthread_cond_init", &::pthread_cond_init);
    add_fn("_pthread_cond_wait", &::pthread_cond_wait);
    add_fn("_pthread_cond_timedwait", &::pthread_cond_timedwait);
    add_fn("_pthread_cond_signal", &::pthread_cond_signal);
    add_fn("_pthread_cond_broadcast", &::pthread_cond_broadcast);
    add_fn("_pthread_cond_destroy", &::pthread_cond_destroy);
    add_fn("_pthread_rwlock_init", &::pthread_rwlock_init);
    add_fn("_pthread_rwlock_rdlock", &::pthread_rwlock_rdlock);
    add_fn("_pthread_rwlock_tryrdlock", &::pthread_rwlock_tryrdlock);
    add_fn("_pthread_rwlock_wrlock", &::pthread_rwlock_wrlock);
    add_fn("_pthread_rwlock_trywrlock", &::pthread_rwlock_trywrlock);
    add_fn("_pthread_rwlock_unlock", &::pthread_rwlock_unlock);
    add_fn("_pthread_rwlock_destroy", &::pthread_rwlock_destroy);
    add_fn("_pthread_once", &::pthread_once);
    add_fn("_pthread_key_create", &::pthread_key_create);
    add_fn("_pthread_getspecific", &::pthread_getspecific);
    add_fn("_pthread_setspecific", &::pthread_setspecific);
    add_data("__dispatch_main_q", &g_dispatch_main_q);
    add_fn("_dispatch_get_main_queue", &shim_dispatch_get_main_queue);
    add_fn("_dispatch_get_global_queue", &shim_dispatch_get_global_queue);
    add_fn("_dispatch_queue_create", &shim_dispatch_queue_create);
    add_fn("_dispatch_retain", &shim_dispatch_retain);
    add_fn("_dispatch_release", &shim_dispatch_release);
    add_fn("_dispatch_once", &shim_dispatch_once);
    add_fn("_dispatch_once_f", &shim_dispatch_once_f);
    add_fn("_dispatch_sync", &shim_dispatch_sync);
    add_fn("_dispatch_async", &shim_dispatch_async);
    add_fn("_sysctlbyname", &shim_sysctlbyname);
    add_fn("__NSGetArgc", &shim_ns_get_argc);
    add_fn("__NSGetArgv", &shim_ns_get_argv);
    add_fn("__NSGetExecutablePath", &shim_ns_get_executable_path);
    add_fn("_dlopen", &::dlopen);
    add_fn("_dlsym", &::dlsym);
    add_fn("_dlclose", &::dlclose);
    add_fn("_dlerror", &::dlerror);
    add_fn("_os_log_create", &shim_os_log_create);
    add_fn("_os_log_type_enabled", &shim_os_log_type_enabled);
    add_fn("__os_log_impl", &shim_os_log_impl);

    // Math
    add_fn("_sin", &m_sin);
    add_fn("_cos", &m_cos);
    add_fn("_tan", &m_tan);
    add_fn("_sqrt", &m_sqrt);
    add_fn("_pow", &m_pow);
    add_fn("_floor", &m_floor);
    add_fn("_ceil", &m_ceil);
    add_fn("_round", &m_round);
    add_fn("_fabs", &m_fabs);
    add_fn("_fmod", &m_fmod);
    add_fn("_log", &m_log);
    add_fn("_log10", &m_log10);
    add_fn("_exp", &m_exp);
    add_fn("_atan2", &m_atan2);
    add_fn("_hypot", &m_hypot);
}

}  // namespace finn::darwin
