#pragma once
#include "project.h"
#include "common.h"
#include "canvas.h"
#include "font.h"

// ESP marker layout (brackets, health bar, labels) given a box. Pure, so every placement is pinned. Everything widens
// to long long and clamps back: project_world saturates coordinates to the full int range.

const int ESP_BRACKET_T    = 2;   // arm thickness
const int ESP_BRACKET_DIV  = 3;   // arm length is edge/this
const int ESP_BRACKET_MIN  = 3;

const int ESP_BAR_W   = 3;
const int ESP_BAR_GAP = 3;

const int ESP_LABEL_GAP = 17;
const int ESP_DIST_GAP  = 2;

// Arm length is a fraction of the edge, capped at (dim-1)/2: opposing arms of dim/2 on an even edge would MEET and
// merge markers into a grid in a crowd. The cap overrides the minimum.
inline int esp_bracket_arm(int dim) {
  if (dim <= 0) return 0;
  int a = dim / ESP_BRACKET_DIV;
  if (a < ESP_BRACKET_MIN) a = ESP_BRACKET_MIN;
  const int cap = (dim - 1) / 2;
  if (a > cap) a = cap;
  return a < 0 ? 0 : a;
}

inline int esp_bracket_thickness(int armX, int armY) {
  int t = ESP_BRACKET_T;
  if (t > armX) t = armX;
  if (t > armY) t = armY;
  return t < 0 ? 0 : t;
}

// The projected box when all four corners came back, else the anchor box (a hard pitch can reject the top pair alone).
inline ScreenBox esp_box_or_anchor(const ScreenBox& box, int anchorX, int anchorY) {
  if (box.valid) return box;
  return proj_anchor_box(anchorX, anchorY);
}

inline int esp_label_x(int boxX, int boxW, int textW) {
  return clamp_ll_to_int((long long)boxX + (long long)boxW / 2 - (long long)textW / 2);
}

inline int esp_name_label_y(int boxY) {
  return clamp_ll_to_int((long long)boxY - ESP_LABEL_GAP);
}
inline int esp_dist_label_y(int boxY, int boxH) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH + ESP_DIST_GAP);
}

inline int esp_bar_x(int boxX) {
  return clamp_ll_to_int((long long)boxX - ESP_BAR_GAP - ESP_BAR_W);
}
inline int esp_bar_fill_y(int boxY, int boxH, int fill) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH - (long long)fill);
}

// Draw FAR to NEAR so the near label wins; STABLE at equal distances (strict `<`) so equidistant markers don't flicker.
enum { ESP_KIND_SECRET = 0, ESP_KIND_ITEM = 1, ESP_KIND_ENEMY = 2, ESP_KIND_EXPLODABLE = 3,
       ESP_KIND_SWITCH = 4 };
const int ESP_MARKER_MAX = 256;
struct EspMarker {
  int kind, index, dist2d, seq;
  ScreenPt p;
  ScreenBox box;
};
inline void esp_sort_far_to_near(EspMarker* m, int count) {
  if (!m || count < 2) return;
  if (count > ESP_MARKER_MAX) count = ESP_MARKER_MAX;
  for (int i = 1; i < count; i++) {
    const EspMarker key = m[i];
    int j = i - 1;
    while (j >= 0 && m[j].dist2d < key.dist2d) { m[j + 1] = m[j]; j--; }
    m[j + 1] = key;
  }
}

const int ESP_LABEL_RUN_GAP = 4;
struct EspLabelRuns { int nameX, numX, totalW; };
inline EspLabelRuns esp_label_runs(int boxX, int boxW, int nameW, int numW) {
  EspLabelRuns r = { 0, 0, 0 };
  const int gap = (nameW > 0 && numW > 0) ? ESP_LABEL_RUN_GAP : 0;
  r.totalW = clamp_ll_to_int((long long)nameW + gap + numW);
  r.nameX = esp_label_x(boxX, boxW, r.totalW);
  r.numX = clamp_ll_to_int((long long)r.nameX + nameW + gap);
  return r;
}

inline int esp_label_mono_y(int nameY, int smallH, int monoH) {
  return clamp_ll_to_int((long long)nameY + smallH - monoH);
}

// Is this pickup still present? cstat bit 15 (invisible) is set by GAME.CON's getcode/getweaponcode when respawn is on,
// cleared by `respawnit`; moveactors sets it for one excluded tile (VA 0x004a3ea7). Sign-extended it reads -32768:
// mask, don't compare.
inline bool esp_item_present(int cstat, int xrepeat) {
  return (cstat & SPR_CSTAT_INVISIBLE) == 0 && xrepeat > 0;
}

