// Clean-room ArkCompiler Panda Bytecode (.abc — "PANDA\0\0\0") binary parser,
// register+accumulator bytecode VM, OpenHarmony 6.1 LTS (API 23) UIAbility &
// system module bridge, and NAPI (napi_*) host C ABI bridge.

#ifndef FINN_OHOS_ABC_RUNTIME_HPP
#define FINN_OHOS_ABC_RUNTIME_HPP

#include "ohos/arkui_bridge.hpp"
#include "ohos/ohos_format.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Clean-room OpenHarmony / Node-API (NAPI) C ABI declarations for native .so modules.
extern "C" {

typedef struct finn_napi_env__* napi_env;
typedef struct finn_napi_value__* napi_value;
typedef struct finn_napi_callback_info__* napi_callback_info;

typedef enum {
    napi_ok = 0,
    napi_invalid_arg = 1,
    napi_object_expected = 2,
    napi_string_expected = 3,
    napi_name_expected = 4,
    napi_function_expected = 5,
    napi_number_expected = 6,
    napi_boolean_expected = 7,
    napi_generic_failure = 9,
} napi_status;

typedef enum {
    napi_undefined,
    napi_null,
    napi_boolean,
    napi_number,
    napi_string,
    napi_symbol,
    napi_object,
    napi_function,
    napi_external,
    napi_bigint,
} napi_valuetype;

typedef napi_value (*napi_callback)(napi_env env, napi_callback_info info);

typedef struct {
    const char* utf8name;
    napi_value name;
    napi_callback method;
    napi_callback getter;
    napi_callback setter;
    napi_value value;
    std::uint32_t attributes;
    void* data;
} napi_property_descriptor;

typedef napi_value (*napi_addon_register_func)(napi_env env, napi_value exports);

typedef struct {
    int nm_version;
    unsigned int nm_flags;
    const char* nm_filename;
    napi_addon_register_func nm_register_func;
    const char* nm_modname;
    void* nm_priv;
    void* reserved[4];
} napi_module;

#ifndef NAPI_AUTO_LENGTH
#define NAPI_AUTO_LENGTH (static_cast<std::size_t>(-1))
#endif

void napi_module_register(napi_module* mod);
napi_status napi_define_properties(napi_env env, napi_value object,
                                   std::size_t property_count,
                                   const napi_property_descriptor* properties);
napi_status napi_create_string_utf8(napi_env env, const char* str,
                                    std::size_t length, napi_value* result);
napi_status napi_create_int32(napi_env env, std::int32_t value,
                              napi_value* result);
napi_status napi_create_double(napi_env env, double value, napi_value* result);
napi_status napi_create_object(napi_env env, napi_value* result);
napi_status napi_set_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value value);
napi_status napi_get_named_property(napi_env env, napi_value object,
                                    const char* utf8name, napi_value* result);
napi_status napi_get_cb_info(napi_env env, napi_callback_info cbinfo,
                             std::size_t* argc, napi_value* argv,
                             napi_value* this_arg, void** data);
napi_status napi_get_value_int32(napi_env env, napi_value value,
                                 std::int32_t* result);
napi_status napi_get_value_double(napi_env env, napi_value value,
                                  double* result);
napi_status napi_get_value_string_utf8(napi_env env, napi_value value,
                                       char* buf, std::size_t bufsize,
                                       std::size_t* result);

// Clean-room subset of the Node-API value / array / type surface.
napi_status napi_get_undefined(napi_env env, napi_value* result);
napi_status napi_get_null(napi_env env, napi_value* result);
napi_status napi_get_boolean(napi_env env, bool value, napi_value* result);
napi_status napi_typeof(napi_env env, napi_value value, napi_valuetype* result);
napi_status napi_create_int64(napi_env env, std::int64_t value, napi_value* result);
napi_status napi_get_value_int64(napi_env env, napi_value value,
                                 std::int64_t* result);
napi_status napi_create_uint32(napi_env env, std::uint32_t value, napi_value* result);
napi_status napi_get_value_bool(napi_env env, napi_value value, bool* result);
napi_status napi_create_array(napi_env env, napi_value* result);
napi_status napi_create_array_with_length(napi_env env, std::size_t length,
                                          napi_value* result);
napi_status napi_is_array(napi_env env, napi_value value, bool* result);
napi_status napi_get_array_length(napi_env env, napi_value value,
                                  std::uint32_t* result);
napi_status napi_set_element(napi_env env, napi_value object,
                             std::uint32_t index, napi_value value);
napi_status napi_get_element(napi_env env, napi_value object,
                             std::uint32_t index, napi_value* result);
napi_status napi_throw_error(napi_env env, const char* code, const char* message);

}  // extern "C"

