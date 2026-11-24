#!/usr/bin/env python3
"""Minimal deterministic COFF (.obj) -> PE32+ UEFI Application (.efi) linker.

Links a single freestanding COFF object compiled with:
  clang --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
        -fshort-wchar -mno-red-zone -fno-jump-tables -O2 -c ...
  clang --target=aarch64-unknown-windows -ffreestanding -fno-stack-protector \
        -fshort-wchar -fno-jump-tables -O2 -c ...

Emits a relocatable PE32+ EFI Application (Subsystem=10) with a valid .reloc
base relocation table so EDK2 / OVMF can load it at any physical address.
Used when lld-link is not installed on the build host.
"""

import struct
import sys

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_FILE_MACHINE_ARM64 = 0xAA64

# AMD64 relocation types
REL_AMD64_ADDR64 = 0x0001
REL_AMD64_ADDR32NB = 0x0003
REL_AMD64_REL32 = 0x0004
REL_AMD64_REL32_1 = 0x0005
REL_AMD64_REL32_2 = 0x0006
REL_AMD64_REL32_3 = 0x0007
REL_AMD64_REL32_4 = 0x0008
REL_AMD64_REL32_5 = 0x0009

# ARM64 relocation types
REL_ARM64_ADDR64 = 0x000E
REL_ARM64_BRANCH26 = 0x0003
REL_ARM64_PAGEBASE_REL21 = 0x0004
REL_ARM64_PAGEOFFSET_12A = 0x0006
REL_ARM64_PAGEOFFSET_12L = 0x0007

IMAGE_REL_BASED_DIR64 = 10

SECTION_ALIGN = 0x1000
FILE_ALIGN = 0x200
IMAGE_BASE = 0x0000000140000000


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


def read_coff_name(raw8, strtab):
    if raw8[:4] == b"\x00\x00\x00\x00":
        off = struct.unpack_from("<I", raw8, 4)[0]
        end = strtab.find(b"\x00", off)
        return strtab[off:end].decode("ascii", "replace")
    return raw8.split(b"\x00", 1)[0].decode("ascii", "replace")


