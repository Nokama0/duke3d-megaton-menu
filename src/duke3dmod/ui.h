#pragma once
#include "common.h"

struct UiRect { int x, y, w, h; };

struct UiInput {
  int  mx, my;      // overlay pixels
  bool down;
  bool pressed;     // edge
  // Right button, same rules; a right-click on a slider row opens the numeric entry (entry.h).
  bool rdown;
  bool rpressed;
};

bool   ui_hit(const UiRect& r, int x, int y);
UiRect ui_row(UiRect& cursor, int height, int gap);
int    ui_slider_value(const UiRect& track, int mx, int lo, int hi);
int    ui_slider_x(const UiRect& track, int value, int lo, int hi);

bool   ui_press_edge(bool down, bool wasDown);

// True for the initial SDL_KEYDOWN, false for auto-repeat.
bool   ui_key_is_initial_press(bool repeat);

// Toggle value for "save real state, force it while the panel owns input, restore on close": never invents a default.
int    ui_saved_toggle_value(bool panelOwnsInput, int openValue, int savedValue);

bool   ui_bytes_any_nonzero(const unsigned char* bytes, int count);

// A disabled row is inert, not merely dimmed. `buttonState` is the press edge for a switch, held for a slider.
bool   ui_row_accepts_click(bool enabled, bool buttonState, bool hit);

// UI scale; the panel is laid out in logical pixels. Floor 75: at 50 a switch is 10px, too small to hit.
const int UI_SCALE_MIN     = 75;
const int UI_SCALE_MAX     = 200;
const int UI_SCALE_DEFAULT = 100;

inline int ui_scale_clamped(int pct) { return clamp_int(pct, UI_SCALE_MIN, UI_SCALE_MAX); }

// Logical -> device, rounded half away from zero so 1px bevels survive 75%; widened as project_world saturates.
inline int ui_scale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * p;
  return clamp_ll_to_int(v >= 0 ? (v + 50) / 100 : (v - 50) / 100);
}

// Device -> logical; not an exact inverse (both round).
inline int ui_unscale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * 100;
  return clamp_ll_to_int(v >= 0 ? (v + p / 2) / p : (v - p / 2) / p);
}

// Largest scale at which the whole panel height fits (above it rows clip): pct <= (100*screenH + 49) / total, snapped
// down to a multiple of five.
inline int ui_scale_max_for(int screenH, int panelLogicalH, int insetLogical) {
  const long long total = (long long)panelLogicalH + 2LL * (long long)insetLogical;
  if (total <= 0) return UI_SCALE_MAX;
  long long pct = ((long long)screenH * 100 + 49) / total;
  pct -= pct % 5;
  return clamp_int(clamp_ll_to_int(pct), UI_SCALE_MIN, UI_SCALE_MAX);
}

// While the left button is held the panel keeps the applied scale: row_slider_at has no drag capture, so a layout moving
// with the value would slide the row from under a still cursor.
inline int ui_scale_effective(int stored, int applied, bool dragging) {
  return dragging ? applied : stored;
}
