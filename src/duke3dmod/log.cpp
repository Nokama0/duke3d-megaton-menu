#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

static char s_path[MAX_PATH];
static bool s_enabled = false;
// Whether this session has truncated the file yet. When logging starts OFF and is switched on from the panel, the first
// write opens the file fresh, so a session that never turns logging on leaves the old file untouched.
static bool s_opened = false;
static SRWLOCK s_lock = SRWLOCK_INIT;

void log_init(const char* path, bool enabled) {
  AcquireSRWLockExclusive(&s_lock);
  strncpy_s(s_path, path, _TRUNCATE);
  s_enabled = enabled;
  s_opened = false;
  if (!enabled) { ReleaseSRWLockExclusive(&s_lock); return; }
  FILE* f = nullptr;
  if (fopen_s(&f, s_path, "w") == 0 && f) {
    fputs("Nuke Panel log\n", f);
    fclose(f);
    s_opened = true;
  }
  ReleaseSRWLockExclusive(&s_lock);
}

void log_set_enabled(bool enabled) {
  AcquireSRWLockExclusive(&s_lock);
  s_enabled = enabled;
  ReleaseSRWLockExclusive(&s_lock);
}

void log_write(const char* level, const char* fmt, ...) {
  AcquireSRWLockExclusive(&s_lock);
  if (!s_enabled) { ReleaseSRWLockExclusive(&s_lock); return; }
  // "w" on the first write of the session, "a" after.
  FILE* f = nullptr;
  if (fopen_s(&f, s_path, s_opened ? "a" : "w") != 0 || !f) {
    ReleaseSRWLockExclusive(&s_lock);
    return;
  }
  if (!s_opened) { fputs("Nuke Panel log\n", f); s_opened = true; }
  fprintf(f, "[%s] ", level);
  va_list ap; va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
  ReleaseSRWLockExclusive(&s_lock);
}
