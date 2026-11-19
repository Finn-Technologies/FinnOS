#include "android/art_bridge.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace finn::android {
namespace {

constexpr std::uint32_t kAccNative = 0x00100u;

// Classes whose surface the finn-android lifecycle and widget bridges model.
// This is an analysis vocabulary, not a claim that the Android framework runs.
const std::set<std::string>& modelled_framework_classes() {
    static const std::set<std::string> kClasses = {
        "Landroid/app/Activity;",
        "Landroid/app/Application;",
        "Landroid/content/Context;",
        "Landroid/content/Intent;",
        "Landroid/os/Bundle;",
        "Landroid/util/Log;",
        "Landroid/view/View;",
        "Landroid/view/ViewGroup;",
        "Landroid/widget/Button;",
        "Landroid/widget/CheckBox;",
        "Landroid/widget/EditText;",
        "Landroid/widget/FrameLayout;",
        "Landroid/widget/ImageView;",
        "Landroid/widget/LinearLayout;",
        "Landroid/widget/ProgressBar;",
        "Landroid/widget/RelativeLayout;",
        "Landroid/widget/ScrollView;",
        "Landroid/widget/SeekBar;",
        "Landroid/widget/Switch;",
        "Landroid/widget/TextView;",
        "Ljava/lang/Math;",
        "Ljava/lang/Object;",
        "Ljava/lang/Runnable;",
        "Ljava/lang/String;",
        "Ljava/lang/System;",
        "Ljava/lang/Thread;",
    };
    return kClasses;
}

bool looks_like_class(std::string_view s) {
    return s.size() > 2u && s.front() == 'L' && s.back() == ';';
}

std::string normalise_activity(std::string_view name, std::string_view package) {
    if (name.empty()) return {};
    std::string out(name);
    if (out.front() == '.') out = std::string(package) + out;
    if (looks_like_class(out)) out = out.substr(1, out.size() - 2);
    return out;
}

std::vector<std::string> sorted_unique(const std::set<std::string>& in) {
    return std::vector<std::string>(in.begin(), in.end());
}

}  // namespace

bool ArtBridge::load(DexFile dex, std::string* error) {
    if (!dex.valid) {
        if (error) *error = dex.error.empty() ? "invalid DEX file" : dex.error;
        return false;
    }
    dex_ = std::move(dex);
    return true;
}

const DexEncodedMethod* ArtBridge::encoded_method(std::uint32_t method_idx) const {
    for (const auto& cls : dex_.classes) {
        for (const auto& m : cls.direct_methods) {
            if (m.method_idx == method_idx) return &m;
        }
        for (const auto& m : cls.virtual_methods) {
            if (m.method_idx == method_idx) return &m;
        }
    }
    return nullptr;
}

ArtMethodAnalysis ArtBridge::analyze(const DexMethod& method) const {
    ArtMethodAnalysis out;
    out.class_name = method.class_name;
    out.method_name = method.name;
    const std::uint32_t idx = static_cast<std::uint32_t>(&method - dex_.methods.data());
    const DexEncodedMethod* em = encoded_method(idx);
    if (!em) {
        out.error = "no class_data entry for this method";
        return out;
    }
    if (!em->has_code) {
        out.error = "method is native or abstract and has no bytecode";
        return out;
    }
    const DexDisassembly dis = disassemble_dex_code(em->code);
    out.instruction_count = dis.instructions.size();
    out.supported_count = dis.instructions.size();
    out.branch_targets = dis.branch_targets;
    out.unsupported = dis.unsupported;
    std::set<std::string> invokes;
    std::set<std::string> strings;
    for (const auto& ins : dis.instructions) {
        if (ins.has_index &&
            (ins.mnemonic.rfind("invoke", 0) == 0 ||
             ins.opcode == 0xfeu || ins.opcode == 0xffu)) {
            if (ins.index < dex_.methods.size()) {
                invokes.insert(dex_.methods[ins.index].full_name());
            }
        }
        if ((ins.opcode == 0x1au || ins.opcode == 0x1bu) && ins.has_index) {
            const std::string s = dex_.string_at(ins.index);
            if (!s.empty()) strings.insert(s);
        }
    }
    out.invoke_targets = sorted_unique(invokes);
    out.string_constants = sorted_unique(strings);
    out.ok = dis.complete;
    if (!dis.error.empty()) out.error = dis.error;
    return out;
}

ArtMethodAnalysis ArtBridge::analyze_by_name(std::string_view class_name,
                                             std::string_view method_name) const {
    const DexMethod* m = dex_.find_method(class_name, method_name);
    if (!m) {
        ArtMethodAnalysis out;
        out.class_name = class_name;
        out.method_name = method_name;
        out.error = "method not present in DEX";
        return out;
    }
    return analyze(*m);
}

