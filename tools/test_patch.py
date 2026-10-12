#!/usr/bin/env python3
"""Verifies the patcher: reproducible output, a correct ASLR-safe DLL-load stub, a non-writable code section, and the
refusals that keep the stock exe safe.

The stub decode walks every instruction and proves each operand: the image-base derivation (sub ebx,imm32), the three
string pointers (lea reg,[ebx+imm32]), the GetModuleHandleW and GetProcAddress IAT slots (call [ebx+imm32]), the
search flags, both skip branches, and the jump back to the original entry point. Entry point and IAT slots are read
from the STOCK exe's own header and import table at test time, never trusted from the patcher. A decode that consumes
the stub's full length with every check passing leaves no byte in which an absolute operand could hide.

Every file this writes lives in a fresh temp directory, deleted at the end; the stock exe given is only read.
"""
import hashlib, os, re, shutil, struct, subprocess, sys, tempfile

HERE  = os.path.dirname(os.path.abspath(__file__))
STOCK = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("DUKE3D_STOCK_EXE")
if not STOCK:
    print("usage: python test_patch.py <stock duke3d.exe>   (or set DUKE3D_STOCK_EXE)")
    sys.exit(2)
PATCHER = os.path.join(HERE, "ultrawide_patch.py")

LOAD_LIBRARY_SEARCH_APPLICATION_DIR = 0x200
LOAD_LIBRARY_SEARCH_SYSTEM32        = 0x800
SCN_MEM_EXECUTE, SCN_MEM_WRITE      = 0x20000000, 0x80000000


