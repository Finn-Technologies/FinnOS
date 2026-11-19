#include "darwin/swift_runtime.hpp"

#include "darwin/objc_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace finn::darwin {
namespace {

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// Matches Swift 5.x 64-bit HeapObject header layout (16 bytes).
struct SwiftHeapObject {
    void* metadata;
    std::atomic<std::uint64_t> ref_counts;
};
static_assert(sizeof(SwiftHeapObject) == 16);

struct SwiftAccessBuffer {
    void* pointer;
    std::uintptr_t flags;
    void* pc;
};

struct SwiftRuntimeState {
    std::mutex mu;
    std::unordered_set<void*> live_objects;
};

SwiftRuntimeState& swift_rt() {
    static SwiftRuntimeState state;
    return state;
}

std::size_t normalize_alignment(std::size_t align_mask) {
    std::size_t req = align_mask + 1;
    if (req < alignof(void*)) req = alignof(void*);
    // Round up to a power of two required by posix_memalign
    std::size_t pow2 = alignof(void*);
    while (pow2 < req && pow2 < (std::size_t{1} << 20)) pow2 <<= 1;
    return pow2;
}

bool is_live_swift_object(void* ptr) {
    if (!ptr) return false;
    auto& st = swift_rt();
    std::lock_guard<std::mutex> lk(st.mu);
    return st.live_objects.find(ptr) != st.live_objects.end();
}

void* shim_swift_slowAlloc(std::size_t bytes, std::size_t align_mask) {
    const std::size_t sz = (bytes == 0) ? 1 : bytes;
    const std::size_t align = normalize_alignment(align_mask);
    void* ptr = nullptr;
    if (::posix_memalign(&ptr, align, sz) != 0) return nullptr;
    return ptr;
}

void shim_swift_slowDealloc(void* ptr, std::size_t, std::size_t) {
    std::free(ptr);
}

SwiftHeapObject* shim_swift_allocObject(void* metadata, std::size_t required_size,
                                        std::size_t required_align_mask) {
    const std::size_t sz = std::max(required_size, sizeof(SwiftHeapObject));
    const std::size_t align = normalize_alignment(required_align_mask);
    void* raw = nullptr;
    if (::posix_memalign(&raw, align, sz) != 0 || !raw) return nullptr;
    std::memset(raw, 0, sz);

    auto* obj = static_cast<SwiftHeapObject*>(raw);
    obj->metadata = metadata;
    obj->ref_counts.store(1, std::memory_order_relaxed);

    auto& st = swift_rt();
    std::lock_guard<std::mutex> lk(st.mu);
    st.live_objects.insert(obj);
    return obj;
}

void shim_swift_deallocObject(SwiftHeapObject* obj, std::size_t, std::size_t) {
    if (!obj) return;
    bool owned = false;
    {
        auto& st = swift_rt();
        std::lock_guard<std::mutex> lk(st.mu);
        owned = (st.live_objects.erase(obj) > 0);
    }
    if (owned) {
        obj->metadata = nullptr;
        obj->ref_counts.store(0, std::memory_order_relaxed);
        std::free(obj);
    }
}

void shim_swift_deallocClassInstance(SwiftHeapObject* obj, std::size_t size,
                                     std::size_t align_mask) {
    shim_swift_deallocObject(obj, size, align_mask);
}

SwiftHeapObject* shim_swift_retain_n(SwiftHeapObject* obj, std::uint32_t n) {
    if (!obj || n == 0) return obj;
    if (is_live_swift_object(obj)) {
        obj->ref_counts.fetch_add(n, std::memory_order_relaxed);
    } else {
        for (std::uint32_t i = 0; i < n; ++i) {
            finn_objc_retain(reinterpret_cast<ObjcId>(obj));
        }
    }
    return obj;
}

SwiftHeapObject* shim_swift_retain(SwiftHeapObject* obj) {
    return shim_swift_retain_n(obj, 1);
}

void shim_swift_release_n(SwiftHeapObject* obj, std::uint32_t n) {
    if (!obj || n == 0) return;
    if (is_live_swift_object(obj)) {
        const std::uint64_t prev = obj->ref_counts.fetch_sub(n, std::memory_order_acq_rel);
        if (prev <= n) {
            shim_swift_deallocObject(obj, 0, 0);
        }
    } else {
        for (std::uint32_t i = 0; i < n; ++i) {
            finn_objc_release(reinterpret_cast<ObjcId>(obj));
        }
    }
}

void shim_swift_release(SwiftHeapObject* obj) {
    shim_swift_release_n(obj, 1);
}

std::size_t shim_swift_retainCount(SwiftHeapObject* obj) {
    if (!obj || !is_live_swift_object(obj)) return 0;
    return static_cast<std::size_t>(obj->ref_counts.load(std::memory_order_acquire));
}

bool shim_swift_isUniquelyReferenced_nonNull_native(SwiftHeapObject* obj) {
    if (!obj || !is_live_swift_object(obj)) return false;
    return obj->ref_counts.load(std::memory_order_acquire) == 1;
}

void* shim_swift_unknownObjectRetain_n(void* obj, std::uint32_t n) {
    if (!obj || n == 0) return obj;
    if (is_live_swift_object(obj)) {
        return shim_swift_retain_n(static_cast<SwiftHeapObject*>(obj), n);
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        finn_objc_retain(static_cast<ObjcId>(obj));
    }
    return obj;
}

void* shim_swift_unknownObjectRetain(void* obj) {
    return shim_swift_unknownObjectRetain_n(obj, 1);
}

void shim_swift_unknownObjectRelease_n(void* obj, std::uint32_t n) {
    if (!obj || n == 0) return;
    if (is_live_swift_object(obj)) {
        shim_swift_release_n(static_cast<SwiftHeapObject*>(obj), n);
        return;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        finn_objc_release(static_cast<ObjcId>(obj));
    }
}

void shim_swift_unknownObjectRelease(void* obj) {
    shim_swift_unknownObjectRelease_n(obj, 1);
}

// Swift 64-bit BridgeObject tagged pointer handling
constexpr std::uintptr_t kSwiftBridgeImmortalBit = std::uintptr_t{1} << 63;
constexpr std::uintptr_t kSwiftBridgePtrMask = std::uintptr_t{0x0000fffffffffff8ull};

std::uintptr_t shim_swift_bridgeObjectRetain_n(std::uintptr_t bits, std::uint32_t n) {
    if (bits == 0 || n == 0 || (bits & kSwiftBridgeImmortalBit) != 0) return bits;
    void* ptr = reinterpret_cast<void*>(bits & kSwiftBridgePtrMask);
    if (ptr) shim_swift_unknownObjectRetain_n(ptr, n);
    return bits;
}

std::uintptr_t shim_swift_bridgeObjectRetain(std::uintptr_t bits) {
    return shim_swift_bridgeObjectRetain_n(bits, 1);
}

void shim_swift_bridgeObjectRelease_n(std::uintptr_t bits, std::uint32_t n) {
    if (bits == 0 || n == 0 || (bits & kSwiftBridgeImmortalBit) != 0) return;
    void* ptr = reinterpret_cast<void*>(bits & kSwiftBridgePtrMask);
    if (ptr) shim_swift_unknownObjectRelease_n(ptr, n);
}

void shim_swift_bridgeObjectRelease(std::uintptr_t bits) {
    shim_swift_bridgeObjectRelease_n(bits, 1);
}

bool shim_swift_isUniquelyReferenced_nonNull_bridgeObject(std::uintptr_t bits) {
    if (bits == 0 || (bits & kSwiftBridgeImmortalBit) != 0) return false;
    void* ptr = reinterpret_cast<void*>(bits & kSwiftBridgePtrMask);
    return shim_swift_isUniquelyReferenced_nonNull_native(static_cast<SwiftHeapObject*>(ptr));
}

void shim_swift_beginAccess(void* pointer, void* buffer, std::uintptr_t flags, void* pc) {
    if (!buffer) return;
    auto* rec = static_cast<SwiftAccessBuffer*>(buffer);
    rec->pointer = pointer;
    rec->flags = flags;
    rec->pc = pc;
}

void shim_swift_endAccess(void* buffer) {
    if (!buffer) return;
    auto* rec = static_cast<SwiftAccessBuffer*>(buffer);
    rec->pointer = nullptr;
    rec->flags = 0;
    rec->pc = nullptr;
}

void shim_swift_once(std::intptr_t* predicate, void (*fn)(void*), void* context) {
    if (!predicate || !fn) return;
    auto* pred = reinterpret_cast<std::atomic<std::intptr_t>*>(predicate);
    if (pred->load(std::memory_order_acquire) == -1) return;

    std::intptr_t expected = 0;
    if (pred->compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        fn(context);
        pred->store(-1, std::memory_order_release);
        return;
    }
    while (pred->load(std::memory_order_acquire) != -1) {
        std::this_thread::yield();
    }
}

void* shim_swift_getInitializedObjCClass(void* c) {
    return c;
}

}  // namespace

