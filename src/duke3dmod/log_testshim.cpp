// Test-build stand-in for log.cpp (no file I/O). Records ops so a caller that never logs fails a check.
#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static LogOp s_ops[LOG_RECORD_MAX];
static int  s_opCount = 0;
static bool s_recording = false;
static bool s_overflowed = false;
// Starts true (unlike log.cpp): nothing in the test build calls log_init, and false would gate out every LOGI check.
static bool s_enabled = true;

void log_record_begin() { s_opCount = 0; s_overflowed = false; s_recording = true; }
void log_record_end()   { s_recording = false; }
int  log_record_count() { return s_opCount; }
bool log_record_overflowed() { return s_overflowed; }
const LogOp* log_record_op(int i) {
  return (i >= 0 && i < s_opCount) ? &s_ops[i] : nullptr;
}
const LogOp* log_record_find(const char* text) {
  if (!text) return nullptr;
  for (int i = 0; i < s_opCount; i++)
    if (strcmp(s_ops[i].text, text) == 0) return &s_ops[i];
  return nullptr;
}

// log_write still honours s_enabled, as in the DLL.
void log_init(const char*, bool enabled) { s_enabled = enabled; }
void log_set_enabled(bool enabled) { s_enabled = enabled; }

// Overflow sets a flag: a truncated recording would let a "never logged" check pass for the wrong reason.
void log_write(const char* level, const char* fmt, ...) {
  if (!s_enabled) return;
  if (!s_recording) return;
  if (s_opCount >= LOG_RECORD_MAX) { s_overflowed = true; return; }
  LogOp& op = s_ops[s_opCount];
  strncpy_s(op.level, level ? level : "", _TRUNCATE);
  va_list ap; va_start(ap, fmt);
  _vsnprintf_s(op.text, sizeof(op.text), _TRUNCATE, fmt, ap);
  va_end(ap);
  s_opCount++;
}
