#pragma once

// The Windows clipboard, DLL-only; entry.h holds the pure rules. Fail-closed: held by another process or format absent
// answers false and leaves the field as it was. Never throws; writes no engine memory.

bool clipboard_read_text(char* out, int cap);

bool clipboard_write_text(const char* s);