std::size_t swift_live_object_count() {
    auto& st = swift_rt();
    std::lock_guard<std::mutex> lk(st.mu);
    return st.live_objects.size();
}

void register_swift_runtime_symbols(std::unordered_map<std::string, void*>* table) {
    (*table)["_swift_allocObject"] = to_ptr(&shim_swift_allocObject);
    (*table)["_swift_deallocObject"] = to_ptr(&shim_swift_deallocObject);
    (*table)["_swift_deallocClassInstance"] = to_ptr(&shim_swift_deallocClassInstance);
    (*table)["_swift_retain"] = to_ptr(&shim_swift_retain);
    (*table)["_swift_release"] = to_ptr(&shim_swift_release);
    (*table)["_swift_retain_n"] = to_ptr(&shim_swift_retain_n);
    (*table)["_swift_release_n"] = to_ptr(&shim_swift_release_n);
    (*table)["_swift_retainCount"] = to_ptr(&shim_swift_retainCount);
    (*table)["_swift_bridgeObjectRetain"] = to_ptr(&shim_swift_bridgeObjectRetain);
    (*table)["_swift_bridgeObjectRelease"] = to_ptr(&shim_swift_bridgeObjectRelease);
    (*table)["_swift_bridgeObjectRetain_n"] = to_ptr(&shim_swift_bridgeObjectRetain_n);
    (*table)["_swift_bridgeObjectRelease_n"] = to_ptr(&shim_swift_bridgeObjectRelease_n);
    (*table)["_swift_unknownObjectRetain"] = to_ptr(&shim_swift_unknownObjectRetain);
    (*table)["_swift_unknownObjectRelease"] = to_ptr(&shim_swift_unknownObjectRelease);
    (*table)["_swift_unknownObjectRetain_n"] = to_ptr(&shim_swift_unknownObjectRetain_n);
    (*table)["_swift_unknownObjectRelease_n"] = to_ptr(&shim_swift_unknownObjectRelease_n);
    (*table)["_swift_beginAccess"] = to_ptr(&shim_swift_beginAccess);
    (*table)["_swift_endAccess"] = to_ptr(&shim_swift_endAccess);
    (*table)["_swift_once"] = to_ptr(&shim_swift_once);
    (*table)["_swift_slowAlloc"] = to_ptr(&shim_swift_slowAlloc);
    (*table)["_swift_slowDealloc"] = to_ptr(&shim_swift_slowDealloc);
    (*table)["_swift_isUniquelyReferenced_nonNull_native"] =
        to_ptr(&shim_swift_isUniquelyReferenced_nonNull_native);
    (*table)["_swift_isUniquelyReferenced_nonNull_bridgeObject"] =
        to_ptr(&shim_swift_isUniquelyReferenced_nonNull_bridgeObject);
    (*table)["_swift_getInitializedObjCClass"] = to_ptr(&shim_swift_getInitializedObjCClass);
}

}  // namespace finn::darwin
