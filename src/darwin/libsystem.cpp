#include "darwin/libsystem.hpp"

#include "darwin/objc_runtime.hpp"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
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
    if (e == EAGAIN) return 35;       // Darwin EAGAIN/EWOULDBLOCK = 35
    if (e == ENAMETOOLONG) return 63; // Darwin ENAMETOOLONG = 63
    if (e == ENOTEMPTY) return 66;    // Darwin ENOTEMPTY = 66
    if (e == ELOOP) return 62;        // Darwin ELOOP = 62
    if (e == EOPNOTSUPP) return 102;  // Darwin EOPNOTSUPP = 102
    if (e == ECONNREFUSED) return 61; // Darwin ECONNREFUSED = 61
    if (e == ETIMEDOUT) return 60;    // Darwin ETIMEDOUT = 60
    return e;
}

// Translates Darwin open(2) flags onto host OS flags.
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

int* shim_errno() {
    g_darwin_errno = to_darwin_errno(errno);
    return &g_darwin_errno;
}

void shim_stack_chk_fail() {
    std::fprintf(stderr, "finn-darwin: stack smashing detected\n");
    std::abort();
}

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

void shim_bzero(void* s, std::size_t n) {
    if (s && n > 0) std::memset(s, 0, n);
}

void* shim_memcpy_chk(void* dst, const void* src, std::size_t n, std::size_t) {
    return std::memcpy(dst, src, n);
}

void* shim_memmove_chk(void* dst, const void* src, std::size_t n, std::size_t) {
    return std::memmove(dst, src, n);
}

void* shim_memset_chk(void* dst, int c, std::size_t n, std::size_t) {
    return std::memset(dst, c, n);
}

char* shim_strcpy_chk(char* dst, const char* src, std::size_t) {
    return std::strcpy(dst, src);
}

char* shim_strncpy_chk(char* dst, const char* src, std::size_t n, std::size_t) {
    return std::strncpy(dst, src, n);
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

std::size_t shim_strlcpy_chk(char* dst, const char* src, std::size_t siz, std::size_t) {
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

    // Memory & string operations
    add_fn("_malloc", &::malloc);
    add_fn("_calloc", &::calloc);
    add_fn("_realloc", &::realloc);
    add_fn("_free", &::free);
    add_fn("_posix_memalign", &::posix_memalign);
    add_fn("_memcpy", &::memcpy);
    add_fn("_memmove", &::memmove);
    add_fn("_memset", &::memset);
    add_fn("_memcmp", &::memcmp);
    add_fn("_memchr", static_cast<void*(*)(void*, int, std::size_t)>(&std::memchr));
    add_fn("_bzero", &shim_bzero);
    add_fn("___bzero", &shim_bzero);
    add_fn("___memcpy_chk", &shim_memcpy_chk);
    add_fn("___memmove_chk", &shim_memmove_chk);
    add_fn("___memset_chk", &shim_memset_chk);
    add_fn("___strcpy_chk", &shim_strcpy_chk);
    add_fn("___strncpy_chk", &shim_strncpy_chk);
    add_fn("___strlcpy_chk", &shim_strlcpy_chk);
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

    // POSIX & Darwin ABI-translated syscalls
    add_fn("_open", &shim_open);
    add_fn("_close", &::close);
    add_fn("_read", &::read);
    add_fn("_write", &::write);
    add_fn("_pread", &::pread);
    add_fn("_pwrite", &::pwrite);
    add_fn("_lseek", &::lseek);
    add_fn("_dup", &::dup);
    add_fn("_dup2", &::dup2);
    add_fn("_pipe", &::pipe);
    add_fn("_stat", &shim_stat);
    add_fn("_stat$INODE64", &shim_stat);
    add_fn("_lstat", &shim_lstat);
    add_fn("_lstat$INODE64", &shim_lstat);
    add_fn("_fstat", &shim_fstat);
    add_fn("_fstat$INODE64", &shim_fstat);
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

    // Time, process, Mach, locks, GCD
    add_fn("_time", &::time);
    add_fn("_localtime", &::localtime);
    add_fn("_localtime_r", &::localtime_r);
    add_fn("_gmtime_r", &::gmtime_r);
    add_fn("_strftime", &::strftime);
    add_fn("_gettimeofday", &::gettimeofday);
    add_fn("_clock_gettime", &::clock_gettime);
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
    add_fn("_pthread_mutex_init", &::pthread_mutex_init);
    add_fn("_pthread_mutex_lock", &::pthread_mutex_lock);
    add_fn("_pthread_mutex_unlock", &::pthread_mutex_unlock);
    add_fn("_pthread_mutex_destroy", &::pthread_mutex_destroy);
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