namespace finn::ohos {

struct AbcHeader {
    bool valid = false;
    std::uint32_t checksum = 0;
    bool checksum_valid = false;
    std::uint8_t version[4] = {};
    std::string version_string;
    std::uint32_t file_size = 0;
    std::uint32_t foreign_off = 0;
    std::uint32_t foreign_size = 0;
    std::uint32_t num_classes = 0;
    std::uint32_t class_idx_off = 0;
    std::uint32_t num_lnps = 0;
    std::uint32_t lnp_idx_off = 0;
    std::uint32_t num_literalarrays = 0;
    std::uint32_t literalarray_idx_off = 0;
    std::uint32_t num_indexes = 0;
    std::uint32_t index_section_off = 0;
};

struct AbcMethod {
    std::string class_name;
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t access_flags = 0;
    std::uint32_t code_offset = 0;
    std::uint32_t num_vregs = 0;
    std::uint32_t num_args = 0;
    std::vector<std::uint8_t> bytecode;
};

struct AbcClass {
    std::string name;
    std::uint32_t offset = 0;
    bool is_foreign = false;
    std::uint32_t access_flags = 0;
    std::string source_file;
    std::vector<std::string> field_names;
    std::vector<AbcMethod> methods;
};

// One regular import entry from a Panda module record. External module
// variables are indexed by the entry's position in this list.
struct AbcModuleImport {
    std::string module_request;   // e.g. "@ohos:file.fs", "@bundle:.../Log"
    std::string import_name;      // exported name in the source module
    std::string local_name;       // binding name in this module
};

struct AbcModuleRecord {
    std::vector<AbcModuleImport> imports;
};

struct AbcIndexRegion {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::vector<std::uint32_t> class_idx;
    std::vector<std::uint32_t> method_idx;
    std::vector<std::uint32_t> field_idx;
    std::vector<std::uint32_t> proto_idx;
};

struct AbcFile {
    bool valid = false;
    AbcHeader header;
    std::vector<AbcClass> classes;
    std::vector<AbcIndexRegion> index_regions;
    std::vector<AbcModuleRecord> module_records;
    std::vector<std::string> strings;
    std::vector<std::uint8_t> raw_bytes;
    std::string error;

    std::string read_mutf8_string(std::uint32_t offset) const;
    std::string resolve_id16_string(std::uint32_t method_offset,
                                    std::uint16_t id16) const;
    const AbcMethod* find_method(std::string_view name) const;
    const AbcMethod* find_class_method(std::string_view class_name,
                                       std::string_view method_name) const;
};

struct KitAudit {
    std::vector<std::string> supported;
    // Known hardware / proprietary kits that finn-ohos deliberately does not
    // bridge. These make a package non-runnable.
    std::vector<std::string> unsupported;
    // Import specifiers that are neither recognised as supported nor known to
    // be hardware kits. Reported as unknown, never claimed to work.
    std::vector<std::string> unknown;
};

std::uint32_t compute_panda_adler32(const std::uint8_t* data, std::size_t size);
AbcFile parse_abc_bytes(const std::uint8_t* data, std::size_t size);
AbcFile parse_abc_file(const std::string& path);
KitAudit audit_ohos_kits(const std::vector<std::string>& strings);

class AbcRuntime;
struct AbcObject;

struct AbcValue {
    enum class Kind {
        Undefined,
        Null,
        Bool,
        Int,
        Double,
        String,
        Object,
        NativeFunction,
        AbcFunction,
    };

    using NativeFn = std::function<AbcValue(AbcRuntime&, const AbcValue&,
                                            const std::vector<AbcValue>&)>;

    Kind kind = Kind::Undefined;
    bool bool_val = false;
    std::int64_t int_val = 0;
    double double_val = 0.0;
    std::string str_val;
    std::shared_ptr<AbcObject> obj_val;
    NativeFn native_fn;
    const AbcMethod* abc_method = nullptr;

    static AbcValue make_undefined();
    static AbcValue make_null();
    static AbcValue make_bool(bool b);
    static AbcValue make_int(std::int64_t v);
    static AbcValue make_double(double v);
    static AbcValue make_string(std::string s);
    static AbcValue make_object(std::string class_name = "Object");
    static AbcValue make_array();
    static AbcValue make_native(NativeFn fn);
    static AbcValue make_abc_method(const AbcMethod* method);