// Whether any ESP category is on (esp_any_on opens the GL bracket on this); here so a missing category fails the suite
// instead of drawing nothing. aimmarker, playermarker and notice use the same bracket, so each is its own parameter.
inline bool esp_any_category_on(int espenemies, int espitems, int espsecrets,
                                int aimmarker, int playermarker, int notice,
                                int espexplodables, int espswitches,
                                int espbreakables = 0) {
  return espenemies != 0 || espitems != 0 || espsecrets != 0
      || aimmarker != 0 || playermarker != 0 || notice != 0
      || espexplodables != 0 || espswitches != 0 || espbreakables != 0;
}

// The teleport notice: centred, y a FRACTION of screen height (right at 1024x768 and 3440x1440).
const int ESP_NOTICE_Y_DIVISOR = 8;

inline int esp_notice_x(int screenW, int textW) {
  const int x = (screenW - textW) / 2;
  return x < 0 ? 0 : x;
}
inline int esp_notice_y(int screenH) {
  const int y = screenH / ESP_NOTICE_Y_DIVISOR;
  return y < 0 ? 0 : y;
}

const int ESP_SECRET_EXIT_NOTE_MARGIN = 8;
const char* const ESP_SECRET_EXIT_NOTE = "Secret exit in this level";
inline int esp_secret_exit_note_x() { return ESP_SECRET_EXIT_NOTE_MARGIN; }
inline int esp_secret_exit_note_y() { return ESP_SECRET_EXIT_NOTE_MARGIN; }

// Takes the resolved flag because esp.cpp can't link into test_host but canvas_text can (NO_GL).
inline void esp_draw_secret_exit_note(bool show) {
  if (!show) return;
  canvas_text(esp_secret_exit_note_x(), esp_secret_exit_note_y(),
              ESP_SECRET_EXIT_NOTE, canvas_esp_secret_colour(), FONT_SMALL);
}

// Draws only when its switch AND a consumer it instruments are on (aim_acquire_should_run, target.h).
inline bool aim_marker_on(int aimon, int silenton, int aimdebug) {
  return (aimon != 0 || silenton != 0) && aimdebug != 0;
}

// The secret marker is a CROSS on the sector centre, not a box (a room's box would fill the screen close up).
const int ESP_SECRET_ARM = 5;   // half-length of each arm
const int ESP_SECRET_T   = 2;

const char* const ESP_SECRET_LABEL = "Secret";

const char* const ESP_TOUCH_EXIT_LABEL = "Secret exit";

inline int esp_secret_bar_span() { return ESP_SECRET_ARM * 2 + 1; }

inline int esp_secret_hbar_x(int cx) {
  return clamp_ll_to_int((long long)cx - ESP_SECRET_ARM);
}
inline int esp_secret_hbar_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2);
}
inline int esp_secret_vbar_x(int cx) {
  return clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2);
}
inline int esp_secret_vbar_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM);
}

inline int esp_secret_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM - ESP_LABEL_GAP);
}
inline int esp_secret_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_SECRET_ARM + 1 + ESP_DIST_GAP);
}

// Keycard/lock mark: a small rounded-rect outline with a 3px filled notch (the magnetic stripe); colour from
// keycard_colour(pal).
const int ESP_CARD_W = 14;
const int ESP_CARD_H = 10;
const int ESP_CARD_R = 2;
const int ESP_CARD_T = 1;
const int ESP_CARD_NOTCH = 3;

inline int esp_card_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_CARD_W / 2); }
inline int esp_card_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2); }

inline int esp_card_notch_x(int cx) { return esp_card_x(cx); }
inline int esp_card_notch_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_CARD_NOTCH / 2);
}

inline int esp_card_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2 - ESP_LABEL_GAP);
}
inline int esp_card_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_CARD_H / 2 + ESP_DIST_GAP);
}

// Outline, notch, label and optional second line in one colour; shared by the keycard pickup and the SW_ACCESS lock.
inline void esp_draw_card_mark(const ScreenPt& p, unsigned colour,
                               const char* label, const char* line2) {
  canvas_round_rect_outline(esp_card_x(p.x), esp_card_y(p.y),
                            ESP_CARD_W, ESP_CARD_H, ESP_CARD_R, ESP_CARD_T, colour);
  canvas_rect(esp_card_notch_x(p.x), esp_card_notch_y(p.y),
             ESP_CARD_NOTCH, ESP_CARD_NOTCH, colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_card_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_card_dist_label_y(p.y), line2, colour, FONT_SMALL);
}

