#include "menu.h"
#include "panel_layout.h"
#include "canvas.h"
#include "config.h"
#include "common.h"
#include "font.h"
#include "input.h"
#include "fov.h"
// Each include keeps a control's bounds with the code that clamps it, so a slider cannot offer a value the clamp would
// undo.
#include "esp_layout.h"
#include "target.h"
#include "freecam.h"
#include "accuracy.h"
#include "nocull.h"
#include "light.h"
#include "bind.h"
#include "gamecfg.h"
#include "mouseaim.h"
#include "firerate.h"
#include "anim.h"
#include "wallclock.h"
#include "tooltip.h"
#include "entry.h"
#include "autosave.h"
#include <stdio.h>
#include <string.h>

static int s_tab = TAB_VISUALS;

// Which row is waiting for a key, if any. One capture for the whole panel: a second armed row would make the next key
// ambiguous. Transitions are keycap_step's (bind.h).
static KeyCapState s_keycap = { KEYCAP_IDLE, -1 };

// The game's own cfg, read once per panel open. Empty is both default and failure mode: every row then reads unbound.
static char s_gameCfg[GAMECFG_MAX] = { 0 };

void menu_gamecfg_load(const char* path) {
  s_gameCfg[0] = 0;
  if (!path || !path[0]) return;
  FILE* f = nullptr;
  if (fopen_s(&f, path, "rb") != 0 || !f) return;
  const size_t n = fread(s_gameCfg, 1, sizeof(s_gameCfg) - 1, f);
  s_gameCfg[n] = 0;
  fclose(f);
}

bool menu_keycap_armed() { return s_keycap.phase == KEYCAP_ARMED; }

void menu_keycap_bind(int vk) {
  // A mouse button and a key are one binding to config_bind_set and bind_label, but keycap_step refuses mouse 1 on the
  // mouse arm.
  KeyCapEvent e;
  e.kind = bind_is_mouse(vk) ? KEYCAP_EV_MOUSE : KEYCAP_EV_KEY;
  e.row = -1;
  e.vk = vk;
  const KeyCapResult r = keycap_step(s_keycap, e);
  s_keycap = r.state;
  if (r.boundRow >= 0) {
    config_bind_set(g_cfg, r.boundRow, r.boundVk);
    g_cfgDirty = true;
  }
}

// Forward-declared: defined below MenuPresentation; close_menu() reaches it on every real close.
static void menu_note_real_close();

void menu_keycap_close() {
  KeyCapEvent e;
  e.kind = KEYCAP_EV_CLOSE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
  menu_note_real_close();
}

void menu_keycap_cancel() {
  // The player's cancel (Escape with a row armed). Closing disarms via menu_keycap_close() instead; both differ only in
  // `consumed`.
  KeyCapEvent e;
  e.kind = KEYCAP_EV_ESCAPE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
}

// The store only: every transition is entry_step's (entry.h).
static EntryState s_entry = entry_begin();

// Where a committed value waits until the owning row writes it back on the next draw. One slot: two fields cannot be
// open in one frame.
struct MenuEntryCommit { bool pending; int rowKey; int value; };
static MenuEntryCommit s_entryCommit = { false, -1, 0 };

// Whether the left button went down inside the field and is still down: later held frames drag the selection. Cleared
// when the button is up and at menu_entry_begin_edit, so a drag cannot carry into the next field.
static bool s_entryCaretDrag = false;

bool menu_entry_open() { return s_entry.open != 0; }

const char* menu_entry_text() { return s_entry.text; }

// A left press outside the field, or a right press opening a field on another row, means the player is done; Escape is
// the only cancel.
static void entry_clickaway_commit() {
  if (!s_entry.open) return;
  const int row = s_entry.rowKey;
  const EntryResult r = entry_step(s_entry, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = row;
    s_entryCommit.value = r.value;
  }
}

void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text) {
  // An armed capture goes down first: input.cpp routes to the field before the capture, so one armed behind an open
  // field could never bind a digit, Backspace, Enter or Escape. Cancel, not close: the armed row keeps its binding.
  menu_keycap_cancel();
  // A right-click can open a field while the left button is still held in the field it replaces; a surviving drag flag
  // would open it with a spurious selection.
  s_entryCaretDrag = false;
  s_entry = entry_step(s_entry, ENTRY_EV_OPEN, 0, rowKey, kind, lo, hi, text).state;
}

void menu_entry_close() {
  s_entry = entry_begin();
  // The pending commit goes too, or it would apply next session to a row no longer in view.
  s_entryCommit.pending = false;
  s_entryCommit.rowKey = -1;
  s_entryCommit.value = 0;
}

// What the last Ctrl+C or Ctrl+X put on the clipboard, kept alive for input.cpp to read.
static char s_entryClip[ENTRY_TEXT_CAP + 1] = { 0 };

const char* menu_entry_key_mod(int sc, bool shift, bool ctrl) {
  if (!s_entry.open) return nullptr;
  const int row = s_entry.rowKey;

  int ev = ENTRY_EV_CHAR;
  int ch = 0;
  if (ctrl && sc == SDL_SCANCODE_A)      ev = ENTRY_EV_SELECT_ALL;
  else if (ctrl && sc == SDL_SCANCODE_C) ev = ENTRY_EV_COPY;
  else if (ctrl && sc == SDL_SCANCODE_X) ev = ENTRY_EV_CUT;
  // Ctrl+V needs a clipboard read this file cannot make; input.cpp reads it and calls menu_entry_paste.
  else if (ctrl && sc == SDL_SCANCODE_V) return nullptr;
  else if (sc == SDL_SCANCODE_ESCAPE)    ev = ENTRY_EV_ESCAPE;
  else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                                         ev = ENTRY_EV_ENTER;
  else if (sc == SDL_SCANCODE_BACKSPACE) ev = ENTRY_EV_BACKSPACE;
  else if (sc == SDL_SCANCODE_DELETE)    ev = ENTRY_EV_DELETE;
  else if (sc == SDL_SCANCODE_LEFT)  { ev = ENTRY_EV_LEFT;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_RIGHT) { ev = ENTRY_EV_RIGHT; ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_HOME)  { ev = ENTRY_EV_HOME;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_END)   { ev = ENTRY_EV_END;   ch = shift ? 1 : 0; }
  else {
    ch = entry_char_of_scancode(sc);
    // input.cpp asks the same question before routing, so a key is never swallowed by routing then ignored here.
    if (!ch) return nullptr;
  }

  const EntryResult r = entry_step(s_entry, ev, ch, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = row;
    s_entryCommit.value = r.value;
  }
  if (!r.hasClip) return nullptr;
  int n = 0;
  while (n < ENTRY_TEXT_CAP && r.clip[n]) { s_entryClip[n] = r.clip[n]; n++; }
  s_entryClip[n] = 0;
  return s_entryClip;
}

void menu_entry_key(int sc) { menu_entry_key_mod(sc, false, false); }

void menu_entry_paste(const char* s) {
  if (!s_entry.open || !s) return;
  // A paste never commits.
  s_entry = entry_step(s_entry, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, s).state;
}

bool menu_entry_pending(int* rowKey, int* value) {
  if (!s_entryCommit.pending) return false;
  if (rowKey) *rowKey = s_entryCommit.rowKey;
  if (value)  *value  = s_entryCommit.value;
  return true;
}

// PANEL_W/H/INSET/VINSET live in panel_layout.h so panel_layout() reproduces this rect exactly. The vertical margin is
// half the horizontal so a 1440p screen carries the panel at 150%.

// A group heading is shorter than a row and carries its own room below (mockup `.grouphead`).
const int GROUP_H = 22, GROUP_GAP = 6;
// A sub-row's label indent and the x of its connecting rule (mockup `.row.sub`).
const int SUB_RULE_X = 3, SUB_INDENT = 21;

// The drag's state for panel_layout.h's rule: `s_drag` is the machine, `s_dragBox` the last live box (no mouse position
// remains once the button is up), `s_dragLive` marks a drag in flight so no row acts under it.
static PanelDrag s_drag = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
static PanelBox  s_dragBox = { 0, 0, 0, 0 };
static bool      s_dragLive = false;

// A press already spent: the reset glyph (overlapping the track's grab box) and the click-away closing an open field sit
// inside a row and would let one press do two things. A latch for the whole hold; cleared once the button is up.
static bool      s_pressSpent = false;

// A press a control has claimed; a left press no control took is a panel move. Per frame, unlike s_pressSpent, which
// spans the hold and would make a dragging slider disarm itself on the second frame.
static bool      s_pressClaimed = false;

// The four ini keys as a box, with panel_layout's sentinels; nothing is interpreted here.
static PanelBox panel_stored_box() {
  PanelBox b = { g_cfg.panelx, g_cfg.panely, g_cfg.panelw, g_cfg.panelh };
  return b;
}

PanelBox menu_panel_box(int logicalW, int logicalH) {
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  return s_dragLive ? s_dragBox : laid;
}

UiRect menu_panel_rect(int screenW, int screenH) {
  const PanelBox b = menu_panel_box(screenW, screenH);
  return UiRect{ b.x, b.y, b.w, b.h };
}

// The scale the panel settles at on a screen this tall: the stored setting, capped so the whole logical height fits.
// Public because a click arrives in device pixels. The ceiling follows the stored height, not the drawn one, so it does
// not move every frame of a resize; floored at PANEL_MIN_H, below which panel_layout raises the panel and drops rows.
static int menu_ceiling_height() {
  const int h = g_cfg.panelh > 0 ? g_cfg.panelh : PANEL_H;
  return h < PANEL_MIN_H ? PANEL_MIN_H : h;
}

int menu_applied_scale(int screenH) {
  const int stored = ui_scale_clamped(g_cfg.uiscale);
  const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
  return stored < ceiling ? stored : ceiling;
}

