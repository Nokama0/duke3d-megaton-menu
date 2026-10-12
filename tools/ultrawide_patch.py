#!/usr/bin/env python3
"""Duke Nukem 3D: Megaton Edition - ultrawide (3440x1440) borderless-windowed patch.

usage: ultrawide_patch.py [stock_exe] [output_exe] [--force]

Always run against the STOCK exe (duke3d.exe.orig, SHA-256 checked); it regenerates the patched exe from scratch
rather than layering onto an already-patched file. It never writes over its input or over a stock exe, and replaces
an existing output only with --force, which first keeps the old file as <output>.bak.

What it does
  1. getvalidmodes:  accept a windowed mode as wide as the desktop (was: strictly narrower)
  2. checkvideomode: raise the 2560 width cap to 3440, but ONLY for bpp>8 (GL/Polymost).
                     The 8-bit classic renderer keeps the stock 2560 cap, which is what
                     keeps every software-renderer MAXXDIM array in bounds.
  3. setvideomode:   windowed GL window gets SDL_WINDOW_BORDERLESS
  4. Relocates the per-column (MAXXDIM=2560) arrays that the GL path writes for
     xdim/xdimen columns into a new section, resized to 4096 entries:
        startumost, startdmost  - written by setview/displayrooms
        umost, dmost            - written by drawrooms BEFORE polymost_drawrooms(),
                                  i.e. on every frame in GL mode
        radarang2               - written by dosetaspect() for xdimen columns
     Without this, each is overwritten 880 entries past its end every frame:
     umost spills into prevspritestat[] (corrupts the sprite status lists -> the
     ms()/moveeffectors crash), dmost into validmode[], radarang2 into spritesz[].
  5. Redirects the entry point through a stub that loads duke3dmod.dll from the exe's own folder.

Verified against Ghidra + duke3d.pdb; every immediate is replaced by exact count and
the result is swept to prove no stale reference to an old array base remains.
"""
import argparse, hashlib, os, shutil, struct, sys

# Megaton Edition 1.4.9 STABLE (Steam) duke3d.exe, and what this script makes of it. install.ps1 and uninstall.ps1
# record the same output hash; tools/test_patch.py proves all three agree.
STOCK_SHA256   = "0782CF3803B847F55EC06C031EB6046C0F9EDD1B49B31DB30673254B99704D45"
PATCHED_SHA256 = "A81893835533D0B4A234E014C65E6BE7A00D962D6B9F1992FAADC4469B04D4D0"
STOCK_SIZE = 2348032


def fail(msg):
    raise SystemExit(f"ABORT: {msg}")


def sha256(b):
    return hashlib.sha256(b).hexdigest().upper()


_here = os.path.dirname(os.path.abspath(__file__))
_default_src = os.path.join(_here, "duke3d.exe.orig")
if not os.path.exists(_default_src):
    _default_src = os.path.join(_here, "duke3d.exe")
ap = argparse.ArgumentParser(description="Patch the stock Megaton duke3d.exe (ultrawide fix + Nuke Panel loader).")
ap.add_argument("src", nargs="?", default=_default_src, help="the stock exe (default: duke3d.exe.orig beside this)")
ap.add_argument("dst", nargs="?", default=os.path.join(_here, "duke3d_patched.exe"), help="where to write the result")
ap.add_argument("--force", action="store_true", help="replace an existing output, keeping it as <output>.bak first")
args = ap.parse_args()
SRC, DST = args.src, args.dst
print(f"input:  {SRC}")
print(f"output: {DST}")

# ---------------- output refusals, before any work: the stock exe must survive every mistake ----------------
if not os.path.isfile(SRC):
    fail(f"no input file at {SRC}")
if os.path.normcase(os.path.abspath(SRC)) == os.path.normcase(os.path.abspath(DST)) or \
        (os.path.exists(DST) and os.path.samefile(SRC, DST)):
    fail("the output is the input. Keep the stock exe as duke3d.exe.orig and write the patched exe beside it.")
