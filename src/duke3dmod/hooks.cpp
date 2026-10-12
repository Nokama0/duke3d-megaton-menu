#include "hooks.h"
#include "common.h"
#include "log.h"
#include <windows.h>
#include <string.h>

unsigned char* g_base = nullptr;

// Reads this module's own PE headers to find SizeOfImage, so hook_iat checks a slot against the real image bounds
// rather than a single point. A non-PE base never happens for a live duke3d.exe, but a hook must not trust unverified
// memory.
static bool get_image_end(unsigned char* base, unsigned char** outEnd) {
  if (!base) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(base, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;

  unsigned e_lfanew = *(unsigned*)(base + 0x3C);
  unsigned char* peSig = base + e_lfanew;
  if (*(unsigned*)peSig != 0x00004550 /* 'PE\0\0' */) return false;

  unsigned char* optHeader = peSig + 4 /* PE signature */ + 20 /* COFF header */;
  unsigned sizeOfImage = *(unsigned*)(optHeader + 0x38);
  if (sizeOfImage == 0) return false;

  *outEnd = base + sizeOfImage;
  return true;
}

bool hook_iat(unsigned slotRva, void* newFn, void** origOut) {
  if (!g_base) return false;
  void** slot = (void**)(g_base + slotRva);

  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(slot, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
    LOGE("hook_iat: slot rva %08X is not committed memory", slotRva);
    return false;
  }

  // A slot on inaccessible or guard-paged memory would fault on the next dereference; refuse first.
  DWORD prot = mbi.Protect;
  DWORD baseProt = prot & 0xFF;
  bool readableProt = baseProt == PAGE_READONLY || baseProt == PAGE_READWRITE ||
                       baseProt == PAGE_EXECUTE_READ || baseProt == PAGE_EXECUTE_READWRITE ||
                       baseProt == PAGE_WRITECOPY || baseProt == PAGE_EXECUTE_WRITECOPY;
  if ((prot & PAGE_GUARD) || !readableProt) {
    LOGE("hook_iat: slot rva %08X has protect %08X, refusing", slotRva, prot);
    return false;
  }

  void* cur = *slot;

  // An import slot points into the exporting DLL: never null, never anywhere inside our own image (the real bounds).
  unsigned char* imageEnd = nullptr;
  if (!get_image_end(g_base, &imageEnd)) {
    LOGE("hook_iat: slot rva %08X could not read module PE headers, refusing", slotRva);
    return false;
  }
  if (!cur || ((unsigned char*)cur >= g_base && (unsigned char*)cur < imageEnd)) {
    LOGE("hook_iat: slot rva %08X holds %p inside image [%p,%p), refusing", slotRva, cur, g_base, imageEnd);
    return false;
  }

  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    LOGE("hook_iat: VirtualProtect failed for rva %08X", slotRva);
    return false;
  }
  if (origOut) *origOut = cur;
  *slot = newFn;
  VirtualProtect(slot, sizeof(void*), old, &old);
  LOGI("hook_iat: rva %08X %p -> %p", slotRva, cur, newFn);
  return true;
}

// Shared body of hook_jmp_thunk and hook_call_site: identical verification, rel32 arithmetic and all-or-nothing
// failure. Only the opcode byte (0xE9 vs 0xE8) and the log label differ, so both are parameters.
static bool hook_rel32_site(unsigned siteRva, const unsigned char* expected5,
                            void* newTarget, void** origTargetOut,
                            unsigned char newOpcode, const char* who) {
  if (!g_base) return false;
  unsigned char* site = g_base + siteRva;

  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(site, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
    LOGE("%s: rva %08X is not committed memory", who, siteRva);
    return false;
  }

  // This is code: require an executable and readable protection (the current 5 bytes are read before verifying) and
  // refuse a guard page, as hook_iat does.
  DWORD prot = mbi.Protect;
  DWORD baseProt = prot & 0xFF;
  bool execReadableProt = baseProt == PAGE_EXECUTE_READ || baseProt == PAGE_EXECUTE_READWRITE ||
                           baseProt == PAGE_EXECUTE_WRITECOPY;
  if ((prot & PAGE_GUARD) || !execReadableProt) {
    LOGE("%s: rva %08X has protect %08X, refusing", who, siteRva, prot);
    return false;
  }

  if (memcmp(site, expected5, 5) != 0) {
    LOGE("%s: rva %08X bytes %02X %02X %02X %02X %02X do not match "
         "expected %02X %02X %02X %02X %02X, refusing",
         who, siteRva, site[0], site[1], site[2], site[3], site[4],
         expected5[0], expected5[1], expected5[2], expected5[3], expected5[4]);
    return false;
  }

  // Recover the current target before writing: decode the rel32 (4 bytes after the opcode) relative to the byte after
  // this 5-byte instruction, via the pure helper tested in test_host (common.h). CALL and JMP rel32 share that rule.
  unsigned oldRel32;
  memcpy(&oldRel32, site + 1, 4);
  const unsigned origTarget = thunk_decode_target((unsigned)site, oldRel32);
  if (origTargetOut) *origTargetOut = (void*)origTarget;

  DWORD old = 0;
  if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
    LOGE("%s: VirtualProtect(writable) failed for rva %08X", who, siteRva);
    return false;
  }

  // One memcpy of the 5 bytes is not atomic on x86, so nothing may execute the site during install. Every caller
  // installs from mod_deferred_init on the game's one logic thread (inside hooked_poll), the only thread that reaches
  // these sites: argument at mod_deferred_init, dllmain.cpp. A caller installing elsewhere must supply its own.
  unsigned char newBytes[5];
  newBytes[0] = newOpcode;
  const unsigned newRel32 = thunk_encode_rel32((unsigned)site, (unsigned)newTarget);
  memcpy(newBytes + 1, &newRel32, 4);
  memcpy(site, newBytes, 5);

  DWORD ignore = 0;
  VirtualProtect(site, 5, old, &ignore);
  FlushInstructionCache(GetCurrentProcess(), site, 5);

  LOGI("%s: rva %08X patched, recovered original target %p (rva %08X) -> new target %p",
       who, siteRva, (void*)origTarget, (unsigned)(origTarget - (unsigned)g_base), newTarget);
  return true;
}

bool hook_jmp_thunk(unsigned thunkRva, const unsigned char* expected5, void* newTarget, void** origTargetOut) {
  return hook_rel32_site(thunkRva, expected5, newTarget, origTargetOut, 0xE9, "hook_jmp_thunk");
}

bool hook_call_site(unsigned siteRva, const unsigned char* expected5, void* newTarget, void** origTargetOut) {
  return hook_rel32_site(siteRva, expected5, newTarget, origTargetOut, 0xE8, "hook_call_site");
}