// The applied scale carried between frames so a press can hold it: written only while the button is UP, so it is the
// pre-drag value for the whole press.
static int s_scaleHeld = UI_SCALE_DEFAULT;

int  menu_active_tab() { return s_tab; }
void menu_set_tab(int tab) { if (tab >= 0 && tab < TAB_COUNT) s_tab = tab; }

static const char* TAB_NAMES[TAB_COUNT] = { "Visuals", "Combat", "Weapons", "View", "Settings" };
static const unsigned TAB_COLOURS[TAB_COUNT] = { C_BLUE, C_RED, C_HAZARD, C_GREEN, C_SETTINGS };

// Cosmetic layer, consulted only when drawing; hit testing uses the logical UiRect. `knob`/`fill`/`hover` are indexed by
// row ordinal, not per-tab identity, so a tab switch snaps every spring to its logical value instead of sliding in.
struct MenuPresentation {
  unsigned lastMs;
  AnimSpring open;
  int openTarget;
  unsigned tabSwitchMs;
  int tabFrom;
  AnimSpring pillX, pillW;
  AnimSpring knob[64];
  AnimSpring fill[64];
  double hover[64];
  // The reset button's own hover fraction; `hover` already follows a slider track's grab region.
  double resetHover[64];
  // Glass only. `openStartMs` is when this open began, `wasOpen` the previous frame's openTarget (an edge once per
  // open); `toggleMs`/`toggleSeen` are per row ordinal, for the knob's landing bloom.
  unsigned openStartMs;
  int wasOpen;
  unsigned toggleMs[64];
  int toggleSeen[64];   // 0 not seen yet, 1 off, 2 on
  int rowsThisFrame;
};
static MenuPresentation s_pres;

// The one moment production learns the panel closed: under ANIM_OFF overlay.cpp never draws the closing frame, so
// wasOpen would never fall to 0. Also resets the drag statics (a panel closed mid-drag would commit a half-finished
// box on reopen) and the tooltip (its rest timer would show it at full alpha on the next open's first frame).
static void menu_tooltip_note_close();
static void menu_note_real_close() {
  s_pres.wasOpen = 0;
  s_drag.phase = PDRAG_IDLE;
  s_dragLive = false;
  // And the spent press, or the first click of the next session would do nothing.
  s_pressSpent = false;
  menu_tooltip_note_close();
}

// Every tab stays well under this many row calls (Combat, the longest, has fifteen), so `% ANIM_ROW_SLOTS` is a
// defensive floor.
static const int ANIM_ROW_SLOTS = 64;
static bool s_tabSwitchedThisFrame = false;
static double s_frameDt = 0.0;

// Hints as tooltips (the rule is tooltip.h's): row_hint attaches a hint by adjacency in the draw, not a table that could
// drift. The key is `s_tab * 1000 + ordinal`: the ordinal is stable only within a tab. -1 means "no row", which a row
// helper also leaves when it draws nothing.
static UiRect s_lastRow = { 0, 0, 0, 0 };
static int s_lastRowKey = -1;
static int s_hoverRowKey = -1;
// The hint is copied, not pointed at: the tooltip draws after the tab's case block closed and some hints are stack
// buffers, so a pointer would dangle.
static char s_hintBuf[96];
static const char* s_hoverHint = nullptr;
static const char* s_tooltipText = nullptr;
static TooltipState s_tooltip = tooltip_begin();
static const int TOOLTIP_R = 4;
// The reset glyph's own hint, preferred by the tooltip pass whenever set. Its own static: a row_hint call from inside
// row_slider_at would be overwritten by the row's hint written after the helper returns. A literal, never a copy.
static const char* s_glyphHint = nullptr;

// The panel closed for real: resets the tooltip so the reopen starts its rest from its own clock. The tooltip pass also
// refuses to draw unless the panel is logically open.
static void menu_tooltip_note_close() {
  s_tooltip = tooltip_begin();
  s_tooltipText = nullptr;
  s_hoverHint = nullptr;
  s_glyphHint = nullptr;
  s_hoverRowKey = -1;
  s_lastRowKey = -1;
}

const char* menu_tooltip_text() { return s_tooltipText; }

// The hint belonging to the row just drawn; records what to show if the cursor rests here, drawn once at the end.
static void row_hint(const char* text) {
  if (s_lastRowKey < 0 || !text) return;
  if (!ui_hit(s_lastRow, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_lastRowKey;
  strncpy_s(s_hintBuf, sizeof(s_hintBuf), text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

static unsigned s_nowOverride = 0;
void menu_set_now_ms(unsigned ms) { s_nowOverride = ms; }
static unsigned menu_now() { return s_nowOverride ? s_nowOverride : wallclock_ms(); }

bool menu_closing_animation_active() {
  if (g_cfg.menuanim == ANIM_OFF || input_menu_open()) return false;
  // A spring nothing has stepped for a second is at rest: settled here (this reader runs when menu_draw may not) so the
  // next open animates from 0 instead of jumping in from an abandoned fade. Unsigned difference, wrap-safe.
  if (menu_now() - s_pres.lastMs > ANIM_STALE_MS) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
  return !anim_spring_settled(s_pres.open, 0.0, 0.01);
}

// The whole-panel open-alpha. Identity under ANIM_OFF, where `open.x` is exactly 0.0 or 1.0.
static unsigned pres_colour(unsigned c) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  const int srcA = (int)((c >> 24) & 0xFFu);
  const int a = (int)(srcA * s_pres.open.x + 0.5);
  return canvas_with_alpha(c, a);
}

// A colour at a fraction of its alpha for the tooltip's fade-in; the tooltip has its own alpha, not the panel's.
static unsigned alpha_scale(unsigned c, double a) {
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  const int srcA = (int)((c >> 24) & 0xFFu);
  return canvas_with_alpha(c, (int)(srcA * a + 0.5));
}

// pres_colour further scaled by the active tab's crossfade (ANIM_TAB_MS after a tab switch); the rail and chrome use
// pres_colour.
static unsigned pres_content_colour(unsigned c) {
  const unsigned withOpen = pres_colour(c);
  if (g_cfg.menuanim == ANIM_OFF) return withOpen;
  const double tabP = anim_ease_out(anim_progress(s_pres.tabSwitchMs, s_pres.lastMs, ANIM_TAB_MS));
  const int srcA = (int)((withOpen >> 24) & 0xFFu);
  const int a = (int)(srcA * tabP + 0.5);
  return canvas_with_alpha(withOpen, a);
}

// Blends `c` toward C_BONE by `hoverFrac`, alpha untouched. Identity under ANIM_OFF.
static unsigned pres_hover_lighten(unsigned c, double hoverFrac) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  return anim_blend_colour(c, C_BONE, hoverFrac);
}

// The same toward a caller-chosen target: lightening a hotkey field's well, rim and name all toward C_BONE made the
// name vanish at full hover, so row_bind moves well and rim toward other colours.
static unsigned pres_hover_toward(unsigned c, unsigned target, double hoverFrac) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  return anim_blend_colour(c, target, hoverFrac);
}

// Glass's numbers, in logical pixels and milliseconds. The boldness is spent on the open; only the two opening
// constants run over 200 ms.
static const int      GLASS_R            = 8;    // the slab's corner radius
static const int      GLASS_ROW_R        = 4;    // a control's, smaller so a switch still reads as a switch
static const int      GLASS_TOP_H        = 40;   // the lighter top edge's height
static const unsigned GLASS_CONDENSE_MS  = 250;  // backdrop wobble and settle
static const unsigned GLASS_SWEEP_MS     = 400;  // the one highlight, after the condense
static const int      GLASS_SWEEP_W      = 80;
static const unsigned GLASS_TOGGLE_MS    = 200;  // a knob's bloom as it lands
static const int      GLASS_BLUR_TAPS    = 9;    // canvas_backdrop_draw takes 1, 5 or 9
static const int      GLASS_BLUR_RADIUS  = 2;
static const double   GLASS_SLAB_MIN     = 0.96;
static const double   GLASS_WOBBLE_AMP   = 6.0;
static const double   GLASS_PILL_STRETCH = 0.0008;
static const double   GLASS_PILL_MAX     = 0.35;
static const double   GLASS_HOVER_GLOW   = 0.5;
static const double   GLASS_GROUND_A     = 0.45; // share of an interior fill that survives

// The open slides down from this far above its rest, in logical pixels.
static const int      SIMPLE_SLIDE_PX    = 8;

static bool glass_style() { return g_cfg.menuanim == ANIM_GLASS; }

// No row acts while the panel is dragged: a clamped move leaves the cursor over a row with the button held, which a
// slider reads as a drag. A filter on the button fields so rows still draw; s_pressSpent covers glyph/click-away.
static bool row_pressed() { return !s_dragLive && !s_pressSpent && g_ui.pressed; }
static bool row_down()    { return !s_dragLive && !s_pressSpent && g_ui.down; }

// Where the open field was drawn last frame: the click-away test runs before any row draws, so it reads the previous
// frame's well rect (stable, except the opening frame, which it is never asked about).
static UiRect s_entryFieldRect = { 0, 0, 0, 0 };

// C_GLASS_GLOW at a fraction of its alpha, faded with the panel's open.
static unsigned glass_glow(double f) {
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  const int a = (int)(((C_GLASS_GLOW >> 24) & 0xFFu) * f * s_pres.open.x + 0.5);
  return canvas_with_alpha(C_GLASS_GLOW, a);
}

// A panel-interior fill: opaque steel under Off and Simple, a fraction of its alpha under Glass, or the slab is glass in
// name only.
static unsigned glass_ground(unsigned c) {
  if (!glass_style()) return c;
  const int a = (int)(((c >> 24) & 0xFFu) * GLASS_GROUND_A + 0.5);
  return canvas_with_alpha(c, a);
}

// A bevel under Off and Simple, a rounded slab under Glass; every canvas_bevel call goes through here.
static void chrome_box(int x, int y, int w, int h, unsigned fill, unsigned hi,
                       unsigned lo, int r) {
  if (glass_style()) canvas_round_rect(x, y, w, h, r, fill);
  else               canvas_bevel(x, y, w, h, fill, hi, lo);
}

// The shared body of both switch rows; draws nothing when no full row fits (ui_row has no floor on the remaining
// height). `indent` draws the rule tying a sub-row to its parent. A disabled row is inert; if ON it keeps its knob
// right but fills C_RAISED, so "on, but doing nothing" differs from "on and live".
static int row_switch_at(UiRect& cursor, const char* label, int value, unsigned accent,
                         bool enabled, int indent, unsigned labelOn) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const UiRect sw = { r.x + r.w - 42, r.y + 5, 42, 20 };
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  // The hint's row is the whole row, not the switch: a hint answers to what the player is reading.
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;

  // Hover rises toward 1 while the switch is hovered and falls otherwise; snapped under ANIM_OFF.
  const bool hovered = enabled && ui_hit(sw, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  // The knob's drawn ON fraction: sprung toward the value, reset on the first row after a tab switch so nothing slides
  // in from another tab's row. ANIM_OFF skips the spring.
  int knobX;
  if (g_cfg.menuanim == ANIM_OFF) {
    knobX = value ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = value ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }

  // When the switch last flipped, for the Glass bloom; kept in every style. Stamped only when the value moved, never on
  // a slot's first frame or across a tab switch.
  {
    const int seen = value ? 2 : 1;
    if (!s_tabSwitchedThisFrame && s_pres.toggleSeen[idx] != 0 &&
        s_pres.toggleSeen[idx] != seen)
      s_pres.toggleMs[idx] = s_pres.lastMs;
    s_pres.toggleSeen[idx] = seen;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  chrome_box(sw.x, sw.y, sw.w, sw.h,
             pres_content_colour(value ? (enabled ? accent : C_RAISED) : C_WELL),
             pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED), GLASS_ROW_R);
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((g_cfg.menuanim == ANIM_OFF ? value != 0 : s_pres.knob[idx].x >= 0.5)
                                   ? C_BONE : C_RAISED));
  if (glass_style()) {
    const double bloom = s_pres.toggleMs[idx] == 0 ? 0.0
                       : 1.0 - anim_progress(s_pres.toggleMs[idx], s_pres.lastMs, GLASS_TOGGLE_MS);
    if (bloom > 0.0) canvas_glow_rect(knobX - 3, sw.y - 1, 23, 22, glass_glow(bloom));
    if (s_pres.hover[idx] > 0.0)
      canvas_glow_rect(sw.x, sw.y, sw.w, sw.h, glass_glow(s_pres.hover[idx] * GLASS_HOVER_GLOW));
  }
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    g_cfgDirty = true;
    return !value;
  }
  return value;
}