BAK = DST + ".bak"
if os.path.lexists(DST + ".tmp"):
    fail(f"{DST}.tmp already exists; it is the scratch file the write goes through. Move it away first.")
if os.path.lexists(DST):
    if not os.path.isfile(DST) or os.path.islink(DST):
        fail(f"{DST} is not a plain file")
    if sha256(open(DST, "rb").read()) == STOCK_SHA256:
        fail(f"{DST} is a stock exe. Rename it to duke3d.exe.orig (or delete it if a stock .orig already exists) "
             "and patch from that.")
    if not args.force:
        fail(f"{DST} already exists. Pass --force to replace it (the old file is kept as {BAK}).")
    if os.path.lexists(BAK):
        fail(f"{BAK} already exists; move it away first so no backup is overwritten.")

# ---------------- input: exactly the stock exe ----------------
data = bytearray(open(SRC, "rb").read())
if len(data) != STOCK_SIZE:
    fail(f"input is {len(data)} bytes, expected the stock {STOCK_SIZE}. "
         "Point this at duke3d.exe.orig (the untouched original).")
if sha256(data) != STOCK_SHA256:
    fail(f"input SHA-256 is {sha256(data)}, expected the stock Megaton 1.4.9 exe {STOCK_SHA256}.")

# ---------------- PE parsing ----------------
pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe:pe+4] != b"PE\0\0":
    fail("no PE signature")
nsec = struct.unpack_from("<H", data, pe+6)[0]
opt_size = struct.unpack_from("<H", data, pe+20)[0]
opt = pe + 24
if struct.unpack_from("<H", data, opt)[0] != 0x10B:
    fail("not PE32")
image_base  = struct.unpack_from("<I", data, opt+28)[0]
sect_align  = struct.unpack_from("<I", data, opt+32)[0]
file_align  = struct.unpack_from("<I", data, opt+36)[0]
size_of_image_off = opt + 56
size_of_headers   = struct.unpack_from("<I", data, opt+60)[0]
sects_off = opt + opt_size

sections = []
for i in range(nsec):
    o = sects_off + i*40
    vsize, rva, rsize, rptr = struct.unpack_from("<IIII", data, o+8)
    sections.append(dict(name=data[o:o+8].rstrip(b"\0").decode(),
                         vsize=vsize, rva=rva, rsize=rsize, rptr=rptr))

def rva2off(rva):
    for s in sections:
        if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["rsize"]):
            off = rva - s["rva"]
            if off >= s["rsize"]:
                raise ValueError(f"RVA {rva:#x} is in uninitialised data of {s['name']}")
            return s["rptr"] + off
    raise ValueError(f"RVA {rva:#x} not mapped")

def va2off(va):
    return rva2off(va - image_base)

def cstr(off):
    return bytes(data[off:data.index(b"\0", off)])

text = next(s for s in sections if s["name"] == ".text")
TEXT_START, TEXT_END = text["rptr"], text["rptr"] + text["rsize"]
print(f"ImageBase={image_base:#x}  .text file range {TEXT_START:#x}-{TEXT_END:#x}")

# ---------------- what the stub relies on, re-read from this exe's own header and import table ----------------
STOCK_ENTRY_RVA = 0x0018AAE2
RVA_IAT_GETMODULEHANDLEW = 0x01B19EC0   # KERNEL32!GetModuleHandleW import slot
RVA_IAT_GETPROCADDRESS   = 0x01B19E64   # KERNEL32!GetProcAddress import slot

def import_slot(dll_name, func_name):
    """`func_name`'s IAT slot RVA under `dll_name`, walked from the import directory."""
    import_rva = struct.unpack_from("<I", data, opt + 96 + 8)[0]
    d = rva2off(import_rva)
    while any(data[d:d+20]):
        oft, _tds, _fc, name_rva, ft = struct.unpack_from("<IIIII", data, d)
        if cstr(rva2off(name_rva)).decode().lower() == dll_name.lower():
            j = 0
            while True:
                thunk = struct.unpack_from("<I", data, rva2off(oft or ft) + j*4)[0]
                if thunk == 0:
                    return None
                if not thunk & 0x80000000 and cstr(rva2off(thunk) + 2).decode(errors="replace") == func_name:
                    return ft + j*4
                j += 1
        d += 20
    return None

entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
if entry_rva != STOCK_ENTRY_RVA:
    fail(f"entry point is {entry_rva:#x}, expected the stock {STOCK_ENTRY_RVA:#x}")
for fn, want in (("GetModuleHandleW", RVA_IAT_GETMODULEHANDLEW), ("GetProcAddress", RVA_IAT_GETPROCADDRESS)):
    got = import_slot("KERNEL32.dll", fn)
    if got != want:
        fail(f"KERNEL32!{fn} import slot is {got if got is None else hex(got)}, expected {want:#x}")

# ---------------- new sections: code (execute+read) and arrays (read+write), never both ----------------
def align(v, a): return (v + a - 1) & ~(a - 1)
last = max(sections, key=lambda s: s["rva"])
CODE_SIZE = 0x1000
DATA_SIZE = 0xA000
code_rva  = align(last["rva"] + max(last["vsize"], last["rsize"]), sect_align)
data_rva  = code_rva + align(CODE_SIZE, sect_align)
code_rptr = align(len(data), file_align)
data_rptr = code_rptr + CODE_SIZE
if CODE_SIZE % file_align or DATA_SIZE % file_align:
    fail("new section sizes are not multiples of the file alignment")
code_va, data_va = image_base + code_rva, image_base + data_rva

# .uwtext layout
OFF_CAVE     = 0x000
OFF_LOADER   = 0x100
OFF_DLLNAME  = 0x200
OFF_PROCNAME = 0x220
OFF_K32NAME  = 0x240
# .uwdata layout: short[4096] each
OFF_STARTUMOST, OFF_STARTDMOST, OFF_UMOST, OFF_DMOST, OFF_RADAR = 0x0000, 0x2000, 0x4000, 0x6000, 0x8000
NEW_ARRAY_BYTES = 4096 * 2
va_cave = code_va + OFF_CAVE

MOD_DLL_NAME  = b"duke3dmod.dll\0"
PROC_NAME     = b"LoadLibraryExA\0"
K32_NAME      = "kernel32.dll\0".encode("utf-16-le")
# The exe's folder for the dll, System32 for its glu32 dependency (the exe does not load glu32 itself); never the
# current directory or PATH, which a bare-name LoadLibraryA would search once the dll is removed.
LOAD_FLAGS = 0x200 | 0x800        # LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32

hdr_end = sects_off + (nsec+2)*40
if not (hdr_end <= size_of_headers and all(s["rptr"] >= hdr_end for s in sections)):
    fail("no room in the PE header for two more section entries")

# ---------------- code cave: width cap 3440 for bpp>8, else stock 2560 ----------------
RET_VA = 0x00494275
cave = bytearray()
cave += b"\x8B\x55\x08"                 # mov edx,[ebp+8]          ; &width
cave += b"\xB9\x00\x0A\x00\x00"         # mov ecx,2560
cave += b"\x83\x7D\x10\x08"             # cmp dword [ebp+0x10],8   ; bpp
cave += b"\x7E\x05"                     # jle keep 2560
cave += b"\xB9\x70\x0D\x00\x00"         # mov ecx,3440
cave += b"\x39\x0A"                     # cmp [edx],ecx
cave += b"\x7E\x02"                     # jle no_clamp
cave += b"\x89\x0A"                     # mov [edx],ecx
cave += b"\xE9" + struct.pack("<i", RET_VA - (va_cave + len(cave) + 5))

code = bytearray(CODE_SIZE)
code[OFF_CAVE:OFF_CAVE+len(cave)] = cave
marker = b"UWFIX v3 3440x1440 borderless + MAXXDIM array relocation\0"
code[OFF_CAVE+len(cave)+16 : OFF_CAVE+len(cave)+16+len(marker)] = marker

