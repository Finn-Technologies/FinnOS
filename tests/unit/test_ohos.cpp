#include "compat/ohos_runner.hpp"
#include "ohos/abc_runtime.hpp"
#include "ohos/arkui_bridge.hpp"
#include "ohos/ohos_format.hpp"
#include "ohos/ohos_installer.hpp"
#include "ohos/zip_reader.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,        \
                         __LINE__, #cond);                                     \
            return false;                                                      \
        }                                                                      \
    } while (false)

class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        root_ = fs::temp_directory_path() /
                ("finn-ohos-test-" + tag + "-" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path path(std::string_view rel) const { return root_ / std::string(rel); }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

void append_le16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
}

void append_le64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    append_le32(out, static_cast<std::uint32_t>(v & 0xffffffffu));
    append_le32(out, static_cast<std::uint32_t>(v >> 32));
}

void append_uleb128(std::vector<std::uint8_t>& out, std::uint32_t v) {
    do {
        std::uint8_t b = static_cast<std::uint8_t>(v & 0x7fu);
        v >>= 7;
        if (v != 0) b |= 0x80u;
        out.push_back(b);
    } while (v != 0);
}

std::vector<std::uint8_t> raw_deflate(const std::vector<std::uint8_t>& src) {
    z_stream strm{};
    if (::deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                       Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<std::uint8_t> out(src.size() + 128);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src.data()));
    strm.avail_in = static_cast<uInt>(src.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());
    const int rc = ::deflate(&strm, Z_FINISH);
    ::deflateEnd(&strm);
    if (rc != Z_STREAM_END) return {};
    out.resize(strm.total_out);
    return out;
}

class ZipBuilder {
public:
    void add(const std::string& name, const std::vector<std::uint8_t>& data,
             bool compress = true) {
        entries_.push_back({name, data, compress});
    }
    void add_text(const std::string& name, const std::string& text,
                  bool compress = true) {
        std::vector<std::uint8_t> b(text.begin(), text.end());
        add(name, b, compress);
    }

    std::vector<std::uint8_t> build() const {
        struct CdRec {
            std::string name;
            std::uint16_t method;
            std::uint32_t crc;
            std::uint32_t comp_sz;
            std::uint32_t uncomp_sz;
            std::uint32_t local_off;
        };
        std::vector<std::uint8_t> out;
        std::vector<CdRec> cds;

        for (const auto& e : entries_) {
            const std::uint32_t crc = static_cast<std::uint32_t>(
                ::crc32(0L, e.data.empty() ? Z_NULL : e.data.data(),
                        static_cast<uInt>(e.data.size())));
            std::vector<std::uint8_t> payload = e.data;
            std::uint16_t method = finn::ohos::kZipMethodStore;
            if (e.compress && !e.data.empty()) {
                auto defl = raw_deflate(e.data);
                if (!defl.empty()) {
                    payload = std::move(defl);
                    method = finn::ohos::kZipMethodDeflate;
                }
            }

            const std::uint32_t local_off = static_cast<std::uint32_t>(out.size());
            append_le32(out, finn::ohos::kZipLocalFileSig);
            append_le16(out, 20);  // version needed
            append_le16(out, 0);   // flags
            append_le16(out, method);
            append_le16(out, 0);   // mod time
            append_le16(out, 0);   // mod date
            append_le32(out, crc);
            append_le32(out, static_cast<std::uint32_t>(payload.size()));
            append_le32(out, static_cast<std::uint32_t>(e.data.size()));
            append_le16(out, static_cast<std::uint16_t>(e.name.size()));
            append_le16(out, 0);  // extra len
            out.insert(out.end(), e.name.begin(), e.name.end());
            out.insert(out.end(), payload.begin(), payload.end());

            cds.push_back({e.name, method, crc,
                           static_cast<std::uint32_t>(payload.size()),
                           static_cast<std::uint32_t>(e.data.size()),
                           local_off});
        }

        const std::uint32_t cd_off = static_cast<std::uint32_t>(out.size());
        for (const auto& c : cds) {
            append_le32(out, finn::ohos::kZipCentralDirSig);
            append_le16(out, 20);
            append_le16(out, 20);
            append_le16(out, 0);
            append_le16(out, c.method);
            append_le16(out, 0);
            append_le16(out, 0);
            append_le32(out, c.crc);
            append_le32(out, c.comp_sz);
            append_le32(out, c.uncomp_sz);
            append_le16(out, static_cast<std::uint16_t>(c.name.size()));
            append_le16(out, 0);
            append_le16(out, 0);
            append_le16(out, 0);
            append_le16(out, 0);
            append_le32(out, 0);
            append_le32(out, c.local_off);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const std::uint32_t cd_sz = static_cast<std::uint32_t>(out.size()) - cd_off;

        append_le32(out, finn::ohos::kZipEndCentralDirSig);
        append_le16(out, 0);
        append_le16(out, 0);
        append_le16(out, static_cast<std::uint16_t>(cds.size()));
        append_le16(out, static_cast<std::uint16_t>(cds.size()));
        append_le32(out, cd_sz);
        append_le32(out, cd_off);
        append_le16(out, 0);
        return out;
    }

    bool write_to(const fs::path& p) const {
        const auto bytes = build();
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(out);
    }

private:
    struct Item {
        std::string name;
        std::vector<std::uint8_t> data;
        bool compress;
    };
    std::vector<Item> entries_;
};

// Real OpenHarmony 6.1 LTS Panda .abc binary builder for tests.
class AbcBuilder {
public:
    AbcBuilder() {
        buf_.resize(finn::ohos::kPandaHeaderSize, 0);
        std::memcpy(buf_.data(), finn::ohos::kPandaMagic, 8);
        // OpenHarmony 6.1 LTS ArkCompiler Panda bytecode version 12.0.6.0
        buf_[12] = 12;
        buf_[13] = 0;
        buf_[14] = 6;
        buf_[15] = 0;
    }

    std::uint32_t add_string(const std::string& s) {
        const auto it = str_offsets_.find(s);
        if (it != str_offsets_.end()) return it->second;
        const std::uint32_t off = static_cast<std::uint32_t>(buf_.size());
        const std::uint32_t tag = (static_cast<std::uint32_t>(s.size()) << 1) | 1u;
        append_uleb128(buf_, tag);
        buf_.insert(buf_.end(), s.begin(), s.end());
        buf_.push_back(0);
        str_offsets_[s] = off;
        return off;
    }

    std::uint16_t add_id16_string(const std::string& s) {
        const std::uint32_t off = add_string(s);
        for (std::size_t i = 0; i < id16_table_.size(); ++i) {
            if (id16_table_[i] == off) return static_cast<std::uint16_t>(i);
        }
        const std::uint16_t idx = static_cast<std::uint16_t>(id16_table_.size());
        id16_table_.push_back(off);
        return idx;
    }

    struct MethodSpec {
        std::string name;
        std::uint32_t num_vregs = 8;
        std::uint32_t num_args = 0;
        std::vector<std::uint8_t> bytecode;
    };

    void add_class(const std::string& desc, const std::string& source_file,
                   const std::vector<MethodSpec>& methods) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> method_meta;
        for (const auto& m : methods) {
            const std::uint32_t name_off = add_string(m.name);
            const std::uint32_t code_off = static_cast<std::uint32_t>(buf_.size());
            append_uleb128(buf_, m.num_vregs);
            append_uleb128(buf_, m.num_args);
            append_uleb128(buf_, static_cast<std::uint32_t>(m.bytecode.size()));
            append_uleb128(buf_, 0);  // tries_size
            buf_.insert(buf_.end(), m.bytecode.begin(), m.bytecode.end());
            method_meta.push_back({name_off, code_off});
        }
        const std::uint32_t sf_off = add_string(source_file);

        const std::uint32_t cls_off = static_cast<std::uint32_t>(buf_.size());
        // Class name (inline MUTF-8)
        append_uleb128(buf_, (static_cast<std::uint32_t>(desc.size()) << 1) | 1u);
        buf_.insert(buf_.end(), desc.begin(), desc.end());
        buf_.push_back(0);
        append_le32(buf_, 0);  // super_class_off
        append_uleb128(buf_, 1);  // access_flags
        append_uleb128(buf_, 0);  // num_fields
        append_uleb128(buf_, static_cast<std::uint32_t>(methods.size()));
        // Class tags: SOURCE_FILE + NOTHING
        buf_.push_back(finn::ohos::panda_tag::kClassSourceFile);
        append_le32(buf_, sf_off);
        buf_.push_back(finn::ohos::panda_tag::kNothing);

        // Methods
        for (const auto& [name_off, code_off] : method_meta) {
            append_le16(buf_, 0);  // class_idx
            append_le16(buf_, 0);  // proto_idx
            append_le32(buf_, name_off);
            append_uleb128(buf_, 1);  // access_flags
            buf_.push_back(finn::ohos::panda_tag::kMethodCode);
            append_le32(buf_, code_off);
            buf_.push_back(finn::ohos::panda_tag::kNothing);
        }

        class_offsets_.push_back(cls_off);
    }

