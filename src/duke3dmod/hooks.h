#pragma once
extern unsigned char* g_base;   // module base of duke3d.exe

// Replaces one IAT slot, after checking it holds a non-null pointer outside the exe image.
bool hook_iat(unsigned slotRva, void* newFn, void** origOut);

// Patches a 5-byte E9 JMP rel32 thunk; all five bytes must equal expected5 and be executable, else logs and returns
// false with bytes untouched.
bool hook_jmp_thunk(unsigned thunkRva, const unsigned char* expected5, void* newTarget, void** origTargetOut);

// Same for an inline `E8 rel32` CALL site (polymost_drawrooms' seed call to polymost_scansector). Install-time only.
bool hook_call_site(unsigned siteRva, const unsigned char* expected5, void* newTarget, void** origTargetOut);