# ---- DLL loader stub (position independent: the exe has ASLR on and these sections carry no relocations, so no
# ---- absolute address may be used). A missing kernel32 handle or LoadLibraryExA skips the load: the game runs stock.
loader_rva = code_rva + OFF_LOADER

def lea_ebx(reg_modrm, off):
    return b"\x8D" + bytes([reg_modrm]) + struct.pack("<I", code_rva + off)

stub = bytearray()
stub += b"\x60"                                         # pushad
stub += b"\xE8\x00\x00\x00\x00"                         # call $+5   (pushes its own address)
here_rva = loader_rva + len(stub)                       # RVA of the instruction after the call
stub += b"\x5B"                                         # pop ebx    -> ebx = VA of `here`
stub += b"\x81\xEB" + struct.pack("<I", here_rva)       # sub ebx, here_rva -> ebx = image base
stub += lea_ebx(0x83, OFF_K32NAME)                      # lea eax, [ebx + L"kernel32.dll"]
stub += b"\x50"                                         # push eax
stub += b"\xFF\x93" + struct.pack("<I", RVA_IAT_GETMODULEHANDLEW)   # call [ebx + iat]
stub += b"\x85\xC0"                                     # test eax,eax
jz1 = len(stub); stub += b"\x74\x00"                    # jz done
stub += lea_ebx(0x8B, OFF_PROCNAME)                     # lea ecx, [ebx + "LoadLibraryExA"]
stub += b"\x51\x50"                                     # push ecx; push eax
stub += b"\xFF\x93" + struct.pack("<I", RVA_IAT_GETPROCADDRESS)     # call [ebx + iat]
stub += b"\x85\xC0"                                     # test eax,eax
jz2 = len(stub); stub += b"\x74\x00"                    # jz done
stub += b"\x68" + struct.pack("<I", LOAD_FLAGS)         # push flags
stub += b"\x6A\x00"                                     # push 0 (hFile)
stub += lea_ebx(0x8B, OFF_DLLNAME)                      # lea ecx, [ebx + "duke3dmod.dll"]
stub += b"\x51"                                         # push ecx
stub += b"\xFF\xD0"                                     # call eax   (LoadLibraryExA)
done = len(stub)
for j in (jz1, jz2):
    stub[j+1] = done - (j + 2)
stub += b"\x61"                                         # popad
jmp_from = loader_rva + len(stub) + 5
stub += b"\xE9" + struct.pack("<i", entry_rva - jmp_from)           # jmp original entry

# ---- layout guardrails: every region stays clear of its neighbour, so a future edit to the cave, marker, stub or an
# ---- OFF_* constant fails here instead of corrupting the stub or an engine array at runtime. Ends come from actual
# ---- lengths, never literals.
def region_ok(name, end, nxt_name, nxt_start):
    if end > nxt_start:
        fail(f"{name} (ends {end:#x}) collides with {nxt_name} (starts {nxt_start:#x})")

region_ok("cave+gap+marker", OFF_CAVE + len(cave) + 16 + len(marker), "the loader stub", OFF_LOADER)
region_ok("loader stub", OFF_LOADER + len(stub), "the dll name", OFF_DLLNAME)
region_ok("dll name", OFF_DLLNAME + len(MOD_DLL_NAME), "the proc name", OFF_PROCNAME)
region_ok("proc name", OFF_PROCNAME + len(PROC_NAME), "the kernel32 name", OFF_K32NAME)
region_ok("kernel32 name", OFF_K32NAME + len(K32_NAME), "the end of .uwtext", CODE_SIZE)
region_ok("startumost array", OFF_STARTUMOST + NEW_ARRAY_BYTES, "the startdmost array", OFF_STARTDMOST)
region_ok("startdmost array", OFF_STARTDMOST + NEW_ARRAY_BYTES, "the umost array", OFF_UMOST)
region_ok("umost array", OFF_UMOST + NEW_ARRAY_BYTES, "the dmost array", OFF_DMOST)
region_ok("dmost array", OFF_DMOST + NEW_ARRAY_BYTES, "the radarang2 array", OFF_RADAR)
region_ok("radarang2 array", OFF_RADAR + NEW_ARRAY_BYTES, "the end of .uwdata", DATA_SIZE)