    std::vector<std::uint8_t> finish() {
        // Align to 4 bytes
        while (buf_.size() % 4 != 0) buf_.push_back(0);

        const std::uint32_t class_idx_off = static_cast<std::uint32_t>(buf_.size());
        for (std::uint32_t off : class_offsets_) append_le32(buf_, off);

        const std::uint32_t method_idx_off = static_cast<std::uint32_t>(buf_.size());
        for (std::uint32_t off : id16_table_) append_le32(buf_, off);

        const std::uint32_t index_sec_off = static_cast<std::uint32_t>(buf_.size());
        // One IndexHeader covering the entire file
        append_le32(buf_, 0);
        append_le32(buf_, 0x7fffffffu);
        append_le32(buf_, 0);
        append_le32(buf_, 0);
        append_le32(buf_, static_cast<std::uint32_t>(id16_table_.size()));
        append_le32(buf_, method_idx_off);
        append_le32(buf_, 0);
        append_le32(buf_, 0);
        append_le32(buf_, 0);
        append_le32(buf_, 0);

        const std::uint32_t file_size = static_cast<std::uint32_t>(buf_.size());
        auto patch32 = [&](std::size_t off, std::uint32_t v) {
            buf_[off + 0] = static_cast<std::uint8_t>(v & 0xffu);
            buf_[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
            buf_[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
            buf_[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
        };
        patch32(16, file_size);
        patch32(28, static_cast<std::uint32_t>(class_offsets_.size()));
        patch32(32, class_idx_off);
        patch32(52, 1);  // num_indexes
        patch32(56, index_sec_off);

        const std::uint32_t adler =
            finn::ohos::compute_panda_adler32(buf_.data(), buf_.size());
        patch32(8, adler);
        return buf_;
    }

private:
    std::vector<std::uint8_t> buf_;
    std::map<std::string, std::uint32_t> str_offsets_;
    std::vector<std::uint32_t> id16_table_;
    std::vector<std::uint32_t> class_offsets_;
};

// Bytecode emission helpers matching AbcOpcode
void emit_op(std::vector<std::uint8_t>& c, finn::ohos::AbcOpcode op) {
    c.push_back(static_cast<std::uint8_t>(op));
}
void emit_lda(std::vector<std::uint8_t>& c, std::uint8_t r) {
    emit_op(c, finn::ohos::AbcOpcode::Lda);
    c.push_back(r);
}
void emit_sta(std::vector<std::uint8_t>& c, std::uint8_t r) {
    emit_op(c, finn::ohos::AbcOpcode::Sta);
    c.push_back(r);
}
void emit_ldai(std::vector<std::uint8_t>& c, std::int32_t v) {
    emit_op(c, finn::ohos::AbcOpcode::Ldai);
    append_le32(c, static_cast<std::uint32_t>(v));
}
void emit_fldai(std::vector<std::uint8_t>& c, double v) {
    emit_op(c, finn::ohos::AbcOpcode::Fldai);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    append_le64(c, bits);
}
void emit_lda_str(std::vector<std::uint8_t>& c, std::uint16_t id16) {
    emit_op(c, finn::ohos::AbcOpcode::LdaStr);
    append_le16(c, id16);  // real Panda ISA uses a 2-byte string_id here
}
void emit_binop(std::vector<std::uint8_t>& c, finn::ohos::AbcOpcode op, std::uint8_t lhs_reg) {
    emit_op(c, op);
    c.push_back(0);  // IC slot
    c.push_back(lhs_reg);
}
void emit_tryldglobal(std::vector<std::uint8_t>& c, std::uint16_t id16) {
    emit_op(c, finn::ohos::AbcOpcode::TryLdGlobalByName8);
    c.push_back(0);
    append_le16(c, id16);
}
void emit_trystglobal(std::vector<std::uint8_t>& c, std::uint16_t id16) {
    emit_op(c, finn::ohos::AbcOpcode::TryStGlobalByName8);
    c.push_back(0);
    append_le16(c, id16);
}
void emit_ldobj(std::vector<std::uint8_t>& c, std::uint16_t id16) {
    emit_op(c, finn::ohos::AbcOpcode::LdObjByName8);
    c.push_back(0);
    append_le16(c, id16);
}
void emit_stobj(std::vector<std::uint8_t>& c, std::uint16_t id16, std::uint8_t obj_reg) {
    emit_op(c, finn::ohos::AbcOpcode::StObjByName8);
    c.push_back(0);
    append_le16(c, id16);
    c.push_back(obj_reg);
}

std::vector<std::uint8_t> make_test_png() {
    return {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
            0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00};
}

std::vector<std::uint8_t> make_test_resources_index() {
    std::vector<std::uint8_t> idx(144, 0);
    const char* ver = "Restool 3.2.0";
    std::memcpy(idx.data(), ver, std::strlen(ver));
    idx[128] = 144;
    idx[132] = 2;  // limit_key_count
    return idx;
}

std::vector<std::uint8_t> make_test_elf(std::uint16_t machine) {
    std::vector<std::uint8_t> elf(64, 0);
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;  // ELFCLASS64
    elf[5] = 1;  // ELFDATA2LSB
    elf[6] = 1;  // EV_CURRENT
    elf[16] = 3;  // ET_DYN
    elf[18] = static_cast<std::uint8_t>(machine & 0xffu);
    elf[19] = static_cast<std::uint8_t>((machine >> 8) & 0xffu);
    return elf;
}

std::vector<std::uint8_t> build_demo_abc() {
    AbcBuilder b;

    const std::uint16_t id_hilog = b.add_id16_string("hilog");
    const std::uint16_t id_info = b.add_id16_string("info");
    const std::uint16_t id_console = b.add_id16_string("console");
    const std::uint16_t id_log = b.add_id16_string("log");
    const std::uint16_t id_deviceInfo = b.add_id16_string("deviceInfo");
    const std::uint16_t id_sdkApiVersion = b.add_id16_string("sdkApiVersion");
    const std::uint16_t id_startupTimeMs = b.add_id16_string("startupTimeMs");
    const std::uint16_t id_bundleName = b.add_id16_string("bundleName");
    const std::uint16_t id_bootApi = b.add_id16_string("boot_api");
    const std::uint16_t id_bootMs = b.add_id16_string("boot_ms");
    const std::uint16_t id_loadContent = b.add_id16_string("loadContent");
    const std::uint16_t id_arkui = b.add_id16_string("arkui");
    const std::uint16_t id_getStateInt = b.add_id16_string("getStateInt");
    const std::uint16_t id_setState = b.add_id16_string("setState");
    const std::uint16_t id_preferences = b.add_id16_string("preferences");
    const std::uint16_t id_getPrefsSync = b.add_id16_string("getPreferencesSync");
    const std::uint16_t id_putSync = b.add_id16_string("putSync");
    const std::uint16_t id_flushSync = b.add_id16_string("flushSync");
    const std::uint16_t id_customProp = b.add_id16_string("customProp");

    const std::uint16_t s_tag = b.add_id16_string("DemoAbility");
    const std::uint16_t s_created = b.add_id16_string("onCreate ready");
    const std::uint16_t s_page = b.add_id16_string("pages/Index");
    const std::uint16_t s_fg = b.add_id16_string("EntryAbility foreground");
    const std::uint16_t s_bg = b.add_id16_string("EntryAbility background");
    const std::uint16_t s_destroy = b.add_id16_string("EntryAbility destroy");
    const std::uint16_t s_count = b.add_id16_string("count");
    const std::uint16_t s_inc_msg = b.add_id16_string("incremented counter");
    const std::uint16_t s_ctx = b.add_id16_string("ctx");
    const std::uint16_t s_pref_name = b.add_id16_string("app");
    const std::uint16_t s_pref_key = b.add_id16_string("last_bundle");
    (void)b.add_string("@ohos.hilog");
    (void)b.add_string("@ohos.window");
    (void)b.add_string("@ohos.data.preferences");
    (void)b.add_string("@ohos.deviceInfo");

    // 1. computeSum(a, b): vregs=4, args=2 -> arg0=v4, arg1=v5
    std::vector<std::uint8_t> c_sum;
    emit_lda(c_sum, 4);
    emit_sta(c_sum, 0);
    emit_lda(c_sum, 5);
    emit_binop(c_sum, finn::ohos::AbcOpcode::Add2, 0);
    emit_op(c_sum, finn::ohos::AbcOpcode::Return);

    // 2. branchTest(x): vregs=4, args=1 -> arg0=v4; returns 100 if x < 10 else 200
    std::vector<std::uint8_t> c_branch;
    emit_ldai(c_branch, 10);       // 5 bytes
    emit_sta(c_branch, 0);         // 2 bytes
    emit_lda(c_branch, 4);         // 2 bytes
    emit_sta(c_branch, 1);         // 2 bytes
    emit_lda(c_branch, 0);         // 2 bytes (acc = 10)
    emit_binop(c_branch, finn::ohos::AbcOpcode::Less, 1);  // acc = (v1 < 10)
    // jeqz8 +8 jumps past: jeqz8(2) + ldai(5) + return(1) = 8 bytes
    emit_op(c_branch, finn::ohos::AbcOpcode::Jeqz8);
    c_branch.push_back(8);
    emit_ldai(c_branch, 100);      // 5 bytes
    emit_op(c_branch, finn::ohos::AbcOpcode::Return);  // 1 byte
    emit_ldai(c_branch, 200);
    emit_op(c_branch, finn::ohos::AbcOpcode::Return);

    // 3. arithmeticMix(): exercises sub2, mul2, div2, mod2, fldai, mov4, mov8,
    //    createemptyobject, stobjbyname, ldobjbyname, eq, jnez, jmp
    std::vector<std::uint8_t> c_arith;
    emit_ldai(c_arith, 20);
    emit_sta(c_arith, 0);
    emit_op(c_arith, finn::ohos::AbcOpcode::Mov4);
    c_arith.push_back(static_cast<std::uint8_t>((0u << 4) | 1u));  // v1 = v0 (20)
    emit_ldai(c_arith, 6);
    emit_binop(c_arith, finn::ohos::AbcOpcode::Sub2, 1);  // 20 - 6 = 14
    emit_sta(c_arith, 2);
    emit_ldai(c_arith, 3);
    emit_binop(c_arith, finn::ohos::AbcOpcode::Mul2, 2);  // 14 * 3 = 42
    emit_sta(c_arith, 3);
    emit_op(c_arith, finn::ohos::AbcOpcode::CreateEmptyObject);
    emit_sta(c_arith, 4);
    emit_lda(c_arith, 3);
    emit_stobj(c_arith, id_customProp, 4);
    emit_lda(c_arith, 4);
    emit_ldobj(c_arith, id_customProp);  // acc = 42
    emit_sta(c_arith, 5);
    emit_fldai(c_arith, 2.0);
    emit_binop(c_arith, finn::ohos::AbcOpcode::Div2, 5);  // 42 / 2 = 21
    emit_sta(c_arith, 6);
    emit_ldai(c_arith, 10);
    emit_binop(c_arith, finn::ohos::AbcOpcode::Mod2, 6);  // 21 % 10 = 1
    emit_sta(c_arith, 7);
    emit_op(c_arith, finn::ohos::AbcOpcode::Mov8);
    c_arith.push_back(8);
    c_arith.push_back(7);  // v8 = v7 (1)
    emit_ldai(c_arith, 1);
    emit_binop(c_arith, finn::ohos::AbcOpcode::Eq, 8);  // true
    // jnez8 +8 skips: jnez8(2) + ldai(5) + return(1)
    emit_op(c_arith, finn::ohos::AbcOpcode::Jnez8);
    c_arith.push_back(8);
    emit_ldai(c_arith, -1);
    emit_op(c_arith, finn::ohos::AbcOpcode::Return);
    // jmp8 +7 skips: jmp8(2) + ldai(5)
    emit_op(c_arith, finn::ohos::AbcOpcode::Jmp8);
    c_arith.push_back(7);
    emit_ldai(c_arith, -2);
    emit_lda(c_arith, 3);  // 42
    emit_op(c_arith, finn::ohos::AbcOpcode::Return);

    // 4. onCreate(want, launchParam): vregs=8, args=2 -> want=v8, launchParam=v9
    std::vector<std::uint8_t> c_onCreate;
    // Save boot_api = deviceInfo.sdkApiVersion
    emit_tryldglobal(c_onCreate, id_deviceInfo);
    emit_ldobj(c_onCreate, id_sdkApiVersion);
    emit_trystglobal(c_onCreate, id_bootApi);
    // Save boot_ms = launchParam.startupTimeMs
    emit_lda(c_onCreate, 9);
    emit_ldobj(c_onCreate, id_startupTimeMs);
    emit_trystglobal(c_onCreate, id_bootMs);
    // hilog.info(0x100, "DemoAbility", "onCreate API=%{public}d")
    emit_tryldglobal(c_onCreate, id_hilog);
    emit_sta(c_onCreate, 0);
    emit_ldai(c_onCreate, 0x100);
    emit_sta(c_onCreate, 1);
    emit_lda_str(c_onCreate, s_tag);
    emit_sta(c_onCreate, 2);
    emit_lda_str(c_onCreate, s_created);
    emit_sta(c_onCreate, 3);
    emit_lda(c_onCreate, 0);
    emit_ldobj(c_onCreate, id_info);
    emit_op(c_onCreate, finn::ohos::AbcOpcode::CallThis3);
    c_onCreate.push_back(0);
    c_onCreate.push_back(0);
    c_onCreate.push_back(1);
    c_onCreate.push_back(2);
    c_onCreate.push_back(3);
    // p = preferences.getPreferencesSync("ctx", "app"); p.putSync("last_bundle", want.bundleName); p.flushSync();
    emit_tryldglobal(c_onCreate, id_preferences);
    emit_sta(c_onCreate, 0);
    emit_lda_str(c_onCreate, s_ctx);
    emit_sta(c_onCreate, 1);
    emit_lda_str(c_onCreate, s_pref_name);
    emit_sta(c_onCreate, 2);
    emit_lda(c_onCreate, 0);
    emit_ldobj(c_onCreate, id_getPrefsSync);
    emit_op(c_onCreate, finn::ohos::AbcOpcode::CallThis2);
    c_onCreate.push_back(0);
    c_onCreate.push_back(0);
    c_onCreate.push_back(1);
    c_onCreate.push_back(2);
    emit_sta(c_onCreate, 4);  // prefs object
    emit_lda_str(c_onCreate, s_pref_key);
    emit_sta(c_onCreate, 5);
    emit_lda(c_onCreate, 8);  // want
    emit_ldobj(c_onCreate, id_bundleName);
    emit_sta(c_onCreate, 6);
    emit_lda(c_onCreate, 4);
    emit_ldobj(c_onCreate, id_putSync);
    emit_op(c_onCreate, finn::ohos::AbcOpcode::CallThis2);
    c_onCreate.push_back(0);
    c_onCreate.push_back(4);
    c_onCreate.push_back(5);
    c_onCreate.push_back(6);
    emit_lda(c_onCreate, 4);
    emit_ldobj(c_onCreate, id_flushSync);
    emit_op(c_onCreate, finn::ohos::AbcOpcode::CallThis0);
    c_onCreate.push_back(0);
    c_onCreate.push_back(4);
    emit_op(c_onCreate, finn::ohos::AbcOpcode::ReturnUndefined);

    // 5. onWindowStageCreate(windowStage): vregs=4, args=1 -> windowStage=v4
    std::vector<std::uint8_t> c_onWin;
    emit_lda_str(c_onWin, s_page);
    emit_sta(c_onWin, 0);
    emit_lda(c_onWin, 4);
    emit_ldobj(c_onWin, id_loadContent);
    emit_op(c_onWin, finn::ohos::AbcOpcode::CallThis1);
    c_onWin.push_back(0);
    c_onWin.push_back(4);
    c_onWin.push_back(0);
    emit_op(c_onWin, finn::ohos::AbcOpcode::ReturnUndefined);

    // 6. onForeground / onBackground / onDestroy
    auto make_console_logger = [&](std::uint16_t msg_str_off) {
        std::vector<std::uint8_t> c;
        emit_tryldglobal(c, id_console);
        emit_sta(c, 0);
        emit_lda_str(c, msg_str_off);
        emit_sta(c, 1);
        emit_lda(c, 0);
        emit_ldobj(c, id_log);
        emit_op(c, finn::ohos::AbcOpcode::CallThis1);
        c.push_back(0);
        c.push_back(0);
        c.push_back(1);
        emit_op(c, finn::ohos::AbcOpcode::ReturnUndefined);
        return c;
    };

    // 7. onIncrement(): cur = arkui.getStateInt("count"); arkui.setState("count", cur + 1);
    std::vector<std::uint8_t> c_inc;
    emit_tryldglobal(c_inc, id_arkui);
    emit_sta(c_inc, 0);
    emit_lda_str(c_inc, s_count);
    emit_sta(c_inc, 1);
    emit_lda(c_inc, 0);
    emit_ldobj(c_inc, id_getStateInt);
    emit_op(c_inc, finn::ohos::AbcOpcode::CallThis1);
    c_inc.push_back(0);
    c_inc.push_back(0);
    c_inc.push_back(1);
    emit_sta(c_inc, 2);  // cur
    emit_ldai(c_inc, 1);
    emit_binop(c_inc, finn::ohos::AbcOpcode::Add2, 2);  // cur + 1
    emit_sta(c_inc, 3);
    emit_lda(c_inc, 0);
    emit_ldobj(c_inc, id_setState);
    emit_op(c_inc, finn::ohos::AbcOpcode::CallThis2);
    c_inc.push_back(0);
    c_inc.push_back(0);
    c_inc.push_back(1);
    c_inc.push_back(3);
    emit_tryldglobal(c_inc, id_console);
    emit_sta(c_inc, 4);
    emit_lda_str(c_inc, s_inc_msg);
    emit_sta(c_inc, 5);
    emit_lda(c_inc, 4);
    emit_ldobj(c_inc, id_log);
    emit_op(c_inc, finn::ohos::AbcOpcode::CallThis1);
    c_inc.push_back(0);
    c_inc.push_back(4);
    c_inc.push_back(5);
    emit_op(c_inc, finn::ohos::AbcOpcode::ReturnUndefined);

    b.add_class("LEntryAbility;", "entry/src/main/ets/entryability/EntryAbility.ets",
                {
                    {"computeSum", 4, 2, c_sum},
                    {"branchTest", 4, 1, c_branch},
                    {"arithmeticMix", 10, 0, c_arith},
                    {"onCreate", 8, 2, c_onCreate},
                    {"onWindowStageCreate", 4, 1, c_onWin},
                    {"onForeground", 4, 0, make_console_logger(s_fg)},
                    {"onBackground", 4, 0, make_console_logger(s_bg)},
                    {"onDestroy", 4, 0, make_console_logger(s_destroy)},
                    {"onIncrement", 8, 2, c_inc},
                });
    return b.finish();
}

std::string demo_module_json() {
    return R"({
  // OpenHarmony 6.1 LTS (API 23) Stage model module.json (JSON5 comments allowed)
  "app": {
    "bundleName": "com.finnos.ohosdemo",
    "bundleType": "app",
    "vendor": "FinnOS",
    "versionCode": 60100,
    "versionName": "6.1.0",
    "icon": "$media:app_icon",
    "label": "Finn OHOS Demo",
    "minAPIVersion": 12,
    "targetAPIVersion": 23,
    "apiReleaseType": "Release",
  },
  "module": {
    "name": "entry",
    "type": "entry",
    "mainElement": "EntryAbility",
    "pages": "$profile:main_pages",
    "deviceTypes": ["default", "tablet", "2in1"],
    "abilities": [
      {
        "name": "EntryAbility",
        "srcEntry": "./ets/entryability/EntryAbility.ets",
        "icon": "$media:app_icon",
        "label": "Finn OHOS Demo",
        "exported": true,
        "allowSelfRedirect": true,
        "skills": [
          {
            "entities": ["entity.system.home"],
            "actions": ["action.system.home"]
          }
        ]
      }
    ],
    "requestPermissions": [
      { "name": "ohos.permission.INTERNET" }
    ],
    "dependencies": [
      { "bundleName": "com.finnos.ohosdemo", "moduleName": "sharedlib", "versionCode": 60100 }
    ]
  }
})";
}

std::string demo_ui_json() {
    return R"({
  "title": "Finn OHOS Demo",
  "state": {
    "count": 0,
    "username": "Finn"
  },
  "root": {
    "type": "Navigation",
    "id": "nav_root",
    "text": "OpenHarmony 6.1 LTS Demo",
    "children": [
      { "type": "Text", "id": "counter_text", "text": "Count: {{count}}" },
      { "type": "Button", "id": "inc_btn", "text": "Increment", "onClick": "onIncrement" },
      { "type": "TextInput", "id": "name_input", "stateKey": "username", "placeholder": "Enter name" },
      { "type": "Toggle", "id": "dark_toggle", "text": "Enable feature", "checked": false },
      { "type": "Slider", "id": "vol_slider", "min": 0, "max": 100, "value": 40 },
      { "type": "Progress", "id": "load_progress", "min": 0, "max": 100, "value": 75 },
      { "type": "Divider", "id": "sep_1" },
      {
        "type": "List",
        "id": "demo_list",
        "children": [
          { "type": "ListItem", "id": "li_1", "text": "Row A" },
          { "type": "ListItem", "id": "li_2", "text": "Row B" }
        ]
      },
      {
        "type": "Grid",
        "id": "demo_grid",
        "columns": 2,
        "multiSelect": true,
        "stateKey": "selected_tiles",
        "children": [
          { "type": "GridItem", "id": "gi_0", "text": "Tile 0" },
          { "type": "GridItem", "id": "gi_1", "text": "Tile 1" },
          { "type": "GridItem", "id": "gi_2", "text": "Tile 2" }
        ]
      },
      { "type": "Blank", "id": "spacer_1" },
      { "type": "AlertDialog", "id": "about_alert", "text": "About", "placeholder": "API 23 Ready" }
    ]
  }
})";
}

