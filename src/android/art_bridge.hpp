// ART-style DEX analysis bridge. Builds class/method tables from a parsed DEX,
// decodes method bytecode, reports the framework surface a package actually
// needs, and honestly marks what finn-android does not bridge.
//
// A tiny strict-subset interpreter executes only the opcodes it implements;
// anything else returns "unsupported" rather than an invented result.

#ifndef FINN_ANDROID_ART_BRIDGE_HPP
#define FINN_ANDROID_ART_BRIDGE_HPP

#include "android/android_format.hpp"
#include "android/dex_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace finn::android {

struct ArtMethodAnalysis {
    bool ok = false;
    std::string class_name;
    std::string method_name;
    std::size_t instruction_count = 0;
    std::size_t supported_count = 0;
    std::vector<std::string> invoke_targets;
    std::vector<std::string> string_constants;
    std::vector<std::uint32_t> branch_targets;
    std::vector<std::pair<std::uint32_t, std::string>> unsupported;
    std::string error;
};

struct ArtAuditReport {
    bool valid = false;
    std::size_t class_count = 0;
    std::size_t method_count = 0;
    std::size_t code_method_count = 0;
    std::size_t native_method_count = 0;
    std::size_t fully_decoded_methods = 0;
    std::size_t partial_methods = 0;
    std::vector<std::string> bridged_framework_classes;
    std::vector<std::string> unbridged_framework_classes;
    std::vector<std::string> unsupported_opcodes;
    std::vector<std::string> reflection_calls;
    std::vector<std::string> jni_methods;
    std::string summary;
    bool analyzable = false;
};

struct LifecycleMethodInfo {
    std::string name;
    bool present = false;
    bool has_code = false;
    bool decodable = false;
    std::uint16_t registers_size = 0;
    std::string reason;
};

struct ArtLifecyclePlan {
    std::string activity_name;
    std::vector<LifecycleMethodInfo> methods;
    std::string summary;
};

struct ArtExecutionResult {
    bool ok = false;
    bool supported = true;
    std::int32_t return_value = 0;
    std::size_t instructions_executed = 0;
    std::string error;
};

class ArtBridge {
public:
    bool load(DexFile dex, std::string* error = nullptr);
    const DexFile& dex() const { return dex_; }

    ArtMethodAnalysis analyze(const DexMethod& method) const;
    ArtMethodAnalysis analyze_by_name(std::string_view class_name,
                                      std::string_view method_name) const;
    ArtAuditReport audit() const;
    ArtLifecyclePlan lifecycle_plan(const ApkManifest& manifest) const;

    // Executes a method made only of the documented strict subset. Any other
    // opcode fails with supported=false and the offending offset in error.
    ArtExecutionResult execute_simple(const DexMethod& method,
                                      const std::vector<std::int32_t>& args,
                                      std::size_t registers,
                                      std::string* error = nullptr) const;

private:
    const DexEncodedMethod* encoded_method(std::uint32_t method_idx) const;

    DexFile dex_;
};

}  // namespace finn::android

#endif  // FINN_ANDROID_ART_BRIDGE_HPP
