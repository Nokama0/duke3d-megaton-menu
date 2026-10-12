#pragma once
void log_init(const char* path, bool enabled);
void log_write(const char* level, const char* fmt, ...);
#define LOGI(...) log_write("INFO", __VA_ARGS__)
#define LOGE(...) log_write("ERR ", __VA_ARGS__)

// Turns logging on or off; takes effect on the next line.
void log_set_enabled(bool enabled);

#ifdef NO_GL
// TEST-BUILD-ONLY: records what was logged, so a caller that computes a diff but never writes a line fails the suite.
const int LOG_RECORD_MAX = 256;
struct LogOp { char level[8]; char text[192]; };
void log_record_begin();
void log_record_end();
int  log_record_count();
bool log_record_overflowed();

const LogOp* log_record_op(int i);
const LogOp* log_record_find(const char* text);
#endif // NO_GL