void populate_demo_hap(ZipBuilder* z) {
    z->add_text("module.json", demo_module_json(), true);
    z->add("resources.index", make_test_resources_index(), false);
    z->add("resources/base/media/app_icon.png", make_test_png(), false);
    z->add("ets/modules.abc", build_demo_abc(), true);
    z->add_text("resources/rawfile/ui.json", demo_ui_json(), true);
    const std::string host_abi = finn::ohos::host_ohos_abi();
    const std::uint16_t host_mach =
        (host_abi == "arm64-v8a") ? finn::ohos::kElfMachineAarch64
                                  : finn::ohos::kElfMachineX86_64;
    z->add("libs/" + host_abi + "/libdemo.so", make_test_elf(host_mach), true);
}

void populate_demo_hsp(ZipBuilder* z) {
    const std::string hsp_json = R"({
  "app": {
    "bundleName": "com.finnos.ohosdemo",
    "versionCode": 60100,
    "versionName": "6.1.0",
    "minAPIVersion": 12,
    "targetAPIVersion": 23
  },
  "module": {
    "name": "sharedlib",
    "type": "shared",
    "deviceTypes": ["default", "2in1"]
  }
})";
    z->add_text("module.json", hsp_json, true);
}

void build_demo_app_pack(const fs::path& app_path,
                         const std::vector<std::uint8_t>& hap_bytes,
                         const std::vector<std::uint8_t>& hsp_bytes) {
    const std::string pack_info = R"({
  "summary": {
    "app": {
      "bundleName": "com.finnos.ohosdemo",
      "bundleType": "app",
      "version": { "code": 60100, "name": "6.1.0" },
      "minAPIVersion": 12,
      "targetAPIVersion": 23
    }
  },
  "packages": [
    { "name": "entry-default.hap", "moduleName": "entry", "moduleType": "entry", "deviceType": ["default"] },
    { "name": "sharedlib-default.hsp", "moduleName": "sharedlib", "moduleType": "shared", "deviceType": ["default"] }
  ]
})";
    ZipBuilder pack;
    pack.add_text("pack.info", pack_info, true);
    pack.add("entry-default.hap", hap_bytes, false);
    pack.add("sharedlib-default.hsp", hsp_bytes, false);
    pack.write_to(app_path);
}

