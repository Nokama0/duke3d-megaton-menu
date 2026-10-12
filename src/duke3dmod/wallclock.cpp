#include <windows.h>
#include "wallclock.h"

// QueryPerformanceCounter: a 10-16 ms GetTickCount step would stutter the 750 ms damage-number fade.
// Whole seconds plus remainder: ticks * 1000 overflows int64 at ~35 days uptime.
unsigned wallclock_ms() {
  static LARGE_INTEGER freq = { 0 };
  static bool asked = false;
  if (!asked) {
    asked = true;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0) freq.QuadPart = 1;   // failed read: raw ticks, never divide by zero
  }
  LARGE_INTEGER now; QueryPerformanceCounter(&now);
  const long long whole = now.QuadPart / freq.QuadPart;
  const long long rem   = now.QuadPart % freq.QuadPart;
  return (unsigned)(whole * 1000 + (rem * 1000) / freq.QuadPart);
}
