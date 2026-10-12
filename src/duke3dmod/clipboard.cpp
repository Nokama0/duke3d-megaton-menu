#include <windows.h>
#include "clipboard.h"

bool clipboard_read_text(char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  // Ask before opening the clipboard: the common case (an image or nothing) then never takes it from the desktop.
  if (!IsClipboardFormatAvailable(CF_TEXT)) return false;
  if (!OpenClipboard(nullptr)) return false;
  int n = 0;
  const HANDLE h = GetClipboardData(CF_TEXT);
  if (h) {
    // The handle is the clipboard's: locked and unlocked but never freed, or the player's clipboard is erased.
    const char* p = (const char*)GlobalLock(h);
    if (p) {
      while (n < cap - 1 && p[n]) { out[n] = p[n]; n++; }
      out[n] = 0;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return n > 0;
}

bool clipboard_write_text(const char* s) {
  if (!s) return false;
  int n = 0;
  while (s[n]) n++;
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (EmptyClipboard()) {
    const HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n + 1);
    if (h) {
      char* p = (char*)GlobalLock(h);
      if (p) {
        for (int i = 0; i < n; i++) p[i] = s[i];
        p[n] = 0;
        GlobalUnlock(h);
        // On success the handle belongs to the clipboard; skipping the free after a failed set would leak it.

        if (SetClipboardData(CF_TEXT, h)) ok = true;
        else GlobalFree(h);
      } else {
        GlobalFree(h);
      }
    }
  }
  CloseClipboard();
  return ok;
}