// The two bars every point marker in esp.cpp uses (secret, explodable, plain switch, touch exit).
inline void esp_draw_cross_mark(const ScreenPt& p, unsigned colour,
                                const char* label, const char* line2) {
  canvas_rect(esp_secret_hbar_x(p.x), esp_secret_hbar_y(p.y),
              esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(p.x), esp_secret_vbar_y(p.y),
              ESP_SECRET_T, esp_secret_bar_span(), colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_secret_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_secret_dist_label_y(p.y), line2, colour, FONT_SMALL);
}

// A HOLLOW square at the RAW aim point (target_aim_z) and a FILLED one at the LED point (target_lead): they coincide
// for a hitscan weapon and separate for RPG/devastator/freezer, making the lead solver visible.
const int AIM_MARKER_HALF = 4;   // the square spans 2*this + 1
const int AIM_MARKER_T    = 1;

const char* const AIM_MARKER_LABEL = "Aim";

inline int aim_marker_span() { return AIM_MARKER_HALF * 2 + 1; }

inline int aim_marker_x(int cx) {
  return clamp_ll_to_int((long long)cx - AIM_MARKER_HALF);
}
inline int aim_marker_y(int cy) {
  return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF);
}

inline int aim_marker_edge_far(int c) {
  return clamp_ll_to_int((long long)c + AIM_MARKER_HALF + 1 - AIM_MARKER_T);
}

inline int aim_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF - ESP_LABEL_GAP);
}

// Free camera's player marker: a DIAMOND labelled "You", so it can't read as a target.
const int PLAYER_MARKER_HALF = 6;

const char* const PLAYER_MARKER_LABEL = "You";

inline int player_marker_rows() { return PLAYER_MARKER_HALF * 2 + 1; }

inline int player_marker_row_halfwidth(int row) {
  const int d = row - PLAYER_MARKER_HALF;
  const int w = PLAYER_MARKER_HALF - (d < 0 ? -d : d);
  return w < 0 ? 0 : w;
}

inline int player_marker_row_x(int cx, int row) {
  return clamp_ll_to_int((long long)cx - player_marker_row_halfwidth(row));
}
inline int player_marker_row_y(int cy, int row) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF + row);
}
inline int player_marker_row_w(int row) { return player_marker_row_halfwidth(row) * 2 + 1; }

inline int player_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF - ESP_LABEL_GAP);
}

// The live camera state, not the toggle flag.
inline bool freecam_marker_on(bool freecamActive, int freecamshowplayer) {
  return freecamActive && freecamshowplayer != 0;
}

// The one range cull, shared by every category, in BUILD units (512/metre). Compared against dist2d, never depth (depth
// scales with viewingrange, so it moves with the FOV slider). config.cpp clamps with this function.
const int ESP_DRAWDIST_MIN     = 512;
const int ESP_DRAWDIST_MAX     = 131072;

// Default is the maximum: an active cull would blank markers past a room's far wall, reading as a broken projection.
const int ESP_DRAWDIST_DEFAULT = ESP_DRAWDIST_MAX;

inline int esp_draw_distance_clamped(int maxdist) {
  return clamp_int(maxdist, ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX);
}

inline bool esp_within_draw_distance(int dist2d, int maxdist) {
  if (dist2d < 0) return false;
  return dist2d <= esp_draw_distance_clamped(maxdist);
}

// The cull off altogether, its own term so the slider keeps meaning metres. Costs frame time on a large map.
inline bool esp_range_unlimited(int noLimit) { return noLimit != 0; }
inline bool esp_within_draw_distance_or_unlimited(int dist2d, int maxdist, int noLimit) {
  if (dist2d < 0) return false;
  return esp_range_unlimited(noLimit) ? true : esp_within_draw_distance(dist2d, maxdist);
}

// Pinned so WHICH FIELD is read (dist2d, not depth) is a suite check: a fixture can set them differently.
inline bool esp_marker_in_range(const ScreenPt& p, int maxdist) {
  return esp_within_draw_distance(p.dist2d, maxdist);
}
inline bool esp_marker_in_range(const ScreenPt& p, int maxdist, int noLimit) {
  return esp_within_draw_distance_or_unlimited(p.dist2d, maxdist, noLimit);
}