static int row_switch(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, 0, C_BONE);
}

// Indented under a connecting rule in the dimmer bone; only does anything while its parent switch is on.
static int row_switch_sub(UiRect& cursor, const char* label, int value, unsigned accent,
                          bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, SUB_INDENT, C_BONEDIM);
}

// VALUE_MARGIN lives in menu.h. FIELD_PAD is the room a bind row's field keeps each side of its text.
const int FIELD_PAD        = 8;
const int TRACK_GAP        = 8;
const int TRACK_X_OFFSET   = 172;  // the track's left edge
const int TRACK_MAX_W      = 120;  // the track's width when the readout is narrow
const int TRACK_MIN_W      = 40;   // a floor so an extreme label cannot collapse it

// 24 logical pixels are reserved between every numeric slider's track and readout on every frame: shortening the track
// only while hovered would move the knob from under the hand, and drawing the button over the track's end would put a
// click target on a drag target.
const int RESET_W          = 20;
const int RESET_RESERVE    = 24;

// The shared body of both slider rows; `valueText` is the readout, right-aligned since a name can be wider than a number.
// The track is sized from `reserveText`, the widest readout, never the live text (it collapsed mid-drag when "141%" became
// "141% max"). EK_NONE is a name readout with no right-click and no reset glyph.
static int row_slider_at(UiRect& cursor, const char* label, int value, int lo, int hi,
                         const char* valueText, const char* reserveText,
                         bool enabled, int indent, unsigned labelOn,
                         int kind, int def) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;
  const int valueW = canvas_text_width(valueText, FONT_MONO);
  const int valueX = r.x + r.w - VALUE_MARGIN - valueW;
  // The track stops TRACK_GAP short of where the WIDEST readout would start, so its width is stable through a drag.
  const int reserveX = r.x + r.w - VALUE_MARGIN - canvas_text_width(reserveText, FONT_MONO);
  // The track's left edge is the fixed TRACK_X_OFFSET so tracks line up; a reserve wider than budgeted slides the edge
  // to wherever TRACK_MIN_W still fits. Numeric rows reserve RESET_RESERVE more for the reset glyph.
  const int resetRoom   = (kind == EK_NONE) ? 0 : RESET_RESERVE;
  const int trackRight  = reserveX - TRACK_GAP - resetRoom;
  const int trackXFixed = r.x + r.w - TRACK_X_OFFSET;
  const int trackXFit   = trackRight - TRACK_MIN_W;
  const int trackX      = trackXFixed < trackXFit ? trackXFixed : trackXFit;
  const UiRect track = { trackX, r.y + 11,
                         clamp_int(trackRight - trackX, TRACK_MIN_W, TRACK_MAX_W),
                         8 };
  const UiRect grab = { track.x - 6, track.y - 8, track.w + 12, track.h + 16 };
  // The button is centred in its reserved room. `glyphLive`: enabled, a number, cursor on the row, and no field open
  // there (a reset button under the hand while typing would be a trap).
  const UiRect glyph = { trackRight + (RESET_RESERVE - RESET_W) / 2, r.y + 5, RESET_W, RESET_W };
  const bool fieldHere   = s_entry.open != 0 && s_entry.rowKey == key;
  const bool glyphLive   = enabled && kind != EK_NONE && !fieldHere
                           && !s_dragLive && ui_hit(r, g_ui.mx, g_ui.my);
  const bool glyphHot    = glyphLive && ui_hit(glyph, g_ui.mx, g_ui.my);

  // The glyph's click goes through the same one-slot commit the typed field uses, before the commit slot is read so the
  // reset lands this frame.
  if (glyphHot && row_pressed()) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = key;
    s_entryCommit.value = def;
    s_pressSpent = true;
    s_pressClaimed = true;
  }

  // A committed value is clamped to the bounds the row holds THIS frame (the UI scale ceiling moves with the panel's
  // height). A commit that changes nothing is not a write: `committed` is compared after the clamp.
  bool committed = false;
  if (s_entryCommit.pending && s_entryCommit.rowKey == key) {
    s_entryCommit.pending = false;
    s_entryCommit.rowKey = -1;
    const int landed = clamp_int(s_entryCommit.value, lo, hi);
    committed = landed != value;
    value = landed;
  }

  // The right press that opens a field, anywhere on the row (an 8px-tall well is a worse target); greyed rows open
  // nothing.
  if (kind != EK_NONE && enabled && !s_dragLive && g_ui.rpressed
      && ui_hit(r, g_ui.mx, g_ui.my)) {
    char prefill[ENTRY_TEXT_CAP + 1];
    entry_format(prefill, sizeof prefill, value, kind);
    // A field open elsewhere is clicked away from, not discarded: leaving is a commit, so typed digits are kept.
    entry_clickaway_commit();
    menu_entry_begin_edit(key, kind, lo, hi, prefill);
  }

  const bool fieldOpen = s_entry.open != 0 && s_entry.rowKey == key;

  // The glyph's hint replaces the row's; most numeric rows have no hint, so nothing else would tell the tooltip.
  if (glyphHot) { s_hoverRowKey = key; s_glyphHint = MENU_RESET_HINT; }

  // The knob's logical x from the value after the commit, so it moves on the frame the value lands.
  const int kx = ui_slider_x(track, value, lo, hi);

  // Hover: row_switch_at's rule, off the grab region (what a drag catches).
  const bool hovered = enabled && ui_hit(grab, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
  // The reset button's fraction, by the same rule, stepped on every frame this row draws so it falls back to 0.
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.resetHover[idx] = glyphHot ? 1.0 : 0.0;
  } else {
    const double rate = glyphHot ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double rv = s_pres.resetHover[idx] + rate * s_frameDt;
    if (rv < 0.0) rv = 0.0; else if (rv > 1.0) rv = 1.0;
    s_pres.resetHover[idx] = rv;
  }

  // The fill's drawn end x: sprung toward `kx`, reset on the first row after a tab switch; ANIM_OFF uses `kx`.
  int drawnKx;
  if (g_cfg.menuanim == ANIM_OFF) {
    drawnKx = kx;
  } else {
    const double targetFrac = track.w > 0 ? (double)(kx - track.x) / (double)track.w : 0.0;
    // Glass's pair is underdamped (overshoots and settles); Simple's critical pair never crosses its target.
    const AnimSpringParams sp = glass_style() ? ANIM_SPRING_GLASS : ANIM_SPRING_CRITICAL;
    if (s_tabSwitchedThisFrame) { s_pres.fill[idx].x = targetFrac; s_pres.fill[idx].v = 0.0; }
    else anim_spring_step(&s_pres.fill[idx], targetFrac, sp.stiffness, sp.damping, s_frameDt);
    drawnKx = track.x + (int)(s_pres.fill[idx].x * track.w + 0.5);
    // Clamped to the track: an overshoot would draw the knob outside its well.
    if (drawnKx < track.x) drawnKx = track.x;
    if (drawnKx > track.x + track.w) drawnKx = track.x + track.w;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  chrome_box(track.x, track.y, track.w, track.h, pres_content_colour(C_WELL),
             pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED), GLASS_ROW_R);
  canvas_rect(track.x, track.y, drawnKx - track.x, track.h, pres_content_colour(C_HAZARDDIM));
  canvas_rect(drawnKx - 4, track.y - 4, 9, 16, pres_content_colour(enabled ? C_HAZARD : C_BONEFNT));
  if (fieldOpen) {
    // The hotkey field's shape (rounded well, 4px C_HAZARD rim), sized for the widest text it can hold ("-2147483648"),
    // never what it holds: a well that grew while typed into would move under the hand.
    const int fieldW = canvas_text_width("-2147483648", FONT_MONO) + 2 * FIELD_PAD;
    const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
    s_entryFieldRect = field;
    canvas_round_rect(field.x, field.y, field.w, field.h, 4,
                      pres_content_colour(C_WELL));
    canvas_round_rect_outline(field.x, field.y, field.w, field.h, 4, 1,
                              pres_content_colour(C_HAZARD));
    // The text's y from FONT_MONO's metrics: ink from cap height to the deepest descender, centred in the well.
    const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
    const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
    const int inkTop = capA.by;
    const int inkH   = (descY.by + descY.h) - inkTop;
    const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
    const int textX  = field.x + FIELD_PAD;
    // FONT_MONO has one advance per character, so the boundary before glyph i is i advances in, as entry_index_at inverts.
    const int advance = canvas_text_width("0", FONT_MONO);
    // A left press in the well puts the caret; holding drags the selection end, the press index staying the anchor. Uses
    // the raw button and its own latch: the press sets s_pressSpent so the row beneath cannot read a track drag.
    if (g_ui.pressed && ui_hit(field, g_ui.mx, g_ui.my)) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_at(s_entry.len, textX, g_ui.mx, advance),
                           0, EK_NONE, 0, 0, 0).state;
      s_entryCaretDrag = true;
    } else if (s_entryCaretDrag && g_ui.down) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_at(s_entry.len, textX, g_ui.mx, advance),
                           1, EK_NONE, 0, 0, 0).state;
    }
    // The selection is drawn under the text, in C_HAZARDDIM, so selected glyphs stay readable in C_BONE.
    const int selA = entry_sel_lo(s_entry), selB = entry_sel_hi(s_entry);
    if (selB > selA)
      canvas_rect(textX + selA * advance, field.y + 4, (selB - selA) * advance,
                  field.h - 8, pres_content_colour(C_HAZARDDIM));
    canvas_text(textX, textY, s_entry.text, pres_content_colour(C_BONE), FONT_MONO);
    // One pixel wide, steady under Off, otherwise blinking on a 500 ms period.
    const bool caretOn = (g_cfg.menuanim == ANIM_OFF) || ((menu_now() / 500u) % 2u == 0u);
    if (caretOn)
      canvas_rect(textX + s_entry.caret * advance + 1, field.y + 4,
                  1, field.h - 8, pres_content_colour(C_BONE));
  } else {
    canvas_text(valueX, r.y + 7, valueText, pres_content_colour(C_BONE), FONT_MONO);
    // Reset button: a 20x20 well like row_bind's, hovered off its own fraction. The icon is eight points joined by seven
    // segments at radii 6 and 5, so the stroke reads 2px wide; the ring stays open (no segment joins index 7 to 0), and
    // the head is a filled 3x3 block at RING6[0] plus one barb line: sixteen ops.
    if (glyphLive) {
      const double rf = s_pres.resetHover[idx];
      canvas_round_rect(glyph.x, glyph.y, glyph.w, glyph.h, 4,
                        pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, rf)));
      canvas_round_rect_outline(glyph.x, glyph.y, glyph.w, glyph.h, 4, 1,
                                pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, rf)));
      // Both rings are about (10, 10), open at three o'clock; index 0 is where the head sits, 7 the other free end.
      static const int RING6[8][2] = {
        { 16, 10 }, { 15, 6 }, { 11, 4 }, { 7, 5 }, { 4, 8 },
        { 4, 12 }, { 7, 15 }, { 11, 16 }
      };
      static const int RING5[8][2] = {
        { 15, 10 }, { 14, 7 }, { 11, 5 }, { 7, 6 }, { 5, 8 },
        { 5, 12 }, { 7, 14 }, { 11, 15 }
      };
      static const int BARB[2] = { 13, 13 };
      // The arrowhead extends the ink one pixel right, so the icon shifts one pixel left to keep even air each side.
      const int ix = glyph.x - 1, iy = glyph.y;
      const unsigned gc = pres_content_colour(glyphHot ? C_BONE : C_BONEDIM);
      for (int i = 0; i < 7; i++) {
        canvas_line(ix + RING6[i][0], iy + RING6[i][1],
                    ix + RING6[i + 1][0], iy + RING6[i + 1][1], gc);
        canvas_line(ix + RING5[i][0], iy + RING5[i][1],
                    ix + RING5[i + 1][0], iy + RING5[i + 1][1], gc);
      }
      canvas_rect(ix + RING6[0][0] - 1, iy + RING6[0][1] - 1, 3, 3, gc);
      canvas_line(ix + RING6[0][0], iy + RING6[0][1],
                  ix + BARB[0], iy + BARB[1], gc);
    }
  }
  if (glass_style() && s_pres.hover[idx] > 0.0)
    canvas_glow_rect(track.x, track.y - 4, track.w, track.h + 8,
                     glass_glow(s_pres.hover[idx] * GLASS_HOVER_GLOW));
  // The held button, not the press edge: a slider is dragged. Reads the logical track so a drag lands where the control
  // is going; not while a field is open on this row (a stray click could set the number being typed).
  if (!fieldOpen &&
      ui_row_accepts_click(enabled, row_down(), ui_hit(grab, g_ui.mx, g_ui.my))) {
    // The track claims the press every frame of the drag. Not s_pressSpent, which spans the hold and row_down() reads, so
    // the track would switch itself off mid-drag.
    s_pressClaimed = true;
    const int v = ui_slider_value(track, g_ui.mx, lo, hi);
    if (v != value) { g_cfgDirty = true; return v; }
  }
  // A value from the field or the glyph returns like a dragged one, so every caller's settle path sees it.
  if (committed) g_cfgDirty = true;
  return value;
}