ArtAuditReport ArtBridge::audit() const {
    ArtAuditReport out;
    out.valid = dex_.valid;
    if (!dex_.valid) {
        out.summary = dex_.error.empty() ? "DEX is not valid" : dex_.error;
        return out;
    }
    out.class_count = dex_.classes.size();
    out.method_count = dex_.methods.size();
    const auto& modelled = modelled_framework_classes();
    std::set<std::string> bridged;
    std::set<std::string> unbridged;
    std::set<std::string> unsupported;
    std::set<std::string> reflection;
    std::set<std::string> native_methods;

    for (const auto& cls : dex_.classes) {
        const std::vector<const DexEncodedMethod*> methods = [&] {
            std::vector<const DexEncodedMethod*> v;
            for (const auto& m : cls.direct_methods) v.push_back(&m);
            for (const auto& m : cls.virtual_methods) v.push_back(&m);
            return v;
        }();
        for (const auto* em : methods) {
            if ((em->access_flags & kAccNative) != 0u) {
                native_methods.insert(em->class_name + "->" + em->name);
            }
            if (!em->has_code) continue;
            ++out.code_method_count;
            const auto a = analyze(dex_.methods[em->method_idx]);
            if (a.ok) {
                ++out.fully_decoded_methods;
            } else {
                ++out.partial_methods;
                for (const auto& u : a.unsupported) unsupported.insert(u.second);
            }
            for (const auto& target : a.invoke_targets) {
                const std::size_t arrow = target.find("->");
                if (arrow == std::string::npos) continue;
                const std::string cls_name = target.substr(0, arrow);
                if (!looks_like_class(cls_name)) continue;
                if (modelled.count(cls_name) != 0u) bridged.insert(cls_name);
                else unbridged.insert(cls_name);
            }
        }
    }

    for (const auto& cls : dex_.classes) {
        if (looks_like_class(cls.superclass)) {
            if (modelled.count(cls.superclass) != 0u) bridged.insert(cls.superclass);
            else unbridged.insert(cls.superclass);
        }
    }
    for (const auto& s : dex_.strings) {
        if (s.find("java/lang/reflect") != std::string::npos ||
            s.find("java.lang.reflect") != std::string::npos ||
            s == "forName" || s == "getDeclaredMethod" || s == "loadClass" ||
            s == "invoke") {
            reflection.insert(s);
        }
    }

    out.native_method_count = native_methods.size();
    out.bridged_framework_classes = sorted_unique(bridged);
    out.unbridged_framework_classes = sorted_unique(unbridged);
    out.unsupported_opcodes = sorted_unique(unsupported);
    out.reflection_calls = sorted_unique(reflection);
    out.jni_methods = sorted_unique(native_methods);

    std::ostringstream ss;
    ss << "DEX v" << dex_.verification.header.magic.substr(4) << ": "
       << out.class_count << " class(es), " << out.method_count << " method(s), "
       << out.code_method_count << " with bytecode; " << out.fully_decoded_methods
       << " fully decoded, " << out.partial_methods << " partial";
    if (!unbridged.empty()) {
        ss << "; " << unbridged.size() << " framework class(es) outside the modelled surface";
    }
    if (!native_methods.empty()) {
        ss << "; " << native_methods.size() << " native method(s) cannot run without JNI";
    }
    if (!reflection.empty()) {
        ss << "; reflection is present and is not bridged";
    }
    out.summary = ss.str();
    out.analyzable = out.code_method_count > 0u && out.fully_decoded_methods > 0u;
    return out;
}

ArtLifecyclePlan ArtBridge::lifecycle_plan(const ApkManifest& manifest) const {
    ArtLifecyclePlan plan;
    plan.activity_name = normalise_activity(manifest.main_activity(), manifest.package_name);
    static constexpr const char* kPhases[] = {"onCreate", "onStart", "onResume",
                                              "onPause", "onStop", "onDestroy"};
    for (const char* phase : kPhases) {
        LifecycleMethodInfo info;
        info.name = phase;
        const DexMethod* m = nullptr;
        if (!plan.activity_name.empty()) {
            m = dex_.find_method(plan.activity_name, phase);
        }
        if (!m && !manifest.application_name.empty()) {
            const std::string app = normalise_activity(manifest.application_name,
                                                       manifest.package_name);
            m = dex_.find_method(app, phase);
        }
        if (m) {
            info.present = true;
            const auto a = analyze(*m);
            // The encoded-method record is the source of truth for code presence.
            const auto idx = static_cast<std::uint32_t>(m - dex_.methods.data());
            const DexEncodedMethod* em = encoded_method(idx);
            info.has_code = em != nullptr && em->has_code;
            info.registers_size = em ? em->code.registers_size : 0u;
            info.decodable = a.ok;
            info.reason = a.ok ? std::string() : a.error;
        } else {
            info.reason = "method not found in DEX";
        }
        plan.methods.push_back(std::move(info));
    }
    std::size_t n = 0;
    for (const auto& m : plan.methods) if (m.present) ++n;
    plan.summary = plan.activity_name.empty()
                       ? "no launcher activity declared in the manifest"
                       : ("activity " + plan.activity_name + ": " + std::to_string(n) +
                          " of 6 lifecycle methods found in DEX");
    return plan;
}