bool test_zip_reader_and_security() {
    ZipBuilder zb;
    zb.add_text("hello.txt", "OpenHarmony 6.1 LTS stored", false);
    zb.add_text("nested/deflated.txt",
                std::string(256, 'A') + " compressed payload", true);
    const auto zip_bytes = zb.build();

    finn::ohos::ZipReader zr;
    std::string err;
    CHECK(zr.open_memory(zip_bytes.data(), zip_bytes.size(), &err));
    CHECK(zr.entries().size() == 2);

    std::string s1, s2;
    CHECK(zr.read_entry_text("hello.txt", &s1, &err));
    CHECK(s1 == "OpenHarmony 6.1 LTS stored");
    CHECK(zr.read_entry_text("nested/deflated.txt", &s2, &err));
    CHECK(s2 == std::string(256, 'A') + " compressed payload");

    // Path traversal rejection
    CHECK(!finn::ohos::ZipReader::is_safe_entry_path("../escape.txt"));
    CHECK(!finn::ohos::ZipReader::is_safe_entry_path("a/../../etc/passwd"));
    CHECK(!finn::ohos::ZipReader::is_safe_entry_path("/etc/passwd"));
    CHECK(!finn::ohos::ZipReader::is_safe_entry_path("C:/Windows/System32"));
    CHECK(finn::ohos::ZipReader::is_safe_entry_path("ets/modules.abc"));

    ZipBuilder evil;
    evil.add_text("../evil.txt", "boom", false);
    const auto evil_bytes = evil.build();
    finn::ohos::ZipReader evil_reader;
    CHECK(!evil_reader.open_memory(evil_bytes.data(), evil_bytes.size(), &err));
    CHECK(err.find("unsafe path") != std::string::npos);

    return true;
}

bool test_hap_hsp_app_pack_and_diagnostics() {
    TempDir tmp("diag");

    ZipBuilder hap_b;
    populate_demo_hap(&hap_b);
    const auto hap_bytes = hap_b.build();
    const fs::path hap_path = tmp.path("demo.hap");
    CHECK(hap_b.write_to(hap_path));

    ZipBuilder hsp_b;
    populate_demo_hsp(&hsp_b);
    const auto hsp_bytes = hsp_b.build();
    const fs::path hsp_path = tmp.path("sharedlib.hsp");
    CHECK(hsp_b.write_to(hsp_path));

    const fs::path app_path = tmp.path("demo.app");
    build_demo_app_pack(app_path, hap_bytes, hsp_bytes);

    // Create a fake macOS .app directory bundle to verify distinction
    const fs::path mac_app = tmp.path("FakeMac.app");
    fs::create_directories(mac_app / "Contents");
    {
        std::ofstream p(mac_app / "Contents" / "Info.plist");
        p << "<plist><dict><key>CFBundleName</key><string>FakeMac</string></dict></plist>";
    }

    CHECK(finn::ohos::detect_ohos_package_kind(hap_path.string()) ==
          finn::ohos::OhosPackageKind::Hap);
    CHECK(finn::ohos::detect_ohos_package_kind(hsp_path.string()) ==
          finn::ohos::OhosPackageKind::Hsp);
    CHECK(finn::ohos::detect_ohos_package_kind(app_path.string()) ==
          finn::ohos::OhosPackageKind::AppPack);
    CHECK(finn::ohos::detect_ohos_package_kind(mac_app.string()) ==
          finn::ohos::OhosPackageKind::Unknown);

    // Lightweight zero-zlib compat probe must agree identically
    CHECK(finn::compat::is_ohos_package_path(hap_path.string()));
    CHECK(finn::compat::is_ohos_package_path(hsp_path.string()));
    CHECK(finn::compat::is_ohos_package_path(app_path.string()));
    CHECK(!finn::compat::is_ohos_package_path(mac_app.string()));
    CHECK(finn::compat::is_ohos_package_path("https://example.org/releases/app.hap?v=23"));

    // Diagnose compatible .hap and .app pack
    const auto d_hap = finn::ohos::diagnose_ohos_package(hap_path.string());
    CHECK(d_hap.valid);
    CHECK(d_hap.api_compatible);
    CHECK(d_hap.manifest.target_api_version == 23);
    CHECK(d_hap.manifest.abilities.size() == 1);
    CHECK(d_hap.manifest.abilities[0].allow_self_redirect);
    CHECK(d_hap.manifest.dependencies.size() == 1);
    CHECK(d_hap.manifest.dependencies[0].module_name == "sharedlib");
    CHECK(d_hap.resource_index.present && d_hap.resource_index.valid);
    CHECK(d_hap.abc_present && d_hap.abc_valid && d_hap.abc_checksum_valid);
    CHECK(d_hap.abc_version == "12.0.6.0");
    CHECK(d_hap.abc_class_count == 1);
    CHECK(d_hap.abc_method_count == 9);
    CHECK(d_hap.native_libs.size() == 1 && d_hap.native_libs[0].host_compatible);
    CHECK(d_hap.runnable);

    const auto d_app = finn::ohos::diagnose_ohos_package(app_path.string());
    CHECK(d_app.valid);
    CHECK(d_app.kind == finn::ohos::OhosPackageKind::AppPack);
    CHECK(d_app.pack_info.valid);
    CHECK(d_app.pack_info.packages.size() == 2);
    CHECK(d_app.runnable);

    // Diagnose an incompatible .hap (API 24 + @kit.ArkWeb + @kit.DistributedDataObject + wrong ABI)
    AbcBuilder bad_abc;
    (void)bad_abc.add_string("@kit.ArkWeb");
    (void)bad_abc.add_string("@kit.DistributedDataObject");
    bad_abc.add_class("LBadAbility;", "BadAbility.ets", {});
    ZipBuilder bad_hap;
    bad_hap.add_text("module.json", R"({
      "app": { "bundleName": "com.example.future", "versionCode": 1, "versionName": "1.0",
               "minAPIVersion": 24, "targetAPIVersion": 24 },
      "module": { "name": "entry", "type": "entry", "mainElement": "BadAbility" }
    })");
    bad_hap.add("ets/modules.abc", bad_abc.finish());
    const std::uint16_t wrong_mach =
        (finn::ohos::host_ohos_abi() == "arm64-v8a") ? finn::ohos::kElfMachineX86_64
                                                     : finn::ohos::kElfMachineAarch64;
    bad_hap.add("libs/wrong/libbad.so", make_test_elf(wrong_mach));
    const fs::path bad_path = tmp.path("future.hap");
    CHECK(bad_hap.write_to(bad_path));

    const auto d_bad = finn::ohos::diagnose_ohos_package(bad_path.string());
    CHECK(d_bad.valid);
    CHECK(!d_bad.api_compatible);
    CHECK(!d_bad.runnable);
    CHECK(d_bad.unsupported_kits.size() >= 2);
    bool found_arkweb = false;
    bool found_ddo = false;
    for (const auto& k : d_bad.unsupported_kits) {
        if (k == "@kit.ArkWeb") found_arkweb = true;
        if (k == "@kit.DistributedDataObject") found_ddo = true;
    }
    CHECK(found_arkweb && found_ddo);

    return true;
}