// A top-level slider; returns the new value. EK_INT because "%d%s" is what this wrapper formats.
static int row_slider(UiRect& cursor, const char* label, int value, int lo, int hi,
                      const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  // The reserve is `hi`: every numeric range here is non-negative, so its digit count bounds every value shown.
  sprintf_s(reserve, "%d%s", hi, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled, 0, C_BONE,
                       EK_INT, def);
}

static int row_slider_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                          const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  sprintf_s(reserve, "%d%s", hi, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled,
                       SUB_INDENT, C_BONEDIM, EK_INT, def);
}

// A top-level slider whose readout is a name.
static int row_slider_named(UiRect& cursor, const char* label, int value, int lo, int hi,
                            const char* (*labelFn)(int), bool enabled) {
  // The widest name over the range, walked so it cannot drift from the labels.
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  // EK_NONE: a name has nothing to type or reset to; `def` is never read.
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, 0, C_BONE, EK_NONE, 0);
}

static int row_slider_named_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                                const char* (*labelFn)(int), bool enabled) {
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, SUB_INDENT, C_BONEDIM, EK_NONE, 0);
}

// A group heading, passed already uppercase since this renderer has no text-transform; it labels a group, so it is not
// a breach of the sentence-case rule. Same out-of-room guard as the row helpers.
static void draw_group(UiRect& cursor, const char* heading) {
  if (cursor.h < GROUP_H) return;
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  canvas_text(r.x, r.y + 5, heading, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A line of text that is a row of its own. Not a hint (those are tooltips): MENU_GAMECFG_NOTE is a state true of some
// rows that a player must see without hovering.
static void draw_placeholder(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) return;
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  canvas_text(r.x, r.y + 6, text, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A binding row: a click arms it and the next key or button binds, at hooked_poll; the click lands anywhere on the row.
// The readout is a well with a rim (a field-shaped box invites a click). Always live: a binding has no parent switch,
// the feature's own switch decides whether the key acts.
static void row_bind(UiRect& cursor, const char* label, int row) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  // THE FIELD IS SIZED FOR THE WIDEST THING IT CAN SHOW, as row_slider_at's `reserve`: a well that changed width when
  // the capture armed would move under the aiming hand. Measured: the prompt, the invitation and BIND_LABEL_WIDEST.
  int fieldW = canvas_text_width(MENU_KEYCAP_PROMPT, FONT_MONO);
  {
    const int hoverW = canvas_text_width(MENU_KEYCAP_HOVER, FONT_MONO);
    const int nameW  = canvas_text_width(BIND_LABEL_WIDEST, FONT_MONO);
    if (hoverW > fieldW) fieldW = hoverW;
    if (nameW  > fieldW) fieldW = nameW;
  }
  fieldW += 2 * FIELD_PAD;
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };

  // Hover is read off the FIELD, since the invitation is written in it.
  const bool hovered = ui_hit(field, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  canvas_text(r.x, r.y + 6, label, pres_content_colour(C_BONE), FONT_UI);
  const bool armed = s_keycap.phase == KEYCAP_ARMED && s_keycap.row == row;
  const int vk = config_bind_get(g_cfg, row);
  // The hover lifts the well, not the text: fill C_WELL -> C_RAISED, rim C_RAISED -> C_BONEDIM, text stays C_BONE. The
  // rim is HAZARD yellow while the row is in capture.
  canvas_round_rect(field.x, field.y, field.w, field.h, 4,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, 4, 1,
                            pres_content_colour(armed ? C_HAZARD
                                                      : pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  const char* value = bind_field_text(armed, hovered, vk,
                                      MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER);
  const int valueW = canvas_text_width(value, FONT_MONO);
  // The text's y: canvas_text places a glyph's quad top at pen y + glyph.by, so ink runs from 'A'.by (highest cap) to
  // 'y'.by + 'y'.h (deepest descender of the 29 strings this field can draw); centring that band keeps it off the rim.
  const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
  const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
  const int inkTop = capA.by;
  const int inkH   = (descY.by + descY.h) - inkTop;
  const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
  canvas_text(field.x + (field.w - valueW) / 2, textY, value,
              pres_content_colour(armed ? C_HAZARD : C_BONE), FONT_MONO);
  if (ui_row_accepts_click(true, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    KeyCapEvent e; e.kind = KEYCAP_EV_CLICK; e.row = row; e.vk = 0;
    s_keycap = keycap_step(s_keycap, e).state;
    s_pressClaimed = true;
  }
  // "Also bound in the game" under the row; an unreadable cfg means no warning.
  if (gamecfg_key_bound(s_gameCfg, vk))
    draw_placeholder(cursor, MENU_GAMECFG_NOTE);
}

void menu_draw(int screenW, int screenH) {
  s_pres.rowsThisFrame = 0;
  s_hoverRowKey = -1;
  s_hoverHint = nullptr;
  s_lastRowKey = -1;
  s_glyphHint = nullptr;
  if (!g_ui.down) s_pressSpent = false;
  // Released here since the field may not be drawn on the frame the button comes up.
  if (!g_ui.down) s_entryCaretDrag = false;
  s_pressClaimed = false;
  {
    const unsigned now = menu_now();
    s_frameDt = anim_dt_clamped(s_pres.lastMs, now);
    s_pres.lastMs = now;
    // input_menu_open() is the logical state; the open spring chases it.
    s_pres.openTarget = input_menu_open() ? 1 : 0;
    if (g_cfg.menuanim == ANIM_OFF) {
      s_pres.open.x = (double)s_pres.openTarget;
      s_pres.open.v = 0.0;
    } else {
      anim_spring_step(&s_pres.open, (double)s_pres.openTarget,
                       ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping, s_frameDt);
    }
    // An edge on openTarget so Glass's sweep runs once per open; stamped in every style so switching to Glass with the
    // panel open does not replay an unwatched opening.
    if (s_pres.openTarget && !s_pres.wasOpen) s_pres.openStartMs = now;
    s_pres.wasOpen = s_pres.openTarget;
  }
  // Everything below is in logical pixels; the factor must be back to UI_SCALE_DEFAULT on return, as the ESP draws
  // through the same canvas. `pct` holds the pre-drag scale while the button is down, so a layout that moved with the
  // value cannot slide a row from under the hand.
  const int settled = menu_applied_scale(screenH);
  const int pct = ui_scale_effective(settled, s_scaleHeld, g_ui.down);
  if (!g_ui.down) s_scaleHeld = settled;
  canvas_set_scale(pct);
  // The cursor arrives in DEVICE pixels and every hit test is logical, so it is converted once here; one missed
  // conversion is a row that draws right and cannot be clicked.
  const int deviceMx = g_ui.mx, deviceMy = g_ui.my;
  g_ui.mx = ui_unscale_px(deviceMx, pct);
  g_ui.my = ui_unscale_px(deviceMy, pct);
  // The panel is laid out against the LOGICAL screen, which keeps it anchored and vertically centred at every scale.
  const int logicalW = ui_unscale_px(screenW, pct);
  const int logicalH = ui_unscale_px(screenH, pct);

  // The click away, once, before any row: a press of either button outside the open field's well finishes it (the press
  // may land on an earlier row or nothing). A right press opening a field elsewhere still works: it reads the raw
  // `g_ui.rpressed`. Tested before the drag machine, or a resize in flight would leave the field open.
  if (s_entry.open && (g_ui.pressed || g_ui.rpressed) && !s_dragLive) {
    if (g_ui.pressed) { s_pressClaimed = true; s_pressSpent = true; }
    if (!ui_hit(s_entryFieldRect, g_ui.mx, g_ui.my)) entry_clickaway_commit();
  }

  // The drag, before anything is drawn: the live box overrides the ini keys during a move or resize. The grip wins over
  // a move (panel_drag_step asks first); other pixels reach PDRAG_MOVE via the rule at the foot.
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  PanelBox box = laid;
  bool gripHit = false;
  {
    const bool titleHit = ui_hit(panel_title_rect(laid, STRIP_H, STATUS_H), g_ui.mx, g_ui.my);
    // Tested against the box the grip is drawn at, not `laid`: they diverge during a live drag.
    gripHit = ui_hit(panel_grip_rect(s_dragLive ? s_dragBox : laid), g_ui.mx, g_ui.my);
    bool ended = false;
    s_drag = panel_drag_step(s_drag, g_ui.pressed, g_ui.down, titleHit, gripHit,
                             g_ui.mx, g_ui.my, laid, logicalW, logicalH, &box, &ended);
    s_dragLive = s_drag.phase != PDRAG_IDLE;
    if (ended) {
      // The box the frame before drew: with the button gone there is no mouse position to re-derive one from.
      box = s_dragBox;
      // A click that moved nothing stores nothing, or the ini write would freeze a default-positioned panel.
      if (box.x != laid.x || box.y != laid.y || box.w != laid.w || box.h != laid.h) {
        g_cfg.panelx = box.x; g_cfg.panely = box.y;
        g_cfg.panelw = box.w; g_cfg.panelh = box.h;
        g_cfgDirty = true;
      }
    } else if (s_dragLive) {
      s_dragBox = box;
    }
  }
  const UiRect p = { box.x, box.y, box.w, box.h };

  // Counts parent switches only for the hazard strip. Each accuracy switch is masked by its own pair's latch first, so a
  // fingerprint mismatch on Chaingun or Shotgun (sharing ACC_PAIR_HITSCAN) latches both off.
  const int liveCount = menu_live_count(g_cfg.fovon, g_cfg.espenemies,
                                        g_cfg.espitems, g_cfg.espsecrets,
                                        g_cfg.espexplodables, g_cfg.espswitches,
                                        g_cfg.aimon, g_cfg.silenton,
                                        g_cfg.freecamon,
                                        accuracy_row_live(g_cfg.accpistol,   accuracy_pair_latched(ACC_PAIR_PISTOL))   ? 1 : 0,
                                        accuracy_row_live(g_cfg.accchaingun, accuracy_pair_latched(ACC_PAIR_HITSCAN))  ? 1 : 0,
                                        accuracy_row_live(g_cfg.accshotgun,  accuracy_pair_latched(ACC_PAIR_HITSCAN))  ? 1 : 0,
                                        accuracy_row_live(g_cfg.accexpander, accuracy_pair_latched(ACC_PAIR_EXPANDER)) ? 1 : 0,
                                        g_cfg.dmgnumbers, g_cfg.secrettriggers, g_cfg.tboton,
                                        g_cfg.espbreakables);
  const unsigned strip = liveCount ? C_RED : C_HAZARD;

  // The whole panel moves as one canvas-wide transform (canvas.h), not a second visual cursor through every draw call,
  // so every ui_hit still tests the logical layout. Set in every style so a style change leaves no motion standing; not
  // put back, since canvas_begin clears it each frame and the suite reads it back (canvas_record_transform).
  if (g_cfg.menuanim == ANIM_SIMPLE) {
    const int slideY = (int)((1.0 - s_pres.open.x) * (double)SIMPLE_SLIDE_PX + 0.5);
    canvas_set_transform(0, -slideY, 1.0, 0, 0);
  } else if (glass_style()) {
    // One factor condenses everything the panel draws, about its own centre; scaling the slab alone left the rows proud
    // of the frame for ~200 ms.
    canvas_set_transform(0, 0, GLASS_SLAB_MIN + (1.0 - GLASS_SLAB_MIN) * s_pres.open.x,
                         p.x + p.w / 2, p.y + p.h / 2);
  } else {
    canvas_set_transform(0, 0, 1.0, 0, 0);
  }

  if (glass_style()) {
    // The slab is drawn at full size (the condense is the transform's). `openP`, the progress into the condense, damps
    // the backdrop's wobble to nothing and ends where the highlight sweep starts.
    const double openP = anim_progress(s_pres.openStartMs, s_pres.lastMs, GLASS_CONDENSE_MS);
    // The capture comes first: canvas_backdrop_capture reads the back buffer, so anything already drawn would be blurred
    // back onto the slab. A failed capture leaves the tint alone.
    canvas_backdrop_capture(p.x, p.y, p.w, p.h);
    canvas_backdrop_draw(p.x, p.y, p.w, p.h, GLASS_R, pres_colour(C_GLASS_TINT),
                         GLASS_WOBBLE_AMP * (1.0 - openP), openP,
                         GLASS_BLUR_TAPS, GLASS_BLUR_RADIUS);
    // The rim: a real one-pixel ring (canvas.h says why a filled shape could not), the only edge this style has.
    canvas_round_rect_outline(p.x, p.y, p.w, p.h, GLASS_R, 1, pres_colour(C_GLASS_RIM));
    canvas_gradient_rect(p.x, p.y, p.w, GLASS_TOP_H, pres_colour(C_GLASS_TOP),
                         canvas_with_alpha(C_GLASS_TOP, 0));
    // One highlight per open, after the condense, clipped to the slab and measured from openStartMs so it cannot restart
    // while the panel stays open.
    const unsigned age = s_pres.lastMs - s_pres.openStartMs;
    if (s_pres.openTarget && age >= GLASS_CONDENSE_MS &&
        age < GLASS_CONDENSE_MS + GLASS_SWEEP_MS) {
      const double sp = anim_progress(s_pres.openStartMs + GLASS_CONDENSE_MS,
                                      s_pres.lastMs, GLASS_SWEEP_MS);
      int bx = p.x - GLASS_SWEEP_W + (int)((p.w + GLASS_SWEEP_W) * sp + 0.5);
      int bw = GLASS_SWEEP_W;
      if (bx < p.x) { bw -= (p.x - bx); bx = p.x; }
      if (bx + bw > p.x + p.w) bw = p.x + p.w - bx;
      if (bw > 0) canvas_glow_rect(bx, p.y, bw, p.h, glass_glow(1.0 - sp));
    }
    // The hazard strip rides the slab's top edge: the two share the transform.
    canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);
  } else {
    canvas_bevel(p.x, p.y, p.w, p.h,
                pres_colour(C_PANEL), pres_colour(C_BEVELHI), pres_colour(C_BEVELLO));
    canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);
  }

  const int sy = p.y + 1 + STRIP_H;
  canvas_rect(p.x + 1, sy, p.w - 2, STATUS_H, pres_colour(glass_ground(C_RECESS)));
  canvas_rect(p.x + PAD, sy + 11, 9, 9, pres_colour(strip));
  char status[32];
  if (liveCount) sprintf_s(status, "%d cheat%s live", liveCount, liveCount == 1 ? "" : "s");
  else           sprintf_s(status, "Idle");
  canvas_text(p.x + PAD + 18, sy + 7, status, pres_colour(C_BONEDIM), FONT_SMALL);
  canvas_text(p.x + p.w - 100, sy + 7, "Ins closes", pres_colour(C_BONEFNT), FONT_SMALL);

  const int bodyY = sy + STATUS_H;
  const int bodyH = p.y + p.h - bodyY - 1;

  canvas_rect(p.x + 1, bodyY, RAIL_W, bodyH, pres_colour(glass_ground(C_WELL)));
  // The Glass pill springs along the rail's vertical axis: the tabs share x and width, so pillX carries the pill's Y
  // and pillW its height. Drawn before the loop so labels sit on top of it.
  if (glass_style()) {
    const double ty = (double)(bodyY + s_tab * 46), th = 46.0;
    if (s_pres.pillW.x <= 0.0) {
      s_pres.pillX.x = ty; s_pres.pillX.v = 0.0;
      s_pres.pillW.x = th; s_pres.pillW.v = 0.0;
    } else {
      anim_spring_step(&s_pres.pillX, ty, ANIM_SPRING_GLASS.stiffness,
                       ANIM_SPRING_GLASS.damping, s_frameDt);
      anim_spring_step(&s_pres.pillW, th, ANIM_SPRING_GLASS.stiffness,
                       ANIM_SPRING_GLASS.damping, s_frameDt);
    }
    const AnimPill pill = anim_pill_stretch(s_pres.pillX.x, s_pres.pillW.x,
                                            s_pres.pillX.v, GLASS_PILL_STRETCH, GLASS_PILL_MAX);
    canvas_round_rect(p.x + 1, (int)(pill.x + 0.5), RAIL_W, (int)(pill.w + 0.5),
                      GLASS_ROW_R, pres_colour(C_GLASS_TINT));
  }
  // The rail's click is recorded here and applied after the content: a rail press is also a click away from an open
  // field whose commit is keyed to its tab, so menu_set_tab runs at the foot, after the rows consumed the commit.
  int railTab = -1;
  for (int i = 0; i < TAB_COUNT; i++) {
    const UiRect t = { p.x + 1, bodyY + i * 46, RAIL_W, 46 };
    if (i == s_tab) {
      if (!glass_style()) canvas_rect(t.x, t.y, t.w, t.h, pres_colour(C_PANEL));
      canvas_rect(t.x, t.y, 3, t.h, pres_colour(TAB_COLOURS[i]));
    }
    // Centred in the 46px entry; canvas_text takes the top of the line box.
    const int nameY = t.y + (t.h - font_height[FONT_UI]) / 2;
    canvas_text(t.x + RAIL_LABEL_X, nameY, TAB_NAMES[i], pres_colour(i == s_tab ? C_BONE : C_BONEDIM), FONT_UI);
    if (g_ui.pressed && ui_hit(t, g_ui.mx, g_ui.my)) {
      railTab = i;
      // The press is the rail's; without this the move rule would drag the panel.
      s_pressClaimed = true;
    }
  }

  // A tab switch snaps every row's spring on its first frame of content and starts the crossfade. The rail click's
  // switch is applied at the foot, so the frame seen here is the one after the click.
  s_tabSwitchedThisFrame = (s_tab != s_pres.tabFrom);
  if (s_tabSwitchedThisFrame) s_pres.tabSwitchMs = s_pres.lastMs;
  s_pres.tabFrom = s_tab;

  UiRect cursor = { p.x + 1 + RAIL_W + PAD, bodyY + PAD, p.w - RAIL_W - 2 * PAD - 2, bodyH - 2 * PAD };
  switch (s_tab) {
    case TAB_VIEW: {
      g_cfg.fovon = row_switch(cursor, "Custom field of view", g_cfg.fovon, C_GREEN, true);
      g_cfg.fov   = row_slider(cursor, "Field of view", g_cfg.fov, FOV_MIN, FOV_MAX, "",
                               g_cfg.fovon != 0, FOV_DEFAULT);
      char defaultMsg[48];
      sprintf_s(defaultMsg, "Updates as you drag. Default is %d degrees.", FOV_DEFAULT);
      row_hint(defaultMsg);

      draw_group(cursor, "FREE CAMERA");
      // The parent switch decides whether the toggle key acts; the camera starts inactive every launch (freecam.h).
      g_cfg.freecamon = row_switch(cursor, "Free camera", g_cfg.freecamon, C_GREEN, true);
      const bool freecamOn = combat_sub_enabled(g_cfg.freecamon);
      g_cfg.freecamspeed = row_slider_sub(cursor, "Speed", g_cfg.freecamspeed,
                                          FREECAM_SPEED_MIN, FREECAM_SPEED_MAX, "%",
                                          freecamOn, FREECAM_SPEED_DEFAULT);
      g_cfg.freecamshowplayer = row_switch_sub(cursor, "Show player",
                                               g_cfg.freecamshowplayer, C_GREEN, freecamOn);
      // Latched unavailable, session-only: the switch stays live and still writes g_cfg, since the latch must not
      // overwrite a saved setting.
      g_cfg.freecamnocull = row_switch_sub(cursor,
          nocull_row_unavailable() ? "Disable culling (Unavailable)" : "Disable culling",
          g_cfg.freecamnocull, C_GREEN, freecamOn);
      row_hint("Draws every sector in view while flying. Slow on large maps.");
      // A camera flying through solid rock must report some sector or displayrooms draws nothing: off holds the last
      // room it was really in; on seeds from the nearest sector's walls.
      g_cfg.freecamnearest = row_switch_sub(cursor, "Nearest sector",
                                            g_cfg.freecamnearest, C_GREEN, freecamOn);
      row_hint("Out of bounds. No effect while Disable culling is on.");
      g_cfg.freecamcutaway = row_switch_sub(cursor, "Cutaway view", g_cfg.freecamcutaway,
                                            C_GREEN, freecamOn);
      row_hint("Draws the map by depth so it reads from outside");
      // The sky is the engine's own drawing, which leaves thin gaps at a roof's edge from outside; off is plain black.
      g_cfg.freecamcutawaysky = row_switch_sub(cursor, "Cutaway sky", g_cfg.freecamcutawaysky,
                                               C_GREEN, freecamOn && g_cfg.freecamcutaway != 0);
      row_hint("Sky while a cutaway draws. Off is black.");
      // The cutaway work's diagnostic readout: Ctrl+Insert copies its lines, Shift+Insert flies to a copied pose.
      g_cfg.freecamdebug = row_switch_sub(cursor, "Debug readout", g_cfg.freecamdebug,
                                          C_GREEN, freecamOn);
      row_hint("Camera pose and paint counts while flying.");
      // Defaults on: without it the camera flies through a teleporter or water surface and comes out in the wrong half.
      g_cfg.freecamtransport = row_switch_sub(cursor, "Follow teleporters",
                                              g_cfg.freecamtransport, C_GREEN, freecamOn);
      row_hint("Water and teleporters carry the camera, as they do the player.");
      // Shows the teleport key's destination before the key is pressed, from the same plan the key acts on.
      g_cfg.freecamteleportmarker = row_switch_sub(cursor, "Teleport marker",
                                                   g_cfg.freecamteleportmarker, C_GREEN, freecamOn);
      row_hint("Shows where the teleport key will drop you.");
      g_cfg.freecamfog = row_switch_sub(cursor, "Remove fog", g_cfg.freecamfog, C_GREEN, freecamOn);
      row_hint("Distance fog off while flying.");
      g_cfg.freecamfullbright = row_switch_sub(cursor,
          light_row_unavailable() ? "Fullbright (Unavailable)" : "Fullbright",
          g_cfg.freecamfullbright, C_GREEN, freecamOn);
      row_hint("No shading or fog while flying.");
      g_cfg.freecamshot = row_switch_sub(cursor, "Screenshot mode", g_cfg.freecamshot, C_GREEN, freecamOn);
      row_hint("Hides weapon, crosshair, HUD and markers while flying.");
      break;
    }
    case TAB_SETTINGS: {
      // Every binding this panel owns, in one group. All always live: the feature's switch decides whether the key
      // acts, the row only which key.
      draw_group(cursor, "HOTKEYS");
      // The panel's own key first. A hand-edited ini can leave it unmappable; bind_menu_key_effective falls back to
      // Insert while this row reports what is stored.
      row_bind(cursor, "Panel key", BIND_ROW_MENU);
      row_bind(cursor, "Free camera key", BIND_ROW_CAMERA);
      row_bind(cursor, "Teleport key", BIND_ROW_TELEPORT);
      row_bind(cursor, "Aimbot button", BIND_ROW_AIM);

      // Near the top so UI scale stays reachable if everything else clips off the panel. Always live.
      {
        // The range is what this screen can apply; knob and readout show the applied scale. The reserve is the literal
        // "200%", so the track is the same width on every screen. Drawing is not a write: a stored value above the
        // ceiling is left alone and only a drag stores, so a taller screen gives the player their number back.
        const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
        char scaleText[16];
        sprintf_s(scaleText, "%d%%", settled);
        const int v = row_slider_at(cursor, "UI scale", settled, UI_SCALE_MIN, ceiling,
                                    scaleText, "200%", true, 0, C_BONE,
                                    EK_INT, UI_SCALE_DEFAULT);
        if (v != settled) g_cfg.uiscale = v;
      }

      draw_group(cursor, "AUTOSAVE");
      {
        // A range whose 0 is off needs no parent switch ("Off", not "0 min"). row_slider_at directly, because EK_INT
        // keeps the typed value and the reset glyph. The slot is the ini's autosaveslot; the hint names what is overwritten.
        char minsText[16], hint[64];
        const int mins = autosave_mins_clamped(g_cfg.autosavemins);
        if (mins == 0) strcpy_s(minsText, "Off"); else sprintf_s(minsText, "%d min", mins);
        const int v = row_slider_at(cursor, "Autosave", mins,
                                    AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX, minsText, "30 min",
                                    true, 0, C_BONE, EK_INT, AUTOSAVE_MINS_DEFAULT);
        if (v != mins) g_cfg.autosavemins = autosave_mins_clamped(v);
        sprintf_s(hint, "Saves to slot %d every N minutes; 0 is off",
                  autosave_slot_clamped(g_cfg.autosaveslot));
        row_hint(hint);
      }

      draw_group(cursor, "ANIMATIONS");
      // A plain on/off switch over the three-valued style: anim_switch_apply is the whole rule, so a stored Glass
      // survives every redraw.
      g_cfg.menuanim = anim_switch_apply(g_cfg.menuanim, row_switch(cursor, "Animations",
          g_cfg.menuanim != ANIM_OFF, C_GREEN, true));
      row_hint("Fades and slides in the panel");

      draw_group(cursor, "DIAGNOSTICS");
      g_cfg.debuglog = row_switch(cursor, "Debug log", g_cfg.debuglog, C_GREEN, true);
      row_hint("Writes duke3dmod.log beside the game. Off by default.");
      break;
    }
    case TAB_VISUALS: {
      // Each row writes straight back into g_cfg, so a toggle takes effect on the next ESP frame with the panel open.
      draw_group(cursor, "ENEMIES");
      g_cfg.espenemies  = row_switch(cursor, "Show enemies", g_cfg.espenemies, C_RED, true);
      // Inert, not merely greyed, while the parent is off: a click behind a greyed label would change a setting the
      // player was told was unavailable.
      const bool enemiesOn = g_cfg.espenemies != 0;
      g_cfg.espboxes    = row_switch_sub(cursor, "Bounding boxes",  g_cfg.espboxes,    C_RED, enemiesOn);
      g_cfg.esphealth   = row_switch_sub(cursor, "Health bars",     g_cfg.esphealth,   C_RED, enemiesOn);
      g_cfg.espnames    = row_switch_sub(cursor, "Name and health", g_cfg.espnames,    C_RED, enemiesOn);
      g_cfg.espdistance = row_switch_sub(cursor, "Distance",        g_cfg.espdistance, C_RED, enemiesOn);

      draw_group(cursor, "WORLD");
      // Each parent draws on its own, so none is gated by the others; each accent matches its marker's colour.
      g_cfg.espitems    = row_switch(cursor, "Items and pickups", g_cfg.espitems,   C_BLUE,   true);
      g_cfg.espsecrets  = row_switch(cursor, "Secret areas",      g_cfg.espsecrets, C_HAZARD, true);
      // Inert while its parent is off; hazard yellow since it draws the same secret's room.
      const bool secretsOn = g_cfg.espsecrets != 0;
      g_cfg.secretvolume = row_switch_sub(cursor, "Secret volume", g_cfg.secretvolume,
                                          C_HAZARD, secretsOn);
      row_hint("Draws each secret room's walls in the world");
      // The route to the nearest secret. The hint says "a hint" because the search sees geometry only, not a keycard,
      // a locked activator or a one-way drop.
      g_cfg.espsecretpath = row_switch_sub(cursor, "Path to a secret", g_cfg.espsecretpath,
                                           C_HAZARD, secretsOn);
      row_hint("Route to the nearest secret. A hint, not a guarantee.");
      g_cfg.espexplodables = row_switch(cursor, "Explodables", g_cfg.espexplodables,
                                        C_HAZARD, true);
      row_hint("Cracks that blow up when shot");
      g_cfg.espbreakables = row_switch(cursor, "Breakables", g_cfg.espbreakables,
                                       C_HAZARD, true);
      row_hint("Glass and props the engine breaks");
      // Blue like "Items and pickups" on purpose: the two markers never share a shape.
      g_cfg.espswitches = row_switch(cursor, "Switches and locks", g_cfg.espswitches,
                                     C_BLUE, true);
      g_cfg.dmgnumbers = row_switch(cursor, "Damage numbers", g_cfg.dmgnumbers,
                                    C_RED, true);
      row_hint("Shows the damage your hits do over the target");
      // The chain from a hidden touchplate to the thing its lotag operates, when that is a secret sector or shares a
      // wall with one. Hazard yellow since it draws the other end of the secret chain.
      g_cfg.secrettriggers = row_switch(cursor, "Secret triggers", g_cfg.secrettriggers,
                                        C_HAZARD, true);
      row_hint("Hidden triggers whose chain ends at a secret");

      draw_group(cursor, "RANGE");
      // Always live, shared by every category on this tab. Bounds are the cull's own (esp_layout.h). Stored in Build
      // units, shown in metres like "Aim range", so the reserve follows ESP_DRAWDIST_MAX.
      {
        char buf[16], reserve[16];
        sprintf_s(buf, "%d m", g_cfg.espdrawdist / BUILD_UNITS_PER_METRE);
        sprintf_s(reserve, "%d m", ESP_DRAWDIST_MAX / BUILD_UNITS_PER_METRE);
        g_cfg.espdrawdist = row_slider_at(cursor, "Draw distance", g_cfg.espdrawdist,
                                          ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX, buf, reserve,
                                          true, 0, C_BONE,
                                          EK_METRES, ESP_DRAWDIST_DEFAULT);
      }
      row_hint("Markers stop at this range.");
      // Its own term in the cull, not a magic slider value: it means "no limit at all".
      g_cfg.espnolimit = row_switch_sub(cursor, "Unlimited range", g_cfg.espnolimit,
                                        C_GREEN, true);
      row_hint("Ignores the distance. Slow on a large map.");

      // Always live. Same "(Unavailable)" suffix as the View tab's Fullbright, off the same session latch.
      g_cfg.fullbright = row_switch(cursor,
          light_row_unavailable() ? "Fullbright (Unavailable)" : "Fullbright",
          g_cfg.fullbright, C_BLUE, true);
      row_hint("No shading or fog. Textures reload once.");
      break;
    }
    case TAB_COMBAT: {
      // Options for both aim modes: enabled when either parent is on (combat_shared_enabled); gating on aimon alone
      // left them dark with silent aim on.
      const bool targetingOn = combat_shared_enabled(g_cfg.aimon, g_cfg.silenton);
      draw_group(cursor, "TARGETING");
      // "Pick target" and "Aim at" read a name through row_slider_named: each picks a dimension, not a feature.
      g_cfg.aimtarget = row_slider_named(cursor, "Pick target", g_cfg.aimtarget, 0, 1,
                                         aim_target_label, targetingOn);
      g_cfg.aimpoint  = row_slider_named(cursor, "Aim at", g_cfg.aimpoint, 0, 1,
                                         aim_point_label, targetingOn);
      g_cfg.aimcone   = row_slider(cursor, "Cone", g_cfg.aimcone,
                                   AIM_CONE_MIN, AIM_CONE_MAX, "", targetingOn,
                                   AIM_CONE_DEFAULT);
      // How far a candidate may be, independent of the Visuals tab's "Draw distance" (target.h). Shown in metres.
      {
        char buf[16], reserve[16];
        sprintf_s(buf, "%d m", g_cfg.aimrange / BUILD_UNITS_PER_METRE);
        sprintf_s(reserve, "%d m", AIM_RANGE_MAX / BUILD_UNITS_PER_METRE);
        g_cfg.aimrange = row_slider_at(cursor, "Aim range", g_cfg.aimrange,
                                       AIM_RANGE_MIN, AIM_RANGE_MAX, buf, reserve,
                                       targetingOn, 0, C_BONE,
                                       EK_METRES, AIM_RANGE_DEFAULT);
      }
      g_cfg.aimlead   = row_switch(cursor, "Lead moving targets", g_cfg.aimlead, C_RED, targetingOn);
      // A percentage of the computed lead; not gated by "Lead moving targets", since every row here shares targetingOn.
      g_cfg.aimleadscale = row_slider(cursor, "Lead amount", g_cfg.aimleadscale,
                                      AIM_LEADSCALE_MIN, AIM_LEADSCALE_MAX, "%", targetingOn,
                                      AIM_LEADSCALE_DEFAULT);
      g_cfg.aimdebug  = row_switch(cursor, "Show aim marker", g_cfg.aimdebug, C_RED, targetingOn);

      draw_group(cursor, "AIMBOT");
      g_cfg.aimon = row_switch(cursor, "Aimbot", g_cfg.aimon, C_RED, true);
      // Only active once aimon specifically is on, so combat_sub_enabled's single-parent rule, not the either/or one.
      const bool aimOn = combat_sub_enabled(g_cfg.aimon);
      g_cfg.aimhold = row_switch_sub(cursor, "Hold key", g_cfg.aimhold, C_RED, aimOn);
      g_cfg.aimsmooth = row_slider_sub(cursor, "Smoothing", g_cfg.aimsmooth,
                                       AIM_SMOOTH_MIN, AIM_SMOOTH_MAX, "%", aimOn,
                                       AIM_SMOOTH_DEFAULT);

      // One hint line per group, hung on the switch it is about rather than under the group's last row.
      draw_group(cursor, "TRIGGERBOT");
      g_cfg.tboton = row_switch(cursor, "Triggerbot", g_cfg.tboton, C_RED, true);
      row_hint("Fires by itself while the condition below holds");
      const bool tbotOn = combat_sub_enabled(g_cfg.tboton);
      g_cfg.tbotwhen = row_slider_named_sub(cursor, "Fire when", g_cfg.tbotwhen,
                                            TBOT_WHEN_CROSSHAIR, TBOT_WHEN_AIMBOT,
                                            tbot_when_label, tbotOn);
      g_cfg.tbotdelay = row_slider_sub(cursor, "Delay", g_cfg.tbotdelay,
                                       TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, "ms", tbotOn,
                                       TBOT_DELAY_MS_DEFAULT);

      draw_group(cursor, "SILENT AIM");
      g_cfg.silenton = row_switch(cursor, "Silent aim", g_cfg.silenton, C_RED, true);
      const bool silentOn = combat_sub_enabled(g_cfg.silenton);
      g_cfg.silentmaxangle = row_slider_sub(cursor, "Max redirect", g_cfg.silentmaxangle,
                                            SILENT_MAXANGLE_MIN, SILENT_MAXANGLE_MAX, "",
                                            silentOn, SILENT_MAXANGLE_DEFAULT);
      break;
    }
    case TAB_WEAPONS: {
      // Four parents, each decided on its own from the shot's `atwith` (accuracy.h), all top-level and always live.
      draw_group(cursor, "PERFECT ACCURACY");
      // A latched pair's row gets the "(Unavailable)" suffix so a switch reading "on" while doing nothing is not silent;
      // it still writes g_cfg on a click, since the latch is session-only and must not overwrite the saved value.
      const bool pistolLatched   = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_PISTOL));
      const bool chaingunLatched = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_HITSCAN));
      const bool shotgunLatched  = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_HITSCAN));
      const bool expanderLatched = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_EXPANDER));
      g_cfg.accpistol   = row_switch(cursor, pistolLatched   ? "Pistol (Unavailable)"   : "Pistol",
                                     g_cfg.accpistol,   C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");
      g_cfg.accchaingun = row_switch(cursor, chaingunLatched ? "Chaingun (Unavailable)" : "Chaingun",
                                     g_cfg.accchaingun, C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");
      g_cfg.accshotgun  = row_switch(cursor, shotgunLatched  ? "Shotgun (Unavailable)"  : "Shotgun",
                                     g_cfg.accshotgun,  C_HAZARD, true);
      row_hint("Removes the spread; pellets land in one hole.");
      g_cfg.accexpander = row_switch(cursor, expanderLatched ? "Expander (Unavailable)" : "Expander",
                                     g_cfg.accexpander, C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");

      draw_group(cursor, "MOUSE AIM");
      g_cfg.mouseaimon = row_switch(cursor, "Modern mouse aim", g_cfg.mouseaimon, C_HAZARD, true);
      row_hint("Raw input, equal axes, no dead zone.");
      const bool mouseAimOn = g_cfg.mouseaimon != 0;
      {
        // mouseaimsens is degrees TIMES 1000 (mouseaim.h), so integer part and remainder are the digits, no floating
        // point. The reserve is the widest string (MOUSEAIM_SENS_MAX = 200 -> "0.200").
        char sensText[16];
        sprintf_s(sensText, "%d.%03d", g_cfg.mouseaimsens / 1000, g_cfg.mouseaimsens % 1000);
        // EK_MILLI: the one row whose typed text carries a decimal point.
        g_cfg.mouseaimsens = row_slider_at(cursor, "Sensitivity", g_cfg.mouseaimsens,
                                           MOUSEAIM_SENS_MIN, MOUSEAIM_SENS_MAX, sensText, "0.200",
                                           mouseAimOn, SUB_INDENT, C_BONEDIM,
                                           EK_MILLI, MOUSEAIM_SENS_DEFAULT);
      }
      g_cfg.mouseaimvscale = row_slider_sub(cursor, "Vertical scale", g_cfg.mouseaimvscale,
                                            MOUSEAIM_VSCALE_MIN, MOUSEAIM_VSCALE_MAX, "%",
                                            mouseAimOn, MOUSEAIM_VSCALE_DEFAULT);

      // Two ranges with no parent switch: 100 is each one's identity (the DLL writes no player byte). A drag is snapped
      // through firerate_rate_clamped like the loader, so the knob lands on the 50s. Only a drag writes.
      draw_group(cursor, "WEAPON SPEED");
      {
        char rateText[16];
        sprintf_s(rateText, "%d%%", g_cfg.firerate);
        const int dragged = row_slider_at(cursor, "Fire rate", g_cfg.firerate,
                                          FIRERATE_MIN, FIRERATE_MAX, rateText, "400%",
                                          true, 0, C_BONE, EK_INT, FIRERATE_DEFAULT);
        if (dragged != g_cfg.firerate) g_cfg.firerate = firerate_rate_clamped(dragged);
      }
      row_hint("Compresses every frame that is not a shot.");
      {
        char switchText[16];
        sprintf_s(switchText, "%d%%", g_cfg.switchspeed);
        // The same default and clamp as the fire rate, from the same constant.
        const int dragged = row_slider_at(cursor, "Weapon switch speed", g_cfg.switchspeed,
                                          FIRERATE_MIN, FIRERATE_MAX, switchText, "400%",
                                          true, 0, C_BONE, EK_INT, FIRERATE_DEFAULT);
        if (dragged != g_cfg.switchspeed) g_cfg.switchspeed = firerate_rate_clamped(dragged);
      }
      row_hint("Faster lowering and raising when you switch");
      break;
    }
  }

  // After the content: the row that owned an open field has taken the value the click-away committed, so the tab can
  // change.
  if (railTab >= 0) menu_set_tab(railTab);

  // The resize grip: three diagonals in the lower-right triangle, drawn after the content so no row covers the corner.
  // C_BONEFNT at rest (furniture, not a control), C_BONEDIM only while the cursor is on it.
  {
    const UiRect g = panel_grip_rect(box);
    const unsigned gc = pres_colour(gripHit ? C_BONEDIM : C_BONEFNT);
    for (int i = 1; i <= 3; i++) {
      const int off = i * 4;
      canvas_line(g.x + off, g.y + g.h - 1, g.x + g.w - 1, g.y + off, gc);
    }
  }

  // The tooltip, last so it is on top. Stepped once per frame here, not in row_hint; `s_hoverRowKey` -1 is the "left"
  // state. Either button puts it away (a right press on a numeric row opens a field).
  s_tooltip = tooltip_step(s_tooltip, s_hoverRowKey, g_ui.pressed || g_ui.rpressed,
                           s_pres.lastMs, g_cfg.menuanim, s_frameDt);
  // While an entry field is open the player types exactly where a tooltip would sit, so it stands down on every row.
  const bool entryFieldOpen = s_entry.open != 0;
  // The glyph's hint wins: the cursor cannot be on a reset glyph and off its row, so it is the more specific answer.
  const char* const hint = s_glyphHint ? s_glyphHint : s_hoverHint;
  s_tooltipText = nullptr;
  // Not while the panel is closing: `openTarget` is false from the key press while the fade-out runs with the cursor
  // frozen, so the rest timer would reach its delay and bring the box back over a panel already going.
  if (s_tooltip.shown && s_tooltip.alpha > 0.0 && hint && !entryFieldOpen &&
      s_pres.openTarget) {
    const UiRect b = tooltip_box_place(g_ui.mx, g_ui.my,
                                       canvas_text_width(hint, FONT_SMALL),
                                       font_height[FONT_SMALL], logicalW, logicalH);
    canvas_round_rect(b.x, b.y, b.w, b.h, TOOLTIP_R,
                      alpha_scale(C_WELL, s_tooltip.alpha));
    canvas_round_rect_outline(b.x, b.y, b.w, b.h, TOOLTIP_R, 1,
                              alpha_scale(C_RAISED, s_tooltip.alpha));
    canvas_text(b.x + TOOLTIP_PAD, b.y + TOOLTIP_PAD, hint,
                alpha_scale(C_BONE, s_tooltip.alpha), FONT_SMALL);
    s_tooltipText = hint;
  }

  // A left press no control claimed is a panel grab, the frame's last decision. `g_ui.down` too, since the suite can set a
  // press edge with the button up. A closing frame must not arm a move; s_dragBox/s_dragLive are stamped so a release no-ops.
  if (g_ui.pressed && g_ui.down && !s_pressClaimed && !s_dragLive &&
      s_pres.openTarget && ui_hit(p, g_ui.mx, g_ui.my)) {
    s_drag.phase = PDRAG_MOVE;
    s_drag.start = box;
    s_drag.grabDx = g_ui.mx - box.x;
    s_drag.grabDy = g_ui.my - box.y;
    s_dragBox = box;
    s_dragLive = true;
  }

  // Put back what was borrowed: the cursor is the caller's and the scale belongs to the ESP. The transform stays; the
  // suite asks what the panel was drawn with.
  g_ui.mx = deviceMx;
  g_ui.my = deviceMy;
  canvas_set_scale(UI_SCALE_DEFAULT);
}