code[OFF_LOADER:OFF_LOADER + len(stub)] = stub
code[OFF_DLLNAME:OFF_DLLNAME + len(MOD_DLL_NAME)] = MOD_DLL_NAME
code[OFF_PROCNAME:OFF_PROCNAME + len(PROC_NAME)] = PROC_NAME
code[OFF_K32NAME:OFF_K32NAME + len(K32_NAME)] = K32_NAME
struct.pack_into("<I", data, opt + 16, loader_rva)  # AddressOfEntryPoint -> our stub
print(f"  loader stub at rva {loader_rva:#x}, entry point redirected from {entry_rva:#x}")

# ---------------- code patches ----------------
def poke(va, expect: bytes, new: bytes, label):
    off = va2off(va)
    if len(expect) != len(new):
        fail(f"{label}: patch length mismatch")
    if bytes(data[off:off+len(expect)]) != expect:
        fail(f"{label}: bytes at {va:#x} are {data[off:off+len(expect)].hex()}, expected {expect.hex()}")
    data[off:off+len(new)] = new
    print(f"  {label}")

print("code patches:")
# 1. getvalidmodes: JGE -> JG  (mode.w < desktop.w  ->  <=)
poke(0x004940FF, b"\x7D\x14", b"\x7F\x14", "getvalidmodes: allow desktop-width windowed modes")
# 2. checkvideomode: replace the unconditional 2560 width clamp with a jump to the cave
poke(0x00494261,
     bytes.fromhex("8b5508" "813a000a0000" "7e09" "8b4508" "c700000a0000"),
     b"\xE9" + struct.pack("<i", va_cave - (0x00494261 + 5)) + b"\x90"*15,
     f"checkvideomode: width cap -> cave @ {va_cave:#x} (3440 when bpp>8)")
# 3. setvideomode: windowed GL flags |= SDL_WINDOW_BORDERLESS (0x10)
poke(0x00494858, b"\x83\xC8\x04", b"\x83\xC8\x14", "setvideomode: windowed GL window is borderless")

# ---------------- array relocation ----------------
# name -> (old_base, byte_size, new_offset in .uwdata, {old_immediate: exact_expected_count})
# Immediate lists come from Ghidra's full reference scan of each array's whole extent.
ARRAYS = {
    "startumost": (0x01E99780, 5120, OFF_STARTUMOST, {0x01E99780: 22}),
    "startdmost": (0x01ED5040, 5120, OFF_STARTDMOST, {0x01ED5040: 22}),
    "umost":      (0x01EE5C40, 5120, OFF_UMOST,      {0x01EE5C40: 56, 0x01EE5C42: 6}),
    "dmost":      (0x01E8C020, 5120, OFF_DMOST,      {0x01E8C020: 47}),
    "radarang2":  (0x005EE0D8, 5120, OFF_RADAR,      {0x005EE0D8: 3}),
}

# An array address can also occur in .text as a coincidental byte sequence (e.g. a
# PUSH+CALL rel32 pair). Only rewrite occurrences that are genuine x86 disp32/imm32
# operands; every one of these forms was confirmed present in this binary.
SIB_DISP32   = {0x45, 0x4d, 0x55, 0x5d, 0x65, 0x6d, 0x75, 0x7d}   # SIB base=101 -> disp32
MODRM_INDEX  = {0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c}   # mod=00 rm=100 (SIB follows)
MODRM_DIRECT = {0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d}   # mod=00 rm=101 -> disp32

