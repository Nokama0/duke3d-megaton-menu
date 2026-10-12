#pragma once
#include "ui.h"
#include "common.h"
#include "input.h"
#include "tbot.h"
#include "panel_layout.h"

// TAB_SETTINGS is not a cheat tab: nothing on it reaches menu_live_count.
enum { TAB_VISUALS = 0, TAB_COMBAT = 1, TAB_WEAPONS = 2, TAB_VIEW = 3, TAB_SETTINGS = 4, TAB_COUNT = 5 };

UiRect menu_panel_rect(int screenW, int screenH);
// The panel box in logical pixels: stored ini keys, or the live box of a drag/resize.
PanelBox menu_panel_box(int logicalW, int logicalH);
int    menu_active_tab();
void   menu_set_tab(int tab);
void   menu_draw(int screenW, int screenH);
int    menu_applied_scale(int screenH);

// Live cheat categories (hazard strip, status line). Counts parent switches only, never sub-settings, so a fresh
// install reports none; input preferences (mouseaimon, firerate/switchspeed) are excluded.
inline int menu_live_count(int fovon, int espenemies, int espitems, int espsecrets,
                           int espexplodables, int espswitches,
                           int aimon, int silenton, int freecamon,
                           int accpistol, int accchaingun, int accshotgun, int accexpander,
                           int dmgnumbers, int secrettriggers, int tboton,
                           int espbreakables = 0) {
  return (fovon       ? 1 : 0) + (espenemies  ? 1 : 0)
       + (espitems    ? 1 : 0) + (espsecrets  ? 1 : 0)
       + (espexplodables ? 1 : 0) + (espswitches ? 1 : 0)
       + (aimon       ? 1 : 0) + (silenton    ? 1 : 0)
       + (freecamon   ? 1 : 0)
       + (accpistol   ? 1 : 0) + (accchaingun ? 1 : 0)
       + (accshotgun  ? 1 : 0) + (accexpander ? 1 : 0)
       + (dmgnumbers  ? 1 : 0) + (secrettriggers ? 1 : 0)
       + (tboton      ? 1 : 0) + (espbreakables  ? 1 : 0);
}

// Menu log lines, named here because input.cpp cannot link into test_host.
const char* const MENU_LOG_OPEN  = "menu: open";
const char* const MENU_LOG_CLOSE = "menu: close";

const char* const MENU_KEYCAP_PROMPT = "Press a key";
const char* const MENU_GAMECFG_NOTE  = "Also bound in the game";
// Hotkey field text while hovered with nothing armed; armed beats hovered (bind_field_text).
const char* const MENU_KEYCAP_HOVER  = "Click to set";

const char* const MENU_RESET_HINT = "Reset to default";

// Reads the game's own cfg for the "Also bound in the game" note, once per panel open. A null path or unopenable file
// leaves it empty.
void menu_gamecfg_load(const char* path);

bool menu_keycap_armed();

// Binds vk to the armed row and saves; no-op with none armed; refuses what keycap_step refuses (0, mouse 1).
void menu_keycap_bind(int vk);

void menu_keycap_cancel();

// Disarms because the panel is closing; a capture left armed across a close would eat the next in-game key.
void menu_keycap_close();

// One numeric entry field for the whole panel; rules in entry.h.
bool menu_entry_open();

// Hands one SDL scancode to the open field: 41 cancels, 40/88 commit, 42 deletes; no-op with no field.
void menu_entry_key(int sc);

// Poll-hook variant of menu_entry_key adding caret keys (Left/Right 80/79, Home/End 74/77, Delete 76) and Ctrl+A/C/X
// (4/6/27). Returns the clipboard text on Ctrl+C/X, else nullptr; input.cpp owns the syscall.
const char* menu_entry_key_mod(int sc, bool shift, bool ctrl);

// Types s over the selection through the typed-key rule, so a pasted "FOV: 90" delivers "90".
void menu_entry_paste(const char* s);

void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text);

// Closes the field because the panel is closing; input.cpp routes above its menu-open check, so it would eat keys.
void menu_entry_close();

const char* menu_entry_text();

// The last committed value and its row, or false; the slider helper clears the flag on its next draw.
bool menu_entry_pending(int* rowKey, int* value);

void menu_set_now_ms(unsigned ms);

// True while the panel fades out: logical state is gone but the open spring has not settled. Always false under ANIM_OFF.
bool menu_closing_animation_active();

const char* menu_tooltip_text();

const int RAIL_W = 116;
const int RAIL_LABEL_X = 12;

const int STRIP_H = 15, STATUS_H = 30, ROW_H = 30, ROW_GAP = 4, PAD = 14;

// Value column's right margin, exported so the suite can tell right-aligned slider readouts from centred hotkey fields.
const int VALUE_MARGIN = 10;

inline bool combat_sub_enabled(int parentValue) { return parentValue != 0; }

// Whether Combat's TARGETING rows are enabled: shared by both aim modes, so on whenever either parent is.
inline bool combat_shared_enabled(int aimon, int silenton) {
  return aimon != 0 || silenton != 0;
}

// "Pick target" shows a NAME: it picks a dimension (TargetMode); any nonzero reads as "closest".
inline const char* aim_target_label(int aimtarget) {
  return aimtarget ? "Closest" : "Crosshair";
}

inline const char* aim_point_label(int aimpoint) {
  return aimpoint ? "Upper" : "Centre";
}

inline const char* tbot_when_label(int when) {
  return tbot_when_clamped(when) == TBOT_WHEN_AIMBOT ? "Aimbot locked" : "Crosshair on enemy";
}
