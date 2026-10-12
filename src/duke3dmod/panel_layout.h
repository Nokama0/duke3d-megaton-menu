#pragma once
#include "ui.h"
// Panel layout and drag rules, pure. The default-rect constants live here; menu.cpp includes them.
const int PANEL_W = 620, PANEL_H = 900, PANEL_INSET = 60, PANEL_VINSET = 30;
// Measured minimums (tb_0123_panel_drawn fails on drift). PANEL_MIN_W 540: row_slider_at's track overlaps the widest
// label ("Weapon switch speed") by 60px at 460 and clears the 8px readout margin at 540 (smallest multiple of 20).
const int PANEL_MIN_W = 540;
// PANEL_MIN_H 740: Visuals is the tallest tab (17 rows + 3 headings = 662px); the lowest ink plus PAD measures 730.
const int PANEL_MIN_H = 740;
const int PANEL_GRIP = 16;
struct PanelBox { int x, y, w, h; };
enum { PDRAG_IDLE = 0, PDRAG_MOVE = 1, PDRAG_RESIZE = 2 };
struct PanelDrag { int phase; int grabDx, grabDy; PanelBox start; };

// Default box for a screen this size: PANEL_INSET anchors left edge and width; the vertical clamp takes PANEL_VINSET.
inline PanelBox panel_default_box(int screenW, int screenH) {
  int w = PANEL_W, h = PANEL_H;
  if (h > screenH - 2 * PANEL_VINSET) h = screenH - 2 * PANEL_VINSET;
  if (w > screenW - 2 * PANEL_INSET) w = screenW - 2 * PANEL_INSET;
  PanelBox b = { PANEL_INSET, (screenH - h) / 2, w, h };
  return b;
}
inline int panel_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// A stored box with w or h <= 0 is "unset" and falls back to the default (keeping non-negative x/y); the result is
// clamped on-screen, size first.
inline PanelBox panel_layout(PanelBox stored, int screenW, int screenH) {
  if (stored.w <= 0 || stored.h <= 0) {
    PanelBox d = panel_default_box(screenW, screenH);
    if (stored.x >= 0) d.x = stored.x;
    if (stored.y >= 0) d.y = stored.y;
    stored = d;
  }
  PanelBox b = stored;
  const int maxW = screenW, maxH = screenH;
  b.w = panel_clamp(b.w, PANEL_MIN_W < maxW ? PANEL_MIN_W : maxW, maxW);
  b.h = panel_clamp(b.h, PANEL_MIN_H < maxH ? PANEL_MIN_H : maxH, maxH);
  b.x = panel_clamp(b.x, 0, screenW - b.w);
  b.y = panel_clamp(b.y, 0, screenH - b.h);
  return b;
}
inline UiRect panel_grip_rect(PanelBox b) { UiRect r = { b.x + b.w - PANEL_GRIP, b.y + b.h - PANEL_GRIP, PANEL_GRIP, PANEL_GRIP }; return r; }
inline UiRect panel_title_rect(PanelBox b, int stripH, int statusH) { UiRect r = { b.x, b.y, b.w, 1 + stripH + statusH }; return r; }
// Drag state machine: a press on the grip (resize) or title band (move) follows the mouse until release, which sets
// *ended once. *box always goes through panel_layout.
inline PanelDrag panel_drag_step(PanelDrag d, bool pressed, bool down, bool titleHit, bool gripHit,
                                 int mx, int my, PanelBox current, int screenW, int screenH,
                                 PanelBox* box, bool* ended) {
  *ended = false; *box = current;
  if (d.phase == PDRAG_IDLE) {
    if (pressed && gripHit) { d.phase = PDRAG_RESIZE; d.start = current; d.grabDx = mx - (current.x + current.w); d.grabDy = my - (current.y + current.h); }
    else if (pressed && titleHit) { d.phase = PDRAG_MOVE; d.start = current; d.grabDx = mx - current.x; d.grabDy = my - current.y; }
    return d;
  }
  if (!down) { d.phase = PDRAG_IDLE; *ended = true; return d; }
  PanelBox b = d.start;
  if (d.phase == PDRAG_MOVE) { b.x = mx - d.grabDx; b.y = my - d.grabDy; }
  else {
    b.w = (mx - d.grabDx) - b.x; b.h = (my - d.grabDy) - b.y;
    // Floored at 1, not 0: panel_layout reads w/h <= 0 as "no stored box", so a resize must stop at the minimum.
    if (b.w < 1) b.w = 1;
    if (b.h < 1) b.h = 1;
  }
  *box = panel_layout(b, screenW, screenH);
  return d;
}