def is_operand(i):
    """True if the dword at file offset i is a real instruction operand, not stray bytes."""
    prev = data[i-1]
    if prev in SIB_DISP32 and data[i-2] in MODRM_INDEX:
        return True                                    # [reg*scale + disp32]
    if prev in MODRM_DIRECT:
        return True                                    # [disp32]
    if data[i-3] == 0xC7 and data[i-2] == 0x45:
        return True                                    # mov [ebp+d8], imm32
    if 0xB8 <= prev <= 0xBF or prev == 0x68:
        return True                                    # mov r32,imm32 / push imm32
    if prev in (0xA1, 0xA3):
        return True                                    # mov eax,[disp32] / mov [disp32],eax
    return False

def find_operands(val):
    pat, hits, i = struct.pack("<I", val), [], TEXT_START
    while True:
        i = data.find(pat, i, TEXT_END)
        if i < 0:
            return hits
        if is_operand(i):
            hits.append(i)
        i += 1

def replace_imm(old_val, new_val, expected, label):
    hits = find_operands(old_val)
    if len(hits) != expected:
        fail(f"{label}: found {len(hits)} genuine operands referencing {old_val:#x}, expected exactly {expected}")
    rep = struct.pack("<I", new_val)
    for h in hits:
        data[h:h+4] = rep
    print(f"  {label}: {old_val:#x} -> {new_val:#x}  ({len(hits)} refs)")

print("array relocation (2560 -> 4096 entries):")
for name, (base, size, new_off, imms) in ARRAYS.items():
    for old_imm, count in imms.items():
        replace_imm(old_imm, data_va + new_off + (old_imm - base), count, name)

# sweep: no instruction may still address the old storage of a relocated array
print("verifying no stale references remain:")
for name, (base, size, _off, _imms) in ARRAYS.items():
    stale = [(va, h) for va in range(base, base + size) for h in find_operands(va)]
    if stale:
        fail(f"{len(stale)} stale reference(s) into old {name}: "
             + ", ".join(f"{v:#x}@file{o:#x}" for v, o in stale[:8]))
    print(f"  {name}: clean")

# ---------------- append the sections ----------------
data.extend(b"\0" * (code_rptr - len(data)))
data.extend(code)
data.extend(b"\0" * DATA_SIZE)
struct.pack_into("<8sIIIIIIHHI", data, sects_off + nsec*40,
                 b".uwtext\0", CODE_SIZE, code_rva, CODE_SIZE, code_rptr,
                 0, 0, 0, 0, 0x60000020)          # CODE|EXEC|READ
struct.pack_into("<8sIIIIIIHHI", data, sects_off + (nsec+1)*40,
                 b".uwdata\0", DATA_SIZE, data_rva, DATA_SIZE, data_rptr,
                 0, 0, 0, 0, 0xC0000040)          # INITDATA|READ|WRITE
struct.pack_into("<H", data, pe+6, nsec+2)
struct.pack_into("<I", data, size_of_image_off, align(data_rva + DATA_SIZE, sect_align))
struct.pack_into("<I", data, opt+64, 0)            # checksum: not validated for exes

# The output must be the exact exe install.ps1 accepts; anything else means this script or its input drifted.
if sha256(data) != PATCHED_SHA256:
    fail(f"output SHA-256 is {sha256(data)}, expected {PATCHED_SHA256}; nothing written.")

# Written beside the output and moved into place, so a failure never leaves a half-written exe.
tmp = DST + ".tmp"
with open(tmp, "xb") as f:      # exclusive: never writes through an existing file or link
    f.write(data)
if os.path.lexists(DST):
    shutil.copyfile(DST, BAK)
    print(f"kept the old output as {BAK}")
os.replace(tmp, DST)
print(f"\nwritten {DST}")
print(f"  {len(data)} bytes, sha256 {PATCHED_SHA256}")
print(f"  .uwtext rva={code_rva:#x} (execute+read), .uwdata rva={data_rva:#x} (read+write)")
for n, (b, s, o, _i) in ARRAYS.items():
    print(f"  {n:11s} {b:#x} -> {data_va + o:#x}  (4096 entries)")