def run(*args):
    """Runs the patcher; returns (exit code, combined output)."""
    p = subprocess.run([sys.executable, PATCHER, *args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest().upper()


class Pe:
    """Minimal PE32 reader shared by every check below."""
    def __init__(self, data):
        self.d = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe+4] != b"PE\0\0":
            raise SystemExit("not a PE file")
        self.opt = pe + 24
        opt_size = struct.unpack_from("<H", data, pe + 20)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        self.image_base = struct.unpack_from("<I", data, self.opt + 28)[0]
        self.entry_rva  = struct.unpack_from("<I", data, self.opt + 16)[0]
        self.num_dirs   = struct.unpack_from("<I", data, self.opt + 92)[0]
        self.dirs_off   = self.opt + 96
        sects_off = self.opt + opt_size
        self.sections = []
        for i in range(nsec):
            o = sects_off + i * 40
            name = data[o:o+8].rstrip(b"\0").decode()
            vs, rva, rs, rp = struct.unpack_from("<IIII", data, o + 8)
            chars = struct.unpack_from("<I", data, o + 36)[0]
            self.sections.append((name, vs, rva, rs, rp, chars))

    def section(self, name):
        return next((s for s in self.sections if s[0] == name), None)

    def rva2off(self, rva):
        for name, vs, srva, rs, rp, _c in self.sections:
            if srva <= rva < srva + max(vs, rs):
                return rp + (rva - srva)
        raise ValueError(f"rva {rva:#x} not mapped in any section")

    def cstr(self, off):
        return self.d[off:self.d.index(b"\0", off)]


def derive_iat_rva(pe: Pe, dll_name: str, func_name: str) -> int:
    """`func_name`'s IAT slot RVA under `dll_name`, from `pe`'s own import directory."""
    import_rva, _size = struct.unpack_from("<II", pe.d, pe.dirs_off + 1 * 8)
    imp_off = pe.rva2off(import_rva)
    i = 0
    while True:
        entry = pe.d[imp_off + i*20: imp_off + i*20 + 20]
        if entry == b"\0" * 20:
            raise ValueError(f"{dll_name} import descriptor not found")
        orig_first_thunk, _tds, _fc, name_rva, first_thunk = struct.unpack("<IIIII", entry)
        if pe.cstr(pe.rva2off(name_rva)).decode().lower() == dll_name.lower():
            thunk_src = orig_first_thunk if orig_first_thunk else first_thunk
            j = 0
            while True:
                thunk_val = struct.unpack_from("<I", pe.d, pe.rva2off(thunk_src) + j * 4)[0]
                if thunk_val == 0:
                    raise ValueError(f"{dll_name}!{func_name} not found")
                if not (thunk_val & 0x80000000) and \
                        pe.cstr(pe.rva2off(thunk_val) + 2).decode(errors="replace") == func_name:
                    return first_thunk + j * 4
                j += 1
        i += 1


failures = []
def check(cond, what, detail=""):
    (print(f"  OK   {what}: {detail}") if cond else failures.append(f"{what}: {detail}"))


def recorded_hash(script):
    text = open(os.path.join(HERE, script), encoding="utf-8").read()
    m = re.search(r"ExpectedExeHash\s*=\s*'([0-9A-Fa-f]{64})'", text)
    return m.group(1).upper() if m else None


tmp = tempfile.mkdtemp(prefix="nukepanel_patch_")
try:
    stock_copy = os.path.join(tmp, "duke3d.exe.orig")
    shutil.copyfile(STOCK, stock_copy)
    stock_bytes = open(stock_copy, "rb").read()

    # ---------------- reproducible, and the hash both scripts record ----------------
    out_a, out_b = os.path.join(tmp, "a.exe"), os.path.join(tmp, "b.exe")
    rc_a, log_a = run(stock_copy, out_a)
    rc_b, _ = run(stock_copy, out_b)
    if rc_a or rc_b or not os.path.exists(out_a):
        print(log_a)
        raise SystemExit("FAIL: the patcher did not produce an output from the stock exe")
    a = open(out_a, "rb").read()
    check(a == open(out_b, "rb").read(), "byte-reproducible")
    out_hash = hashlib.sha256(a).hexdigest().upper()
    check(recorded_hash("install.ps1") == out_hash, "install.ps1 ExpectedExeHash is the output's hash", out_hash)
    check(recorded_hash("uninstall.ps1") == out_hash, "uninstall.ps1 ExpectedExeHash is the output's hash", out_hash)

    # ---------------- sections: code is not writable, data is not executable ----------------
    patched = Pe(a)
    code, data_s = patched.section(".uwtext"), patched.section(".uwdata")
    check(code is not None and data_s is not None, "both new sections exist", f"{[s[0] for s in patched.sections]}")
    check(patched.section(".uwfix") is None, "no single writable+executable .uwfix section remains")
    if code and data_s:
        check(code[5] & SCN_MEM_EXECUTE and not code[5] & SCN_MEM_WRITE, ".uwtext is execute+read, not writable",
              f"{code[5]:#010x}")
        check(data_s[5] & SCN_MEM_WRITE and not data_s[5] & SCN_MEM_EXECUTE, ".uwdata is read+write, not executable",
              f"{data_s[5]:#010x}")
        check(code[2] <= patched.entry_rva < code[2] + code[1], "entry redirected into .uwtext",
              f"{patched.entry_rva:#x}")

    # ---------------- stub decode ----------------
    stock = Pe(stock_bytes)
    iat_gmh = derive_iat_rva(stock, "KERNEL32.dll", "GetModuleHandleW")
    iat_gpa = derive_iat_rva(stock, "KERNEL32.dll", "GetProcAddress")
    print(f"stock IAT: GetModuleHandleW {iat_gmh:#x}, GetProcAddress {iat_gpa:#x}; entry {stock.entry_rva:#x}")

    e = patched.entry_rva
    stub = a[patched.rva2off(e):patched.rva2off(e) + 0x80]
    i = 0

    def take(n):
        global i
        b = stub[i:i+n]
        i += n
        return b

    def string_at(rva, n):
        try:
            off = patched.rva2off(rva)
        except ValueError:
            return b""
        return a[off:off+n]

    check(take(1) == b"\x60", "pushad")
    check(take(5) == b"\xE8\x00\x00\x00\x00", "call $+5")
    here_rva = e + i
    check(take(1) == b"\x5B", "pop ebx")
    check(take(2) == b"\x81\xEB", "sub ebx,imm32")
    check(struct.unpack("<I", take(4))[0] == here_rva, "sub operand makes ebx the image base")
    check(take(2) == b"\x8D\x83", "lea eax,[ebx+imm32]")
    k32 = struct.unpack("<I", take(4))[0]
    check(string_at(k32, 26) == "kernel32.dll\0".encode("utf-16-le"), "lea operand is L\"kernel32.dll\"")
    check(take(1) == b"\x50", "push eax")
    check(take(2) == b"\xFF\x93", "call [ebx+imm32] (GetModuleHandleW)")
    check(struct.unpack("<I", take(4))[0] == iat_gmh, "GetModuleHandleW IAT slot from the stock import table")
    check(take(2) == b"\x85\xC0", "test eax,eax")
    check(take(1) == b"\x74", "jz skip (no kernel32 handle)")
    skip1 = i + 1 + struct.unpack("<b", take(1))[0]
    check(take(2) == b"\x8D\x8B", "lea ecx,[ebx+imm32]")
    proc = struct.unpack("<I", take(4))[0]
    check(string_at(proc, 15) == b"LoadLibraryExA\0", "lea operand is \"LoadLibraryExA\"")
    check(take(1) == b"\x51", "push ecx")
    check(take(1) == b"\x50", "push eax")
    check(take(2) == b"\xFF\x93", "call [ebx+imm32] (GetProcAddress)")
    check(struct.unpack("<I", take(4))[0] == iat_gpa, "GetProcAddress IAT slot from the stock import table")
    check(take(2) == b"\x85\xC0", "test eax,eax")
    check(take(1) == b"\x74", "jz skip (no LoadLibraryExA): never falls back to a PATH search")
    skip2 = i + 1 + struct.unpack("<b", take(1))[0]
    check(take(1) == b"\x68", "push imm32 (search flags)")
    flags = struct.unpack("<I", take(4))[0]
    check(flags == LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32,
          "flags: app dir for the dll, System32 for its glu32 dependency; no cwd, no PATH", f"{flags:#x}")
    check(take(2) == b"\x6A\x00", "push 0 (hFile)")
    check(take(2) == b"\x8D\x8B", "lea ecx,[ebx+imm32]")
    name = struct.unpack("<I", take(4))[0]
    check(string_at(name, 14) == b"duke3dmod.dll\0", "lea operand is \"duke3dmod.dll\"")
    check(take(1) == b"\x51", "push ecx")
    check(take(2) == b"\xFF\xD0", "call eax (LoadLibraryExA)")
    check(skip1 == i and skip2 == i, "both skips land on popad", f"{skip1:#x} {skip2:#x} vs {i:#x}")
    check(take(1) == b"\x61", "popad")
    check(take(1) == b"\xE9", "jmp rel32")
    rel = struct.unpack("<i", take(4))[0]
    check(e + i + rel == stock.entry_rva, "jmp returns to the stock entry point", f"{e + i + rel:#x}")

    # ---------------- refusals ----------------
    print("refusals:")
    rc, _ = run(stock_copy, stock_copy)
    check(rc != 0 and open(stock_copy, "rb").read() == stock_bytes, "refuses output == input, input untouched")
    rc, _ = run(stock_copy, stock_copy, "--force")
    check(rc != 0 and open(stock_copy, "rb").read() == stock_bytes, "refuses output == input even with --force")

    existing = os.path.join(tmp, "existing.exe")
    open(existing, "wb").write(b"older patched exe")
    rc, _ = run(stock_copy, existing)
    check(rc != 0 and open(existing, "rb").read() == b"older patched exe", "refuses an existing output without --force")
    rc, out = run(stock_copy, existing, "--force")
    check(rc == 0 and open(existing, "rb").read() == a, "--force replaces the output", out.strip()[-120:])
    check(os.path.exists(existing + ".bak") and open(existing + ".bak", "rb").read() == b"older patched exe",
          "--force kept the old output as <out>.bak")
    rc, _ = run(stock_copy, existing, "--force")
    check(rc != 0 and open(existing + ".bak", "rb").read() == b"older patched exe",
          "refuses --force when <out>.bak already exists (never overwrites a backup)")

    stock_as_out = os.path.join(tmp, "duke3d.exe")
    shutil.copyfile(stock_copy, stock_as_out)
    rc, _ = run(stock_copy, stock_as_out, "--force")
    check(rc != 0 and open(stock_as_out, "rb").read() == stock_bytes and not os.path.exists(stock_as_out + ".bak"),
          "refuses to write over a stock exe, even with --force")

    tampered = os.path.join(tmp, "tampered.exe")
    tb = bytearray(stock_bytes)
    tb[0x400] ^= 0xFF                                  # same size, first byte of .text changed
    open(tampered, "wb").write(tb)
    fresh = os.path.join(tmp, "fresh.exe")
    rc, out = run(tampered, fresh)
    check(rc != 0 and not os.path.exists(fresh) and "SHA-256" in out, "refuses an input that is not the stock exe")

    pre_tmp = os.path.join(tmp, "pre.exe")
    open(pre_tmp + ".tmp", "wb").write(b"someone's file")
    rc, _ = run(stock_copy, pre_tmp)
    check(rc != 0 and open(pre_tmp + ".tmp", "rb").read() == b"someone's file" and not os.path.exists(pre_tmp),
          "refuses when <out>.tmp already exists, file untouched")
    os.remove(pre_tmp + ".tmp")

    leftovers = [n for n in os.listdir(tmp) if n.endswith(".tmp")]
    check(not leftovers, "no .tmp file left behind", f"{leftovers}")

    # Safety checks must survive python -O, which strips assert statements.
    src = open(PATCHER, encoding="utf-8").read()
    check(not re.search(r"^\s*assert\b", src, re.M), "the patcher has no assert statements")
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print("\nFAILED:")
    for f in failures:
        print("  FAIL", f)
    sys.exit(1)
print("PASS")
