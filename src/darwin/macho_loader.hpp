// Clean-room 64-bit Mach-O dynamic loader, pre-flight dependency analyzer,
// and native execution engine for FinnOS finn-darwin.
// Supports Universal/Fat binaries, LC_SEGMENT_64, LC_MAIN, LC_DYLD_CHAINED_FIXUPS,
// LC_DYLD_INFO_ONLY, bundled dylibs (@executable_path/@loader_path/@rpath),
// and Objective-C ABI v2 section realization.

#ifndef FINN_DARWIN_MACHO_LOADER_HPP
#define FINN_DARWIN_MACHO_LOADER_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace finn::darwin {

struct MachoDiagnosis {
    bool valid = false;
    bool arch_compatible = false;
    bool runnable = false;
    std::string host_arch;
    std::vector<std::string> binary_arches;
    std::string selected_arch;
    std::vector<std::string> linked_dylibs;
    std::vector<std::string> imported_symbols;
    std::vector<std::string> missing_symbols;
    std::vector<std::string> missing_frameworks;
    std::size_t objc_class_count = 0;
    std::string summary;
    std::string error;
};

struct LoadedMachoImage {
    std::string path;
    std::uint8_t* map_base = nullptr;
    std::size_t map_size = 0;
    std::uint64_t preferred_base = 0;
    std::intptr_t slide = 0;
    std::uint64_t entry_offset = 0;
    std::vector<std::uint64_t> mod_init_addrs;
    std::unordered_map<std::string, void*> exports;
    bool mapped_segments_separately = false;
};

class MachoLoader {
public:
    MachoLoader();
    ~MachoLoader();

    MachoLoader(const MachoLoader&) = delete;
    MachoLoader& operator=(const MachoLoader&) = delete;

    // Initializes libSystem, libobjc, Foundation, and AppKit symbol tables.
    void initialize_runtime(int argc, char** argv, const std::string& target_path);

    // Pre-flight analysis without executing the binary. Honestly reports every
    // missing framework or symbol required by the Mach-O executable or .app bundle.
    MachoDiagnosis diagnose(const std::string& binary_or_app_path);

    // Resolves a .app bundle directory to its Contents/MacOS/<CFBundleExecutable> binary.
    static std::string resolve_target_binary(const std::string& path,
                                             std::string* bundle_root_out = nullptr,
                                             std::string* error = nullptr);

    // Loads the Mach-O binary (and any bundled dylibs), applies fixups/bindings,
    // realizes Objective-C sections, runs __mod_init_func constructors, and executes main().
    int load_and_run(const std::string& binary_or_app_path,
                     int argc, char** argv,
                     std::string* error = nullptr,
                     bool allow_missing_symbols = false);

private:
    void* lookup_symbol(const std::string& name, bool allow_stub,
                        std::vector<std::string>* missing_out = nullptr);
    bool load_image(const std::string& path, bool is_main,
                    bool allow_missing_symbols,
                    std::string* error,
                    LoadedMachoImage** out_img);
    void unmap_all();

    bool runtime_initialized_ = false;
    std::unordered_map<std::string, void*> symbols_;
    std::vector<LoadedMachoImage*> images_;
    std::string main_exec_dir_;
    std::vector<std::string> rpaths_;
};

}  // namespace finn::darwin

#endif