def link_coff_to_efi(obj_path, out_path, entry_name="efi_main"):
    with open(obj_path, "rb") as f:
        data = f.read()

    machine, n_secs, _ts, sym_off, n_syms, opt_sz, _chars = struct.unpack_from("<HHIIIHH", data, 0)
    if machine not in (IMAGE_FILE_MACHINE_AMD64, IMAGE_FILE_MACHINE_ARM64):
        raise SystemExit(f"coff2efi: unsupported COFF machine 0x{machine:04x}")

    strtab_off = sym_off + n_syms * 18
    strtab = data[strtab_off:]

    # Parse COFF sections
    sec_off = 20 + opt_sz
    raw_secs = []
    for idx in range(n_secs):
        hdr = data[sec_off + idx * 40 : sec_off + (idx + 1) * 40]
        name = read_coff_name(hdr[:8], strtab)
        vsize, vaddr, rsize, roff, rel_off, _lno, n_rel, _nlno, flags = struct.unpack_from(
            "<IIIIIIHHI", hdr, 8
        )
        # Handle extended relocations (IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000)
        relocs = []
        if n_rel > 0 or (flags & 0x01000000):
            if (flags & 0x01000000) and n_rel == 0xFFFF:
                actual_n_rel = struct.unpack_from("<I", data, rel_off)[0]
                start_r = 1
            else:
                actual_n_rel = n_rel
                start_r = 0
            for r in range(start_r, actual_n_rel):
                rvaddr, rsym, rtype = struct.unpack_from("<IIH", data, rel_off + r * 10)
                relocs.append((rvaddr, rsym, rtype))
        payload = bytearray(data[roff : roff + rsize]) if roff and rsize else bytearray(max(vsize, rsize))
        raw_secs.append({
            "index": idx + 1,
            "name": name,
            "flags": flags,
            "data": payload,
            "vsize": max(vsize, len(payload)),
            "relocs": relocs,
            "rva": 0,
            "keep": False,
        })

    # Keep allocatable code/data/bss sections; merge into 4 canonical PE sections
    # (.text, .rdata, .data, .bss) so SizeOfHeaders is always <= SECTION_ALIGN
    # and the resulting .efi binary is compact and EDK2 PeCoffLib-compliant.
    buckets = {
        ".text":  {"name": ".text",  "flags": 0x60000020, "data": bytearray(), "subs": []},
        ".rdata": {"name": ".rdata", "flags": 0x40000040, "data": bytearray(), "subs": []},
        ".data":  {"name": ".data",  "flags": 0xC0000040, "data": bytearray(), "subs": []},
        ".bss":   {"name": ".bss",   "flags": 0xC0000080, "data": bytearray(), "subs": []},
    }
    for s in raw_secs:
        n = s["name"]
        if n.startswith((".drectve", ".llvm", ".debug", ".pdata", ".xdata", ".comment")):
            continue
        if (s["flags"] & 0x02000000) != 0:  # IMAGE_SCN_MEM_DISCARDABLE
            continue
        if (s["flags"] & (0x00000020 | 0x00000040 | 0x00000080)) == 0:  # CODE|IDATA|UDATA
            continue
        if s["vsize"] == 0 and len(s["data"]) == 0:
            continue
        if s["flags"] & 0x00000020:
            bname = ".text"
        elif s["flags"] & 0x00000080:
            bname = ".bss"
        elif s["flags"] & 0x80000000:  # IMAGE_SCN_MEM_WRITE
            bname = ".data"
        else:
            bname = ".rdata"

        b = buckets[bname]
        # Respect section alignment (COFF align bits 20..23: 1<<(n-1), default 16)
        align_enc = (s["flags"] >> 20) & 0xF
        sec_align = (1 << (align_enc - 1)) if align_enc > 0 else 16
        if sec_align < 16:
            sec_align = 16
        cur_off = align_up(len(b["data"]), sec_align)
        if cur_off > len(b["data"]):
            b["data"].extend(b"\x00" * (cur_off - len(b["data"])))
        s["bucket"] = bname
        s["bucket_off"] = cur_off
        if (s["flags"] & 0x00000080) and len(s["data"]) < s["vsize"]:
            s["data"].extend(b"\x00" * (s["vsize"] - len(s["data"])))
        b["data"].extend(s["data"])
        b["subs"].append(s)
        s["keep"] = True

    keep_secs = []
    for bname in (".text", ".rdata", ".data", ".bss"):
        b = buckets[bname]
        if len(b["data"]) == 0:
            continue
        keep_secs.append({
            "name": b["name"],
            "flags": b["flags"],
            "data": b["data"],
            "vsize": len(b["data"]),
            "subs": b["subs"],
            "rva": 0,
        })

    # Compute headers size up-front (+1 for .reloc) so first section RVA >= headers_raw_sz
    opt_hdr_sz = 240  # PE32+ with 16 data directories
    headers_raw_sz = align_up(0x80 + 4 + 20 + opt_hdr_sz + (len(keep_secs) + 1) * 40, FILE_ALIGN)

    # Assign RVAs to merged sections and propagate to each raw sub-section
    cur_rva = align_up(headers_raw_sz, SECTION_ALIGN)
    for s in keep_secs:
        s["rva"] = cur_rva
        for sub in s["subs"]:
            sub["rva"] = cur_rva + sub["bucket_off"]
        cur_rva = align_up(cur_rva + max(1, len(s["data"])), SECTION_ALIGN)

    # Parse COFF symbol table
    syms = {}
    i = 0
    while i < n_syms:
        soff = sym_off + i * 18
        sname = read_coff_name(data[soff : soff + 8], strtab)
        sval, ssec, stype, sclass, naux = struct.unpack_from("<IhHBB", data, soff + 8)
        syms[i] = {
            "name": sname,
            "value": sval,
            "sec": ssec,
            "class": sclass,
        }
        i += 1 + naux

    # Resolve symbol RVAs
    def sym_rva(sym_idx):
        sym = syms[sym_idx]
        if sym["sec"] > 0:
            sec = raw_secs[sym["sec"] - 1]
            if not sec["keep"]:
                raise SystemExit(f"coff2efi: symbol '{sym['name']}' in dropped section {sec['name']}")
            return sec["rva"] + sym["value"]
        raise SystemExit(f"coff2efi: unresolved external symbol '{sym['name']}'")

    entry_rva = None
    for idx, sym in syms.items():
        if sym["name"] == entry_name and sym["sec"] > 0:
            entry_rva = sym_rva(idx)
            break
    if entry_rva is None:
        raise SystemExit(f"coff2efi: entry symbol '{entry_name}' not found")

    # Apply relocations and collect DIR64 base relocs
    dir64_rvas = []
    for s in keep_secs:
        buf = s["data"]
        for sub in s["subs"]:
            base_off = sub["bucket_off"]
            for rvaddr, rsym, rtype in sub["relocs"]:
                target_rva = sym_rva(rsym)
                buf_off = base_off + rvaddr
                place_rva = s["rva"] + buf_off
                if machine == IMAGE_FILE_MACHINE_AMD64:
                    if rtype == REL_AMD64_ADDR64:
                        addend = struct.unpack_from("<Q", buf, buf_off)[0]
                        struct.pack_into("<Q", buf, buf_off, IMAGE_BASE + target_rva + addend)
                        dir64_rvas.append(place_rva)
                    elif rtype == REL_AMD64_ADDR32NB:
                        addend = struct.unpack_from("<i", buf, buf_off)[0]
                        struct.pack_into("<I", buf, buf_off, (target_rva + addend) & 0xFFFFFFFF)
                    elif REL_AMD64_REL32 <= rtype <= REL_AMD64_REL32_5:
                        extra = rtype - REL_AMD64_REL32
                        addend = struct.unpack_from("<i", buf, buf_off)[0]
                        disp = (target_rva + addend) - (place_rva + 4 + extra)
                        struct.pack_into("<i", buf, buf_off, disp)
                    else:
                        raise SystemExit(f"coff2efi: unsupported AMD64 reloc {rtype} in {sub['name']}")
                elif machine == IMAGE_FILE_MACHINE_ARM64:
                    insn = struct.unpack_from("<I", buf, buf_off)[0]
                    if rtype == REL_ARM64_ADDR64:
                        addend = struct.unpack_from("<Q", buf, buf_off)[0]
                        struct.pack_into("<Q", buf, buf_off, IMAGE_BASE + target_rva + addend)
                        dir64_rvas.append(place_rva)
                    elif rtype == REL_ARM64_BRANCH26:
                        delta = (target_rva - place_rva) >> 2
                        insn = (insn & 0xFC000000) | (delta & 0x03FFFFFF)
                        struct.pack_into("<I", buf, buf_off, insn)
                    elif rtype == REL_ARM64_PAGEBASE_REL21:
                        page_delta = (target_rva >> 12) - (place_rva >> 12)
                        immlo = page_delta & 0x3
                        immhi = (page_delta >> 2) & 0x7FFFF
                        insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5)
                        struct.pack_into("<I", buf, buf_off, insn)
                    elif rtype == REL_ARM64_PAGEOFFSET_12A:
                        imm12 = target_rva & 0xFFF
                        insn = (insn & 0xFFC003FF) | (imm12 << 10)
                        struct.pack_into("<I", buf, buf_off, insn)
                    elif rtype == REL_ARM64_PAGEOFFSET_12L:
                        size = (insn >> 30) & 0x3
                        opc = (insn >> 22) & 0x3
                        is_vec128 = ((insn >> 26) & 1) == 1 and size == 0 and opc >= 2
                        shift = 4 if is_vec128 else size
                        imm12 = (target_rva & 0xFFF) >> shift
                        insn = (insn & 0xFFC003FF) | (imm12 << 10)
                        struct.pack_into("<I", buf, buf_off, insn)
                    else:
                        raise SystemExit(f"coff2efi: unsupported ARM64 reloc {rtype} in {sub['name']}")

    # Build .reloc section
    reloc_bytes = bytearray()
    pages = {}
    for rva in sorted(set(dir64_rvas)):
        page = rva & ~0xFFF
        pages.setdefault(page, []).append(rva & 0xFFF)
    if not pages:
        # Emit a minimal valid empty relocation block
        reloc_bytes.extend(struct.pack("<IIHH", keep_secs[0]["rva"], 12, 0, 0))
    else:
        for page, offs in sorted(pages.items()):
            entries = [(IMAGE_REL_BASED_DIR64 << 12) | o for o in offs]
            if len(entries) & 1:
                entries.append(0)
            block_sz = 8 + 2 * len(entries)
            reloc_bytes.extend(struct.pack("<II", page, block_sz))
            for e in entries:
                reloc_bytes.extend(struct.pack("<H", e))

    reloc_rva = cur_rva
    keep_secs.append({
        "name": ".reloc",
        "flags": 0x42000040,  # INITIALIZED_DATA | MEM_DISCARDABLE | MEM_READ
        "data": reloc_bytes,
        "vsize": len(reloc_bytes),
        "rva": reloc_rva,
    })
    size_of_image = align_up(reloc_rva + max(1, len(reloc_bytes)), SECTION_ALIGN)

    # Assemble PE32+ headers
    dos_hdr = bytearray(0x80)
    dos_hdr[0:2] = b"MZ"
    struct.pack_into("<I", dos_hdr, 0x3C, 0x80)

    n_out_secs = len(keep_secs)
    opt_hdr_sz = 240  # PE32+ with 16 data directories
    headers_raw_sz = align_up(0x80 + 4 + 20 + opt_hdr_sz + n_out_secs * 40, FILE_ALIGN)

    # File layout for sections
    cur_foff = headers_raw_sz
    sec_hdrs = bytearray()
    sec_bodies = bytearray()
    size_of_code = 0
    size_of_idata = 0
    base_of_code = keep_secs[0]["rva"]

    for s in keep_secs:
        raw_sz = align_up(len(s["data"]), FILE_ALIGN)
        vsz = max(s["vsize"], len(s["data"]))
        if s["flags"] & 0x00000020:
            size_of_code += raw_sz
        else:
            size_of_idata += raw_sz
        nm = s["name"].encode("ascii")[:8].ljust(8, b"\x00")
        pe_chars = s["flags"] & 0xFF0000FF
        sec_hdrs.extend(struct.pack(
            "<8sIIIIIIHHI",
            nm, vsz, s["rva"], raw_sz, cur_foff, 0, 0, 0, 0, pe_chars
        ))
        sec_bodies.extend(s["data"])
        sec_bodies.extend(b"\x00" * (raw_sz - len(s["data"])))
        cur_foff += raw_sz

    # COFF File Header (20 bytes)
    coff_hdr = struct.pack(
        "<HHIIIHH",
        machine,
        n_out_secs,
        0,
        0,
        0,
        opt_hdr_sz,
        0x0022,  # EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    )

    # PE32+ Optional Header (240 bytes)
    data_dirs = bytearray(16 * 8)
    # Directory 5 = Base Relocation Table
    struct.pack_into("<II", data_dirs, 5 * 8, reloc_rva, len(reloc_bytes))

    opt_hdr = struct.pack(
        "<HBBIIIIIQIIHHHHHHIIIIHHQQQQII",
        0x020B,          # PE32+ Magic
        14, 0,           # Linker version
        size_of_code,
        size_of_idata,
        0,
        entry_rva,
        base_of_code,
        IMAGE_BASE,
        SECTION_ALIGN,
        FILE_ALIGN,
        0, 0,            # OS version
        1, 0,            # Image version
        0, 0,            # Subsystem version
        0,               # Win32VersionValue
        size_of_image,
        headers_raw_sz,
        0,               # CheckSum
        10,              # IMAGE_SUBSYSTEM_EFI_APPLICATION
        0x0160,          # HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT
        0, 0, 0, 0,      # Stack/Heap sizes
        0,               # LoaderFlags
        16,              # NumberOfRvaAndSizes
    ) + data_dirs

    pe = bytearray()
    pe.extend(dos_hdr)
    pe.extend(b"PE\x00\x00")
    pe.extend(coff_hdr)
    pe.extend(opt_hdr)
    pe.extend(sec_hdrs)
    pe.extend(b"\x00" * (headers_raw_sz - len(pe)))
    pe.extend(sec_bodies)

    with open(out_path, "wb") as f:
        f.write(pe)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit("usage: coff2efi.py <input.obj> <output.efi> [entry_symbol]")
    entry = sys.argv[3] if len(sys.argv) > 3 else "efi_main"
    link_coff_to_efi(sys.argv[1], sys.argv[2], entry)