bool test_live_http_download_and_install() {
    TempDir tmp("http");
    const std::string ohos_root = tmp.path("ohos_root").string();

    ZipBuilder hap_b;
    populate_demo_hap(&hap_b);
    const auto hap_bytes = hap_b.build();

    ZipBuilder hsp_b;
    populate_demo_hsp(&hsp_b);
    const auto hsp_bytes = hsp_b.build();

    const fs::path app_pack_file = tmp.path("serve.app");
    build_demo_app_pack(app_pack_file, hap_bytes, hsp_bytes);
    std::ifstream ain(app_pack_file, std::ios::binary);
    std::vector<std::uint8_t> app_bytes((std::istreambuf_iterator<char>(ain)),
                                        std::istreambuf_iterator<char>());

    // Start ephemeral localhost HTTP server on 127.0.0.1:0
    const int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(srv >= 0);
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(::bind(srv, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(srv, 8) == 0);

    socklen_t alen = sizeof(addr);
    CHECK(::getsockname(srv, reinterpret_cast<struct sockaddr*>(&addr), &alen) == 0);
    const std::uint16_t port = ntohs(addr.sin_port);
    CHECK(port > 0);

    std::atomic<bool> stop{false};
    std::thread server_thread([&]() {
        while (!stop.load()) {
            const int cli = ::accept(srv, nullptr, nullptr);
            if (cli < 0) break;
            char req[4096] = {};
            const ssize_t n = ::recv(cli, req, sizeof(req) - 1, 0);
            if (n > 0) {
                std::string_view r(req, static_cast<std::size_t>(n));
                auto send_all = [&](std::string_view data) {
                    std::size_t off = 0;
                    while (off < data.size()) {
                        const ssize_t w = ::send(cli, data.data() + off, data.size() - off, 0);
                        if (w <= 0) break;
                        off += static_cast<std::size_t>(w);
                    }
                };
                if (r.find("GET /demo.hap ") != std::string_view::npos) {
                    const std::string hdr =
                        "HTTP/1.1 200 OK\r\nContent-Length: " +
                        std::to_string(hap_bytes.size()) +
                        "\r\nConnection: close\r\n\r\n";
                    send_all(hdr);
                    send_all(std::string_view(
                        reinterpret_cast<const char*>(hap_bytes.data()), hap_bytes.size()));
                } else if (r.find("GET /latest.app ") != std::string_view::npos) {
                    send_all("HTTP/1.1 302 Found\r\nLocation: /demo.app\r\n"
                             "Content-Length: 0\r\nConnection: close\r\n\r\n");
                } else if (r.find("GET /demo.app ") != std::string_view::npos) {
                    // Serve using HTTP/1.1 chunked transfer encoding
                    send_all("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                             "Connection: close\r\n\r\n");
                    char hex[32];
                    std::snprintf(hex, sizeof(hex), "%zx\r\n", app_bytes.size());
                    send_all(hex);
                    send_all(std::string_view(
                        reinterpret_cast<const char*>(app_bytes.data()), app_bytes.size()));
                    send_all("\r\n0\r\n\r\n");
                } else {
                    send_all("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                             "Connection: close\r\n\r\n");
                }
            }
            ::close(cli);
        }
    });

    auto stop_server = [&]() {
        stop.store(true);
        ::shutdown(srv, SHUT_RDWR);
        ::close(srv);
        if (server_thread.joinable()) server_thread.join();
    };

    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    // 1. Live HTTP install of .hap
    const auto r_hap = finn::ohos::install_ohos_package(base + "/demo.hap", ohos_root);
    if (!r_hap.ok) {
        std::fprintf(stderr, "HTTP .hap install error: %s\n", r_hap.error.c_str());
        stop_server();
        return false;
    }
    CHECK(r_hap.app.bundle_name == "com.finnos.ohosdemo");
    CHECK(r_hap.app.id == "ohos:com-finnos-ohosdemo");
    CHECK(!r_hap.app.icon_path.empty() && fs::is_regular_file(r_hap.app.icon_path));

    auto apps = finn::ohos::discover_installed_ohos_apps(ohos_root);
    CHECK(apps.size() == 1);
    CHECK(apps[0].bundle_name == "com.finnos.ohosdemo");
    CHECK(apps[0].target_api_version == 23);

    // Verify zero-zlib compat discovery reads the exact same registry.conf
    auto compat_apps = finn::compat::discover_ohos_apps(
        finn::ohos::default_ohos_registry_path(ohos_root));
    CHECK(compat_apps.size() == 1);
    CHECK(compat_apps[0].id == "ohos:com-finnos-ohosdemo");
    CHECK(compat_apps[0].icon_path == r_hap.app.icon_path);

    // Uninstall cleanly
    std::string uerr;
    CHECK(finn::ohos::uninstall_ohos_app("com.finnos.ohosdemo", ohos_root, &uerr));
    CHECK(finn::ohos::discover_installed_ohos_apps(ohos_root).empty());
    CHECK(!fs::exists(r_hap.app.install_dir));
    CHECK(!fs::exists(r_hap.app.icon_path));

    // 2. Live HTTP install of .app via 302 redirect + chunked transfer encoding
    const auto r_app = finn::ohos::install_ohos_package(base + "/latest.app", ohos_root);
    if (!r_app.ok) {
        std::fprintf(stderr, "HTTP .app install error: %s\n", r_app.error.c_str());
        stop_server();
        return false;
    }
    CHECK(r_app.app.bundle_name == "com.finnos.ohosdemo");
    CHECK(r_app.app.shared_packages.size() == 1);
    CHECK(fs::is_regular_file(r_app.app.shared_packages[0]));
    CHECK(fs::is_regular_file(r_app.app.icon_path));

    // 3. 404 fails cleanly
    const auto r_404 = finn::ohos::install_ohos_package(base + "/missing.hap", ohos_root);
    CHECK(!r_404.ok);

    stop_server();
    return true;
}

napi_value test_napi_add_cb(napi_env env, napi_callback_info info) {
    std::size_t argc = 2;
    napi_value argv[2] = {};
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    std::int32_t a = 0;
    std::int32_t b = 0;
    napi_get_value_int32(env, argv[0], &a);
    napi_get_value_int32(env, argv[1], &b);
    napi_value out = nullptr;
    napi_create_int32(env, a + b, &out);
    return out;
}

bool test_abc_vm_arkui_lifecycle_and_napi() {
    TempDir tmp("vm");
    const fs::path app_dir = tmp.path("app");
    fs::create_directories(app_dir / "resources" / "rawfile");
    {
        std::ofstream ui(app_dir / "resources" / "rawfile" / "ui.json");
        ui << demo_ui_json();
    }

    const auto abc_bytes = build_demo_abc();
    finn::ohos::AbcFile abc =
        finn::ohos::parse_abc_bytes(abc_bytes.data(), abc_bytes.size());
    CHECK(abc.valid);
    CHECK(abc.header.checksum_valid);
    CHECK(abc.header.version_string == "12.0.6.0");

    finn::ohos::ArkUiEngine arkui;
    finn::ohos::AbcRuntime rt;
    rt.set_bundle_context("com.finnos.ohosdemo", app_dir.string(),
                          tmp.path("ohos_root").string());
    rt.set_arkui(&arkui);
    std::string lerr;
    CHECK(rt.load_abc(std::move(abc), &lerr));

    // Direct Panda bytecode execution tests
    const auto r_sum = rt.call_method_by_name(
        "computeSum", finn::ohos::AbcValue::make_undefined(),
        {finn::ohos::AbcValue::make_int(19), finn::ohos::AbcValue::make_int(23)});
    CHECK(r_sum.ok && r_sum.return_value.to_int32() == 42);

    const auto r_b1 = rt.call_method_by_name(
        "branchTest", finn::ohos::AbcValue::make_undefined(),
        {finn::ohos::AbcValue::make_int(5)});
    CHECK(r_b1.ok && r_b1.return_value.to_int32() == 100);

    const auto r_b2 = rt.call_method_by_name(
        "branchTest", finn::ohos::AbcValue::make_undefined(),
        {finn::ohos::AbcValue::make_int(15)});
    CHECK(r_b2.ok && r_b2.return_value.to_int32() == 200);

    const auto r_arith = rt.call_method_by_name("arithmeticMix");
    CHECK(r_arith.ok && r_arith.return_value.to_int32() == 42);

    // Check @ohos.deviceInfo honesty
    const auto dev = rt.get_global("deviceInfo");
    CHECK(dev.obj_val->properties.at("sdkApiVersion").to_int32() == 23);
    CHECK(dev.obj_val->properties.at("osFullName").to_string() ==
          "OpenHarmony-6.1-LTS (FinnOS)");

    // Check @ohos.file.fs
    const auto fs_mod = rt.get_global("fs");
    fs_mod.obj_val->properties.at("writeTextSync").native_fn(
        rt, fs_mod,
        {finn::ohos::AbcValue::make_string("note.txt"),
         finn::ohos::AbcValue::make_string("hello-ohos")});
    const auto read_back = fs_mod.obj_val->properties.at("readTextSync").native_fn(
        rt, fs_mod, {finn::ohos::AbcValue::make_string("note.txt")});
    CHECK(read_back.to_string() == "hello-ohos");

    // Run OpenHarmony 6.1 LTS UIAbility lifecycle (keep mounted to test live ArkUI interaction)
    const auto manifest = finn::ohos::parse_module_json(demo_module_json());
    CHECK(manifest.valid);
    const auto report = rt.run_ability_lifecycle(
        manifest, 0, nullptr, /*headless=*/true, /*auto_close=*/false);
    CHECK(report.ok);
    CHECK(report.loaded_page == "pages/Index");
    CHECK(report.startup_time_ms >= 0);
    CHECK(rt.get_global("boot_api").to_int32() == 23);
    CHECK(rt.get_global("boot_ms").to_int32() >= 0);

    // Verify @ohos.data.preferences persisted across onCreate
    const fs::path pref_file =
        tmp.path("ohos_root") / "prefs" / "com.finnos.ohosdemo.app.conf";
    CHECK(fs::is_regular_file(pref_file));
    {
        std::ifstream pin(pref_file);
        std::stringstream ss;
        ss << pin.rdbuf();
        CHECK(ss.str().find("last_bundle=com.finnos.ohosdemo") != std::string::npos);
    }

    // Verify API 23 custom component lifecycle hooks fired on mount
    bool saw_appear = false;
    bool saw_attach = false;
    for (const auto& ev : arkui.lifecycle_log()) {
        if (ev == "aboutToAppear:nav_root") saw_appear = true;
        if (ev == "onAttach:nav_root") saw_attach = true;
    }
    CHECK(saw_appear && saw_attach);

    // Verify @State + ArkTS bytecode button click handler updates UI live
    const auto* counter_node = arkui.find_node("counter_text");
    CHECK(counter_node != nullptr);
    CHECK(arkui.resolve_text(*counter_node) == "Count: 0");
    CHECK(arkui.trigger_click("inc_btn"));
    CHECK(arkui.resolve_text(*counter_node) == "Count: 1");
    CHECK(arkui.trigger_click("inc_btn"));
    CHECK(arkui.resolve_text(*counter_node) == "Count: 2");

    // Verify TextInput, Toggle, Slider, AlertDialog, and API 23 Grid multi-select
    CHECK(arkui.trigger_change("name_input", "FinnOS"));
    CHECK(arkui.get_state("username") == "FinnOS");
    CHECK(arkui.trigger_change("dark_toggle", "true"));
    CHECK(arkui.find_node("dark_toggle")->checked);
    CHECK(arkui.trigger_change("vol_slider", "88"));
    CHECK(static_cast<int>(arkui.find_node("vol_slider")->value) == 88);
    CHECK(arkui.trigger_click("about_alert"));
    CHECK(arkui.alerts().size() == 1);

    CHECK(arkui.toggle_grid_item_selection("demo_grid", 0));
    CHECK(arkui.toggle_grid_item_selection("demo_grid", 2));
    CHECK(arkui.find_node("demo_grid")->selected_indices == std::vector<int>({0, 2}));
    CHECK(arkui.get_state("selected_tiles") == "0,2");

    // Verify API 23 onDetach + aboutToDisappear on unmount
    arkui.unmount();
    bool saw_detach = false;
    bool saw_disappear = false;
    for (const auto& ev : arkui.lifecycle_log()) {
        if (ev == "onDetach:nav_root") saw_detach = true;
        if (ev == "aboutToDisappear:nav_root") saw_disappear = true;
    }
    CHECK(saw_detach && saw_disappear);

    // Verify clean-room NAPI C ABI bridge
    napi_env env = rt.napi_env_handle();
    napi_value exports = nullptr;
    CHECK(napi_create_object(env, &exports) == napi_ok);
    napi_value ver_str = nullptr;
    CHECK(napi_create_string_utf8(env, "6.1-LTS", NAPI_AUTO_LENGTH, &ver_str) == napi_ok);
    CHECK(napi_set_named_property(env, exports, "version", ver_str) == napi_ok);
    napi_value pi_val = nullptr;
    CHECK(napi_create_double(env, 3.14159, &pi_val) == napi_ok);
    CHECK(napi_set_named_property(env, exports, "pi", pi_val) == napi_ok);

    napi_property_descriptor desc{};
    desc.utf8name = "add";
    desc.method = &test_napi_add_cb;
    CHECK(napi_define_properties(env, exports, 1, &desc) == napi_ok);

    napi_value got_ver = nullptr;
    CHECK(napi_get_named_property(env, exports, "version", &got_ver) == napi_ok);
    char ver_buf[32] = {};
    std::size_t ver_len = 0;
    CHECK(napi_get_value_string_utf8(env, got_ver, ver_buf, sizeof(ver_buf), &ver_len) == napi_ok);
    CHECK(std::string(ver_buf) == "6.1-LTS");

    const auto unwrapped = rt.unwrap_napi_value(exports);
    const auto sum_napi = unwrapped.obj_val->properties.at("add").native_fn(
        rt, unwrapped,
        {finn::ohos::AbcValue::make_int(17), finn::ohos::AbcValue::make_int(25)});
    CHECK(sum_napi.to_int32() == 42);

    // Missing native .so fails honestly
    std::string dl_err;
    CHECK(!rt.load_native_module(tmp.path("does_not_exist.so").string(), &dl_err));
    CHECK(!dl_err.empty());

    return true;
}

bool test_util_and_bundle_kits() {
    finn::ohos::AbcRuntime rt;
    const auto util = rt.get_global("util");
    CHECK(util.kind == finn::ohos::AbcValue::Kind::Object);
    auto call_util = [&](const std::string& name,
                         const std::vector<finn::ohos::AbcValue>& args) {
        return util.obj_val->properties.at(name).native_fn(rt, util, args);
    };

    // @ohos.util.generateRandomUUID: real RFC 4122 v4, host entropy.
    const std::string u1 = call_util("generateRandomUUID", {}).to_string();
    const std::string u2 = call_util("generateRandomUUID", {}).to_string();
    CHECK(u1.size() == 36);
    CHECK(u1[8] == '-' && u1[13] == '-' && u1[18] == '-' && u1[23] == '-');
    CHECK(u1[14] == '4');
    CHECK(u1[19] == '8' || u1[19] == '9' || u1[19] == 'a' || u1[19] == 'b');
    for (char c : u1) {
        if (c != '-') CHECK(std::isxdigit(static_cast<unsigned char>(c)) != 0);
    }
    CHECK(u1 != u2);

    // @ohos.util.getRandom: inclusive bounds, reversed bounds normalised.
    CHECK(call_util("getRandom", {finn::ohos::AbcValue::make_int(5),
                                  finn::ohos::AbcValue::make_int(5)})
              .to_int32() == 5);
    for (int i = 0; i < 64; ++i) {
        const int v = call_util("getRandom", {finn::ohos::AbcValue::make_int(10),
                                              finn::ohos::AbcValue::make_int(20)})
                          .to_int32();
        CHECK(v >= 10 && v <= 20);
    }
    const int flipped = call_util("getRandom", {finn::ohos::AbcValue::make_int(20),
                                                finn::ohos::AbcValue::make_int(10)})
                            .to_int32();
    CHECK(flipped >= 10 && flipped <= 20);

    // @ohos.util.Base64: strict RFC 4648, honest failure on malformed input.
    const auto base64 = util.obj_val->properties.at("Base64");
    auto b64_encode = [&](const std::string& s) {
        return base64.obj_val->properties.at("encode").native_fn(
            rt, base64, {finn::ohos::AbcValue::make_string(s)}).to_string();
    };
    auto b64_decode = [&](const std::string& s) {
        return base64.obj_val->properties.at("decode").native_fn(
            rt, base64, {finn::ohos::AbcValue::make_string(s)}).to_string();
    };
    CHECK(b64_encode("finn") == "Zmlubg==");
    CHECK(b64_encode("OpenHarmony") == "T3Blbkhhcm1vbnk=");
    CHECK(b64_encode("") == "");
    CHECK(b64_decode("Zmlubg==") == "finn");
    CHECK(b64_decode("T3Blbkhhcm1vbnk=") == "OpenHarmony");
    CHECK(b64_decode("!!!").empty());
    CHECK(b64_decode("Zml").empty());   // length not a multiple of four
    CHECK(b64_decode("Zml=").empty());  // impossible padding: '=' at position 3 only
    const std::string utf8 = "FinnOS \xe4\xb8\xad\xe6\x96\x87";
    CHECK(b64_decode(b64_encode(utf8)) == utf8);

    // Class-style Base64Encoder / Base64Decoder constructors.
    const auto encoder = call_util("Base64Encoder", {});
    CHECK(encoder.obj_val->properties.at("encodeSync")
              .native_fn(rt, encoder, {finn::ohos::AbcValue::make_string("abc")})
              .to_string() == "YWJj");
    const auto decoder = call_util("Base64Decoder", {});
    CHECK(decoder.obj_val->properties.at("decodeSync")
              .native_fn(rt, decoder, {finn::ohos::AbcValue::make_string("YWJj")})
              .to_string() == "abc");

    // @ohos.bundle.bundleManager returns only real manifest facts. Unset fields
    // stay empty rather than receiving plausible defaults.
    const auto bm = rt.get_global("bundleManager");
    auto self_info = [&]() {
        return bm.obj_val->properties.at("getBundleInfoForSelf")
            .native_fn(rt, bm, {});
    };
    CHECK(self_info().obj_val->properties.at("name").to_string().empty());

    finn::ohos::OhosBundleInfo info;
    info.name = "com.example.ohosapp";
    info.version_name = "2.3.4";
    info.version_code = 20304;
    info.module_name = "entry";
    info.main_element = "MainAbility";
    info.api_version = 23;
    rt.set_bundle_info(info);
    const auto bundle = self_info();
    CHECK(bundle.obj_val->properties.at("name").to_string() == info.name);
    CHECK(bundle.obj_val->properties.at("bundleName").to_string() == info.name);
    CHECK(bundle.obj_val->properties.at("versionName").to_string() == "2.3.4");
    CHECK(bundle.obj_val->properties.at("versionCode").to_int32() == 20304);
    CHECK(bundle.obj_val->properties.at("moduleName").to_string() == "entry");
    CHECK(bundle.obj_val->properties.at("mainElement").to_string() == "MainAbility");
    CHECK(bundle.obj_val->properties.at("apiVersion").to_int32() == 23);

    // Kit audit reports the new shims as supported and hardware kits as missing.
    const auto audit = finn::ohos::audit_ohos_kits(
        {"@ohos.util", "@ohos.bundle.bundleManager", "@kit.ArkWeb", "@ohos.nfc",
         "@ohos.aKitThatDoesNotExist"});
    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    CHECK(contains(audit.supported, "@ohos.util"));
    CHECK(contains(audit.supported, "@ohos.bundle.bundleManager"));
    CHECK(contains(audit.unsupported, "@kit.ArkWeb"));
    CHECK(contains(audit.unsupported, "@ohos.nfc"));
    CHECK(contains(audit.unknown, "@ohos.aKitThatDoesNotExist"));
    return true;
}

bool test_napi_full_surface() {
    finn::ohos::AbcRuntime rt;
    napi_env env = rt.napi_env_handle();
    napi_value v = nullptr;
    napi_valuetype type = napi_undefined;

    CHECK(napi_get_undefined(env, &v) == napi_ok);
    CHECK(napi_typeof(env, v, &type) == napi_ok && type == napi_undefined);
    CHECK(napi_get_null(env, &v) == napi_ok);
    CHECK(napi_typeof(env, v, &type) == napi_ok && type == napi_null);
    CHECK(napi_get_boolean(env, true, &v) == napi_ok);
    bool b = false;
    CHECK(napi_get_value_bool(env, v, &b) == napi_ok && b);
    CHECK(napi_typeof(env, v, &type) == napi_ok && type == napi_boolean);

    std::int64_t i64 = 0;
    CHECK(napi_create_int64(env, -9000000000LL, &v) == napi_ok);
    CHECK(napi_get_value_int64(env, v, &i64) == napi_ok);
    CHECK(i64 == -9000000000LL);
    CHECK(napi_typeof(env, v, &type) == napi_ok && type == napi_number);
    CHECK(napi_create_uint32(env, 4000000000u, &v) == napi_ok);
    CHECK(napi_get_value_int64(env, v, &i64) == napi_ok);
    CHECK(i64 == 4000000000LL);

    // Dense arrays with holes, element access, and honest type reporting.
    napi_value arr = nullptr;
    CHECK(napi_create_array(env, &arr) == napi_ok);
    bool is_array = false;
    CHECK(napi_is_array(env, arr, &is_array) == napi_ok && is_array);
    std::uint32_t len = 99;
    CHECK(napi_get_array_length(env, arr, &len) == napi_ok && len == 0);
    napi_value elem = nullptr;
    CHECK(napi_create_int32(env, 11, &elem) == napi_ok);
    CHECK(napi_set_element(env, arr, 0, elem) == napi_ok);
    CHECK(napi_create_int32(env, 44, &elem) == napi_ok);
    CHECK(napi_set_element(env, arr, 3, elem) == napi_ok);
    CHECK(napi_get_array_length(env, arr, &len) == napi_ok && len == 4);
    CHECK(napi_get_element(env, arr, 0, &elem) == napi_ok);
    std::int32_t i32 = 0;
    CHECK(napi_get_value_int32(env, elem, &i32) == napi_ok && i32 == 11);
    CHECK(napi_get_element(env, arr, 1, &elem) == napi_ok);
    CHECK(napi_typeof(env, elem, &type) == napi_ok && type == napi_undefined);

    napi_value sized = nullptr;
    CHECK(napi_create_array_with_length(env, 2, &sized) == napi_ok);
    CHECK(napi_get_array_length(env, sized, &len) == napi_ok && len == 2);

    CHECK(napi_create_string_utf8(env, "not-an-array", NAPI_AUTO_LENGTH, &v) == napi_ok);
    CHECK(napi_get_array_length(env, v, &len) == napi_object_expected);
    CHECK(napi_is_array(env, v, &is_array) == napi_ok && !is_array);
    CHECK(napi_create_array_with_length(env, (1u << 21u), &v) == napi_invalid_arg);

    // Errors are recorded rather than silently swallowed.
    CHECK(napi_throw_error(env, "ERR_OHOS", "kit unavailable") == napi_ok);
    CHECK(rt.last_napi_error() == "ERR_OHOS: kit unavailable");
    CHECK(napi_typeof(env, nullptr, &type) == napi_invalid_arg);
    CHECK(napi_set_element(env, arr, (1u << 21u), elem) == napi_invalid_arg);
    return true;
}

bool test_arkui_containers_and_image_resources() {
    CHECK(finn::ohos::parse_ark_component_kind("Tabs") ==
          finn::ohos::ArkComponentKind::Tabs);
    CHECK(finn::ohos::parse_ark_component_kind("TabContent") ==
          finn::ohos::ArkComponentKind::TabContent);
    CHECK(finn::ohos::parse_ark_component_kind("Swiper") ==
          finn::ohos::ArkComponentKind::Swiper);
    CHECK(finn::ohos::parse_ark_component_kind("RelativeContainer") ==
          finn::ohos::ArkComponentKind::RelativeContainer);
    CHECK(finn::ohos::ark_component_kind_name(
              finn::ohos::ArkComponentKind::RelativeContainer) == "RelativeContainer");

    TempDir tmp("arkui");
    const fs::path app = tmp.path("app");
    fs::create_directories(app / "resources" / "base" / "media");
    fs::create_directories(app / "resources" / "rawfile");
    {
        const auto png = make_test_png();
        std::ofstream p(app / "resources" / "base" / "media" / "logo.png",
                        std::ios::binary | std::ios::trunc);
        p.write(reinterpret_cast<const char*>(png.data()),
                static_cast<std::streamsize>(png.size()));
    }
    {
        std::ofstream p(app / "resources" / "rawfile" / "notes.txt");
        p << "real rawfile content";
    }

    const std::string doc = R"JSON({
  "title": "Containers",
  "root": {
    "type": "Column", "id": "root",
    "children": [
      { "type": "Tabs", "id": "tabs", "children": [
          { "type": "TabContent", "id": "tab1", "text": "One", "children": [
              { "type": "Text", "id": "t1", "text": "First" } ] },
          { "type": "TabContent", "id": "tab2", "text": "Two" }
      ] },
      { "type": "Swiper", "id": "swiper", "children": [
          { "type": "Text", "id": "s1", "text": "Slide 1" } ] },
      { "type": "RelativeContainer", "id": "rel", "children": [
          { "type": "Text", "id": "r1", "text": "Relative" } ] },
      { "type": "Image", "id": "img_ok", "src": "$media:logo" },
      { "type": "Image", "id": "img_r", "src": "$r('app.media.logo')" },
      { "type": "Image", "id": "img_raw", "src": "$rawfile:notes.txt" },
      { "type": "Image", "id": "img_missing", "src": "$media:does_not_exist" }
    ]
  }
})JSON";

    finn::ohos::ArkUiEngine eng;
    eng.set_resource_root(app.string());
    std::string err;
    CHECK(eng.load_from_json(doc, &err));

    const auto* tabs = eng.find_node("tabs");
    CHECK(tabs != nullptr && tabs->kind == finn::ohos::ArkComponentKind::Tabs);
    CHECK(tabs->children.size() == 2);
    const auto* tab = eng.find_node("tab1");
    CHECK(tab != nullptr && tab->kind == finn::ohos::ArkComponentKind::TabContent);
    const auto* swiper = eng.find_node("swiper");
    CHECK(swiper != nullptr && swiper->kind == finn::ohos::ArkComponentKind::Swiper);
    const auto* relative = eng.find_node("rel");
    CHECK(relative != nullptr &&
          relative->kind == finn::ohos::ArkComponentKind::RelativeContainer);

    const std::string media_path =
        (app / "resources" / "base" / "media" / "logo.png").string();
    const std::string raw_path =
        (app / "resources" / "rawfile" / "notes.txt").string();
    CHECK(eng.resolve_resource_path("$media:logo") == media_path);
    CHECK(eng.resolve_resource_path("$r('app.media.logo')") == media_path);
    CHECK(eng.resolve_resource_path("$rawfile:notes.txt") == raw_path);
    CHECK(eng.resolve_resource_path("$media:does_not_exist").empty());
    CHECK(eng.resolve_image_src(*eng.find_node("img_ok")) == media_path);
    CHECK(eng.resolve_image_src(*eng.find_node("img_r")) == media_path);
    CHECK(eng.resolve_image_src(*eng.find_node("img_raw")) == raw_path);
    CHECK(eng.resolve_image_src(*eng.find_node("img_missing")).empty());

    const std::string summary = eng.render_headless_summary();
    CHECK(summary.find("Tabs#tabs pages=2") != std::string::npos);
    CHECK(summary.find("Swiper#swiper pages=1") != std::string::npos);
    CHECK(summary.find("RelativeContainer#rel children=1") != std::string::npos);
    CHECK(summary.find("logo.png") != std::string::npos);
    CHECK(summary.find("Image missing: $media:does_not_exist") != std::string::npos);

    eng.mount();
    bool saw_attach = false;
    for (const auto& ev : eng.lifecycle_log()) {
        if (ev == "onAttach:tabs") saw_attach = true;
    }
    CHECK(saw_attach);
    return true;
}

// Contacts.hap (openharmony/applications_hap) failed onCreate on the public
// Panda opcode 0x7a (stownbyname). It must store an own property with the same
// semantics as stobjbyname in this prototype-chain-free runtime.
bool test_own_property_store_opcode() {
    AbcBuilder b;
    const std::uint16_t id_prop = b.add_id16_string("ownProp");
    std::vector<std::uint8_t> code;
    emit_op(code, finn::ohos::AbcOpcode::CreateEmptyObject);
    emit_sta(code, 0);  // v0 = {}
    emit_ldai(code, 42);
    emit_op(code, static_cast<finn::ohos::AbcOpcode>(0x7au));  // stownbyname
    code.push_back(0);  // IC slot
    append_le16(code, id_prop);
    code.push_back(0);  // object register v0
    emit_lda(code, 0);
    emit_ldobj(code, id_prop);  // acc = v0.ownProp
    emit_op(code, finn::ohos::AbcOpcode::Return);

    // istrue/isfalse (0x23/0x24) are op_none strict boolean tests.
    std::vector<std::uint8_t> c_true;
    emit_op(c_true, finn::ohos::AbcOpcode::LdTrue);
    emit_op(c_true, finn::ohos::AbcOpcode::IsTrue);
    emit_op(c_true, finn::ohos::AbcOpcode::Return);
    std::vector<std::uint8_t> c_false;
    emit_op(c_false, finn::ohos::AbcOpcode::LdTrue);
    emit_op(c_false, finn::ohos::AbcOpcode::IsFalse);
    emit_op(c_false, finn::ohos::AbcOpcode::Return);
    b.add_class("LOwnStore;", "OwnStore.ets",
                {{"store", 4, 0, code},
                 {"isTrue", 4, 0, c_true},
                 {"isFalse", 4, 0, c_false}});
    const auto bytes = b.finish();
    finn::ohos::AbcFile abc = finn::ohos::parse_abc_bytes(bytes.data(), bytes.size());
    CHECK(abc.valid);
    finn::ohos::AbcRuntime vm;
    std::string err;
    CHECK(vm.load_abc(std::move(abc), &err));
    const auto res = vm.call_method_by_name("store");
    CHECK(res.ok);
    CHECK(res.return_value.to_int32() == 42);
    const auto t = vm.call_method_by_name("isTrue");
    CHECK(t.ok && t.return_value.is_truthy());
    const auto f = vm.call_method_by_name("isFalse");
    CHECK(f.ok && !f.return_value.is_truthy());
    return true;
}

bool test_kit_shims_and_module_opcodes() {
    finn::ohos::AbcRuntime rt;

    // @ohos.app returns only real manifest facts.
    finn::ohos::OhosBundleInfo info;
    info.name = "com.example.kits";
    info.version_name = "4.5.6";
    info.version_code = 456;
    info.api_version = 23;
    rt.set_bundle_info(info);
    const auto app = rt.get_global("app");
    CHECK(app.obj_val->properties.at("getAppInfo")
              .native_fn(rt, app, {})
              .obj_val->properties.at("versionName")
              .to_string() == "4.5.6");

    // @ohos.curves — real easing maths with clamping.
    const auto curves = rt.get_global("curves");
    auto curve = [&](const std::string& name, double t) {
        return curves.obj_val->properties.at(name)
            .native_fn(rt, curves, {finn::ohos::AbcValue::make_double(t)})
            .to_number();
    };
    CHECK(curve("linear", 0.25) == 0.25);
    CHECK(curve("easeIn", 0.5) == 0.25);
    CHECK(curve("easeOut", 0.5) == 0.75);
    CHECK(curve("easeInOut", 0.25) == 0.125);
    CHECK(curve("easeInOut", -1.0) == 0.0);
    CHECK(curve("easeInOut", 2.0) == 1.0);

    // @ohos.matrix4 — real column-major arithmetic.
    const auto matrix = rt.get_global("matrix4");
    auto mat_call = [&](const std::string& name,
                        const std::vector<finn::ohos::AbcValue>& args) {
        return matrix.obj_val->properties.at(name).native_fn(rt, matrix, args);
    };
    const auto ident = mat_call("identity", {});
    CHECK(ident.obj_val->array_val.size() == 16);
    CHECK(ident.obj_val->array_val[0].to_number() == 1.0);
    CHECK(ident.obj_val->array_val[5].to_number() == 1.0);
    CHECK(ident.obj_val->array_val[10].to_number() == 1.0);
    CHECK(ident.obj_val->array_val[15].to_number() == 1.0);
    CHECK(ident.obj_val->array_val[1].to_number() == 0.0);
    const auto t1 = mat_call("translation", {finn::ohos::AbcValue::make_int(1),
                                             finn::ohos::AbcValue::make_int(2),
                                             finn::ohos::AbcValue::make_int(3)});
    CHECK(t1.obj_val->array_val[12].to_number() == 1.0);
    CHECK(t1.obj_val->array_val[13].to_number() == 2.0);
    CHECK(t1.obj_val->array_val[14].to_number() == 3.0);
    const auto t2 = mat_call("translation", {finn::ohos::AbcValue::make_int(4),
                                             finn::ohos::AbcValue::make_int(5),
                                             finn::ohos::AbcValue::make_int(6)});
    const auto combined = mat_call("multiply", {t1, t2});
    CHECK(combined.obj_val->array_val[12].to_number() == 5.0);
    CHECK(combined.obj_val->array_val[13].to_number() == 7.0);
    CHECK(combined.obj_val->array_val[14].to_number() == 9.0);

    // Diagnostics classify the three previously rejected specifiers as supported.
    const auto audit = finn::ohos::audit_ohos_kits(
        {"@ohos.app", "@ohos.curves", "@ohos.matrix4", "@kit.ArkWeb"});
    auto contains = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    CHECK(contains(audit.supported, "@ohos.app"));
    CHECK(contains(audit.supported, "@ohos.curves"));
    CHECK(contains(audit.supported, "@ohos.matrix4"));
    CHECK(contains(audit.unsupported, "@kit.ArkWeb"));

    // Real Panda ISA opcode 0x7e (ldexternalmodulevar) and 0xfe/0x09
    // (undefinedifholewithname), plus packed lexical load/store.
    AbcBuilder b;
    const std::uint32_t s_name = b.add_string("test_module");
    (void)s_name;
    std::vector<std::uint8_t> c_ext;
    emit_op(c_ext, static_cast<finn::ohos::AbcOpcode>(0x7eu));  // LdExternalModuleVar
    c_ext.push_back(0);                                          // import index 0
    emit_op(c_ext, static_cast<finn::ohos::AbcOpcode>(0xfeu));  // ThrowPrefixed
    c_ext.push_back(0x09);                                       // undefinedifholewithname
    append_le16(c_ext, 0);                                       // name id16
    emit_op(c_ext, finn::ohos::AbcOpcode::Return);

    std::vector<std::uint8_t> c_lex;
    emit_ldai(c_lex, 7);
    emit_op(c_lex, static_cast<finn::ohos::AbcOpcode>(0x3du));  // StLexVar level0/slot1
    c_lex.push_back(0x01);
    emit_op(c_lex, static_cast<finn::ohos::AbcOpcode>(0x3cu));  // LdLexVar level0/slot1
    c_lex.push_back(0x01);
    emit_op(c_lex, finn::ohos::AbcOpcode::Return);

    b.add_class("LModuleOps;", "ModuleOps.ets",
                {{"loadExternal", 4, 0, c_ext}, {"lexical", 4, 0, c_lex}});
    const auto bytes = b.finish();
    finn::ohos::AbcFile abc = finn::ohos::parse_abc_bytes(bytes.data(), bytes.size());
    CHECK(abc.valid);
    finn::ohos::AbcRuntime vm;
    std::string lerr;
    CHECK(vm.load_abc(std::move(abc), &lerr));

    // An unlinked external var is an honest placeholder (never a guessed value).
    const auto missing = vm.call_method_by_name("loadExternal");
    CHECK(missing.ok);
    CHECK(missing.return_value.kind == finn::ohos::AbcValue::Kind::Object);
    CHECK(missing.return_value.obj_val->class_name.find("UnresolvedImport") !=
          std::string::npos);

    vm.set_external_module_var(0, finn::ohos::AbcValue::make_int(23));
    const auto loaded = vm.call_method_by_name("loadExternal");
    CHECK(loaded.ok);
    CHECK(loaded.return_value.to_int32() == 23);

    const auto lex = vm.call_method_by_name("lexical");
    CHECK(lex.ok);
    CHECK(lex.return_value.to_int32() == 7);

    // ECMAScript globals used by real compiled ArkTS bytecode.
    CHECK(rt.get_global("Number").native_fn(
              rt, rt.get_global("Number"),
              {finn::ohos::AbcValue::make_string("42")}).to_number() == 42.0);
    CHECK(rt.get_global("String").native_fn(
              rt, rt.get_global("String"),
              {finn::ohos::AbcValue::make_int(7)}).to_string() == "7");
    CHECK(rt.get_global("Boolean").native_fn(
              rt, rt.get_global("Boolean"),
              {finn::ohos::AbcValue::make_int(0)}).is_truthy() == false);
    const auto json = rt.get_global("JSON");
    const auto round = json.obj_val->properties.at("parse").native_fn(
        rt, json, {json.obj_val->properties.at("stringify").native_fn(
                      rt, json, {finn::ohos::AbcValue::make_int(17)})});
    CHECK(round.to_number() == 17.0);
    return true;
}

int run_cli_cmd(const std::vector<std::string>& argv, const std::string& home_dir) {
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        ::setenv("HOME", home_dir.c_str(), 1);
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        ::execv(cargv[0], cargv.data());
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool test_cli_and_compat_plans() {
    TempDir tmp("cli");

    // Locate compiled src/ohos/finn-ohos relative to repo root or tests/unit
    fs::path bin;
    for (const char* cand : {"../../src/ohos/finn-ohos", "src/ohos/finn-ohos"}) {
        std::error_code ec;
        if (fs::is_regular_file(cand, ec)) {
            bin = fs::canonical(cand, ec);
            break;
        }
    }
    CHECK(!bin.empty());

    ZipBuilder hap_b;
    populate_demo_hap(&hap_b);
    const auto hap_bytes = hap_b.build();
    ZipBuilder hsp_b;
    populate_demo_hsp(&hsp_b);
    const auto hsp_bytes = hsp_b.build();
    const fs::path hap_path = tmp.path("cli_demo.hap");
    CHECK(hap_b.write_to(hap_path));
    const fs::path app_path = tmp.path("cli_demo.app");
    build_demo_app_pack(app_path, hap_bytes, hsp_bytes);

    // Prepend finn-ohos directory to PATH so probe_ohos_runtime() discovers it
    const char* old_path = std::getenv("PATH");
    const std::string new_path =
        bin.parent_path().string() + (old_path ? (std::string(":") + old_path) : "");
    ::setenv("PATH", new_path.c_str(), 1);

    const auto rt = finn::compat::probe_ohos_runtime();
    CHECK(rt.finn_ohos_installed);
    CHECK(rt.ready);
    CHECK(rt.supported_api_version == 23);

    const auto p_inst = finn::compat::build_ohos_install_plan(app_path.string());
    CHECK(p_inst.ok && p_inst.argv.size() == 3 && p_inst.argv[1] == "install");
    const auto p_diag = finn::compat::build_ohos_diagnose_plan(hap_path.string());
    CHECK(p_diag.ok && p_diag.argv.size() == 3 && p_diag.argv[1] == "diagnose");
    const auto p_run = finn::compat::build_ohos_launch_plan("com.finnos.ohosdemo");
    CHECK(p_run.ok && p_run.argv.size() == 3 && p_run.argv[1] == "run");

    const std::string fake_home = tmp.path("home").string();
    fs::create_directories(fake_home);

    CHECK(run_cli_cmd({bin.string(), "status"}, fake_home) == 0);
    CHECK(run_cli_cmd({bin.string(), "diagnose", hap_path.string()}, fake_home) == 0);

    // A package requiring a genuinely missing hardware kit is reported
    // precisely and fails cleanly (exit 2) rather than pretending to work.
    AbcBuilder camera_abc;
    (void)camera_abc.add_string("@kit.CameraKit");
    camera_abc.add_class("LCameraAbility;", "CameraAbility.ets", {});
    ZipBuilder camera_hap;
    camera_hap.add_text("module.json", R"({
      "app": { "bundleName": "com.example.camera", "versionCode": 1, "versionName": "1.0",
               "minAPIVersion": 12, "targetAPIVersion": 23 },
      "module": { "name": "entry", "type": "entry", "mainElement": "CameraAbility" }
    })");
    camera_hap.add("ets/modules.abc", camera_abc.finish());
    const fs::path camera_path = tmp.path("camera.hap");
    CHECK(camera_hap.write_to(camera_path));
    const auto camera_diag = finn::ohos::diagnose_ohos_package(camera_path.string());
    CHECK(camera_diag.valid && !camera_diag.runnable);
    CHECK(std::find(camera_diag.unsupported_kits.begin(),
                    camera_diag.unsupported_kits.end(),
                    "@kit.CameraKit") != camera_diag.unsupported_kits.end());
    CHECK(run_cli_cmd({bin.string(), "diagnose", camera_path.string()}, fake_home) == 2);

    // Running and installing the same package refuse it with the same precise
    // reason: no silent "kit unavailable" continuation.
    std::string run_err;
    CHECK(finn::ohos::run_ohos_target(camera_path.string(), 0, nullptr, true,
                                      {}, nullptr, &run_err) == 2);
    CHECK(run_err.find("@kit.CameraKit") != std::string::npos);
    const auto camera_install = finn::ohos::install_ohos_package(
        camera_path.string(), tmp.path("camera_root").string());
    CHECK(!camera_install.ok);
    CHECK(camera_install.error.find("@kit.CameraKit") != std::string::npos);

    CHECK(run_cli_cmd({bin.string(), "install", app_path.string()}, fake_home) == 0);
    CHECK(run_cli_cmd({bin.string(), "list"}, fake_home) == 0);
    CHECK(run_cli_cmd({bin.string(), "run", "com.finnos.ohosdemo", "--headless"},
                      fake_home) == 0);
    CHECK(run_cli_cmd({bin.string(), "uninstall", "com.finnos.ohosdemo"}, fake_home) == 0);

    return true;
}

}  // namespace

int main() {
    struct Case {
        const char* name;
        bool (*fn)();
    };
    const Case cases[] = {
        {"zip_reader_and_security", &test_zip_reader_and_security},
        {"hap_hsp_app_pack_and_diagnostics", &test_hap_hsp_app_pack_and_diagnostics},
        {"live_http_download_and_install", &test_live_http_download_and_install},
        {"abc_vm_arkui_lifecycle_and_napi", &test_abc_vm_arkui_lifecycle_and_napi},
        {"util_and_bundle_kits", &test_util_and_bundle_kits},
        {"napi_full_surface", &test_napi_full_surface},
        {"arkui_containers_and_image_resources", &test_arkui_containers_and_image_resources},
        {"kit_shims_and_module_opcodes", &test_kit_shims_and_module_opcodes},
        {"own_property_store_opcode", &test_own_property_store_opcode},
        {"cli_and_compat_plans", &test_cli_and_compat_plans},
    };
    int failed = 0;
    for (const auto& c : cases) {
        if (!c.fn()) {
            std::fprintf(stderr, "FAIL: %s\n", c.name);
            ++failed;
        } else {
            std::printf("PASS: %s\n", c.name);
        }
    }
    return failed == 0 ? 0 : 1;
}