ArtExecutionResult ArtBridge::execute_simple(const DexMethod& method,
                                             const std::vector<std::int32_t>& args,
                                             std::size_t registers,
                                             std::string* error) const {
    ArtExecutionResult out;
    const auto fail = [&](const std::string& msg) {
        // A failed execution is never a supported execution: the caller must
        // not mistake "returned an error" for "ran the real bytecode".
        out.supported = false;
        out.error = msg;
        if (error) *error = msg;
        return out;
    };
    const std::uint32_t idx = static_cast<std::uint32_t>(&method - dex_.methods.data());
    const DexEncodedMethod* em = encoded_method(idx);
    if (!em || !em->has_code) return fail("method has no bytecode");
    if (registers == 0u || registers > 1024u) return fail("invalid register count");
    const DexDisassembly dis = disassemble_dex_code(em->code);
    if (!dis.complete) return fail(dis.error);

    std::vector<std::int32_t> regs(registers, 0);
    if (args.size() > em->code.ins_size) return fail("too many arguments for method");
    const std::size_t first = registers - em->code.ins_size;
    for (std::size_t i = 0; i < args.size(); ++i) regs[first + i] = args[i];

    std::map<std::uint32_t, std::size_t> by_offset;
    for (std::size_t i = 0; i < dis.instructions.size(); ++i) {
        by_offset[dis.instructions[i].offset] = i;
    }
    std::int32_t last_result = 0;
    std::uint32_t pc = 0;
    constexpr std::size_t kMaxSteps = 100000u;
    while (pc < em->code.insns_size && out.instructions_executed < kMaxSteps) {
        const auto it = by_offset.find(pc);
        if (it == by_offset.end()) return fail("branch into an undecoded code offset");
        const DexInstruction& ins = dis.instructions[it->second];
        const auto reg = [&](std::size_t i) -> std::int32_t& {
            static std::int32_t sink = 0;
            if (i >= ins.registers.size() || ins.registers[i] >= regs.size()) return sink;
            return regs[ins.registers[i]];
        };
        std::uint32_t next = pc + ins.width;
        switch (ins.opcode) {
            case 0x00: break;  // nop
            case 0x12:         // const/4
            case 0x13:         // const/16
            case 0x14:         // const
                reg(0) = static_cast<std::int32_t>(ins.literal);
                break;
            case 0x01:         // move
            case 0x02:
            case 0x03:
                reg(0) = reg(1);
                break;
            case 0x0a:         // move-result
                reg(0) = last_result;
                break;
            case 0x90:         // add-int
                reg(0) = reg(1) + reg(2);
                break;
            case 0x91:         // sub-int
                reg(0) = reg(1) - reg(2);
                break;
            case 0xb0:         // add-int/2addr
                reg(0) = reg(0) + reg(1);
                break;
            case 0xd0:         // add-int/lit8
            case 0xdb:         // add-int/lit16
                reg(0) = reg(1) + static_cast<std::int32_t>(ins.literal);
                break;
            case 0x28:         // goto
            case 0x29: {
                const std::int64_t at = static_cast<std::int64_t>(pc) + ins.target_offset;
                if (at < 0 || at >= static_cast<std::int64_t>(em->code.insns_size)) {
                    return fail("branch target outside the method");
                }
                next = static_cast<std::uint32_t>(at);
                break;
            }
            case 0x38:         // if-eqz
            case 0x39: {        // if-nez
                const bool take = (ins.opcode == 0x38u) ? (reg(0) == 0) : (reg(0) != 0);
                if (take) {
                    const std::int64_t at = static_cast<std::int64_t>(pc) + ins.target_offset;
                    if (at < 0 || at >= static_cast<std::int64_t>(em->code.insns_size)) {
                        return fail("branch target outside the method");
                    }
                    next = static_cast<std::uint32_t>(at);
                }
                break;
            }
            case 0x0e:         // return-void
                out.ok = true;
                out.supported = true;
                return out;
            case 0x0f:         // return
                out.return_value = reg(0);
                out.ok = true;
                out.supported = true;
                return out;
            default:
                out.supported = false;
                return fail("opcode '" + ins.mnemonic +
                            "' is outside the strict interpreter subset at code unit " +
                            std::to_string(pc));
        }
        last_result = out.return_value;
        pc = next;
        ++out.instructions_executed;
    }
    return fail("method completed without a return instruction");
}

}  // namespace finn::android