    bool is_truthy() const;
    double to_number() const;
    std::int32_t to_int32() const;
    std::int64_t to_int64() const;
    std::string to_string() const;
    bool equals(const AbcValue& other) const;
};

struct AbcObject {
    std::string class_name;
    std::unordered_map<std::string, AbcValue> properties;
    // Dense storage for Array values created by Panda CreateEmptyArray or the
    // NAPI array functions. Empty for ordinary objects.
    std::vector<AbcValue> array_val;
    // Used for unresolved ES-import placeholders: a missing property returns a
    // function that logs the skipped call instead of failing silently.
    std::function<AbcValue(const std::string&)> property_fallback;
};

struct AbcExecResult {
    bool ok = false;
    AbcValue return_value;
    std::size_t instructions_executed = 0;
    std::string error;
};

struct AbilityRunReport {
    bool ok = false;
    std::string bundle_name;
    std::string ability_name;
    std::int64_t startup_time_ms = 0;
    std::string loaded_page;
    std::vector<std::string> lifecycle_phases;
    std::vector<std::string> logs;
    std::string error;
};

// Real values exposed by the @ohos.bundle.bundleManager shim. No value is
// invented: unset fields stay empty/zero rather than receiving a fake default.
struct OhosBundleInfo {
    std::string name;
    std::string version_name;
    std::uint32_t version_code = 0;
    std::string module_name;
    std::string main_element;
    int api_version = 0;
};

class AbcRuntime {
public:
    AbcRuntime();
    ~AbcRuntime();

    AbcRuntime(const AbcRuntime&) = delete;
    AbcRuntime& operator=(const AbcRuntime&) = delete;

    void set_bundle_context(const std::string& bundle_name,
                            const std::string& app_root_dir,
                            const std::string& ohos_root_dir = {});
    // Supplies the manifest facts returned by @ohos.bundle.bundleManager.
    // Called with the real parsed module.json before the ability lifecycle runs.
    void set_bundle_info(OhosBundleInfo info);
    const OhosBundleInfo& bundle_info() const { return bundle_info_; }
    void set_arkui(ArkUiEngine* engine);
    ArkUiEngine* arkui() const { return arkui_; }

    bool load_abc(AbcFile file, std::string* error = nullptr);
    const AbcFile& abc() const { return abc_; }

    void set_global(const std::string& name, AbcValue value);
    AbcValue get_global(const std::string& name) const;

    // Resolved ES module import bindings used by ldexternalmodulevar /
    // ldlocalmodulevar. Index is the Panda module-record import entry index.
    void set_external_module_var(std::size_t index, AbcValue value);
    const std::vector<AbcValue>& external_module_vars() const {
        return external_module_vars_;
    }
    // Placeholder for an import slot finn-ohos cannot link; method calls are
    // logged as skipped instead of being silently treated as successful.
    AbcValue unresolved_module_binding(std::size_t index) const;

    AbcExecResult execute_method(const AbcMethod& method,
                                 const AbcValue& this_val = AbcValue::make_undefined(),
                                 const std::vector<AbcValue>& args = {});

    AbcExecResult call_method_by_name(const std::string& method_name,
                                      const AbcValue& this_val = AbcValue::make_undefined(),
                                      const std::vector<AbcValue>& args = {});

    // Executes the OpenHarmony 6.1 LTS UIAbility lifecycle:
    // onCreate(want, launchParam) -> onWindowStageCreate(windowStage) ->
    // onForeground() -> [optional UI loop] -> onBackground() -> onDestroy().
    AbilityRunReport run_ability_lifecycle(const OhosModuleManifest& manifest,
                                           int argc, char** argv,
                                           bool headless = true,
                                           bool auto_close = true);

    // Loads a host-compatible native NAPI .so module via dlopen.
    bool load_native_module(const std::string& so_path, std::string* error = nullptr);
    AbcValue get_native_module(const std::string& modname) const;

    // Exposes the NAPI host environment handle bound to this runtime.
    napi_env napi_env_handle();
    AbcValue unwrap_napi_value(napi_value v) const;
    napi_value wrap_napi_value(AbcValue v);
    // Message recorded by napi_throw_error; empty when nothing was thrown.
    std::string last_napi_error() const;

    void log(const std::string& line);
    const std::vector<std::string>& logs() const { return logs_; }
    const std::vector<std::string>& router_stack() const { return router_stack_; }

private:
    void install_builtin_modules();
    bool load_declarative_page(const std::string& page_path);
    std::string prefs_file_path(const std::string& pref_name) const;
    std::string sandbox_data_dir() const;

    AbcFile abc_;
    ArkUiEngine* arkui_ = nullptr;
    std::string bundle_name_ = "com.finnos.app";
    OhosBundleInfo bundle_info_;
    std::string app_root_dir_;
    std::string ohos_root_dir_;
    std::string last_loaded_page_;
    std::unordered_map<std::string, AbcValue> globals_;
    std::unordered_map<std::string, AbcValue> modules_;
    std::vector<AbcValue> external_module_vars_;
    std::vector<AbcValue> lexical_slots_;
    std::vector<std::string> router_stack_;
    std::vector<std::string> logs_;
    std::vector<void*> dl_handles_;
    int call_depth_ = 0;

    std::unique_ptr<finn_napi_env__> napi_env_;
};

}  // namespace finn::ohos

#endif
