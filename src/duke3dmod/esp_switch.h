#pragma once
#include "canvas.h"
#include "esp_names.h"

// Keycard lock line and combination hint, pure. The set is read from checkhitswitch's case lists (VA 0x0050ea80;
// docs/re/switches-breakables.md A), never a names table. SW_EXIT is NUKEBUTTON (`line` is its label).
enum SwitchFamily { SW_NONE = 0, SW_ACCESS = 1, SW_TWOSTATE = 2, SW_MULTI = 3, SW_COMBINATION = 4,
                    SW_DOOR = 5, SW_EXIT = 6 };
struct SwitchInfo { SwitchFamily family; int base; const char* line; int exitKind; };
const char* const ESP_SWITCH_LABEL = "Switch";
const char* const ESP_DOOR_LABEL = "Usable";

// NUKEBUTTON rule from every DUKE3D.GRP map: pal 0 + lotag 65535 ends the level, pal 0 + lotag 32767 the episode,
// non-zero pal + small lotag is the secret exit (lotag = target level: E1L4 -> 6, E2L5 -> 10, E3L8 -> 11, E4L5 -> 11).
// Its runtime list is stat 5.
const int ESP_PIC_NUKEBUTTON = 142;
enum { EXIT_NONE = 0, EXIT_NORMAL = 1, EXIT_SECRET = 2 };
inline int exit_kind_for(int picnum, int pal, int lotag) {
  if (picnum != ESP_PIC_NUKEBUTTON) return EXIT_NONE;
  if (pal != 0 && lotag > 0 && lotag < 32767) return EXIT_SECRET;
  if (pal == 0 && (lotag == 65535 || lotag == 32767)) return EXIT_NORMAL;
  return EXIT_NONE;
}

const int ESP_STAT_SWITCH = 0;
const int ESP_STAT_NUKEBUTTON = 5;

// MULTISWITCH is the one FOUR-frame family: 146 -> 149 -> 146, activated channel lotag + (picnum - 146).
const int SWITCH_MULTI_BASE = 146;
const int SWITCH_MULTI_FRAMES = 4;

// isadoorwall, VA 0x0050be30: checkhitswitch also fires a door-textured wall/sprite with nonzero lotag.
inline bool isadoorwall(int picnum) {
  if (picnum >= 150 && picnum <= 159) return true;
  if (picnum == 395) return true;
  if (picnum >= 447 && picnum <= 449) return true;
  if (picnum == 717) return true;
  if (picnum == 781) return true;
  if (picnum == 1102) return true;
  if (picnum == 1144) return true;
  if (picnum == 1169) return true;
  if (picnum == 1178 || picnum == 1179) return true;
  if (picnum == 4391) return true;
  return false;
}

inline int switch_base_for(int picnum) {
  static const int BASES[] = {
    130, 132, 134, 136, 138, 140, 162, 164, 166, 168, 170,
    712, 860, 862, 864, 1111, 1122, 1142
  };
  if (picnum >= SWITCH_MULTI_BASE && picnum < SWITCH_MULTI_BASE + SWITCH_MULTI_FRAMES)
    return SWITCH_MULTI_BASE;
  for (int i = 0; i < (int)(sizeof(BASES) / sizeof(BASES[0])); i++)
    if (picnum == BASES[i] || picnum == BASES[i] + 1) return BASES[i];
  if (isadoorwall(picnum)) return picnum;
  return -1;
}

inline SwitchInfo switch_classify(int picnum, int pal, int hitag, int lotag, int gotAccess) {
  SwitchInfo s = { SW_NONE, -1, 0, EXIT_NONE };
  const int exitKind = exit_kind_for(picnum, pal, lotag & 0xFFFF);
  if (exitKind != EXIT_NONE) {
    s.family = SW_EXIT;
    s.exitKind = exitKind;
    s.line = (exitKind == EXIT_SECRET) ? "Secret exit" : "Exit";
    return s;
  }
  const int base = switch_base_for(picnum);
  if (base < 0) return s;
  if (lotag == 0) return s;
  s.base = base;
  if (base == 130 || base == 170) {
    s.family = SW_ACCESS;
    // Keycards are never consumed: an opened access switch sits on its pressed frame (base + 1) with got_access still
    // set, so the pressed frame alone means "already open".
    if (picnum == base + 1) return s;
    int bit = 0;
    const char* need = 0;
    if      (pal == 0)  { bit = 1; need = "Needs blue card";   }
    else if (pal == 21) { bit = 2; need = "Needs red card";    }
    else if (pal == 23) { bit = 4; need = "Needs yellow card"; }
    if (need && (gotAccess & bit) == 0) s.line = need;
    return s;
  }
  if (base == 162 || base == 166 || base == 1142) {
    s.family = SW_COMBINATION;
    const int want = hitag != 0 ? 1 : 0;
    s.line = (((picnum - base) & 1) != want) ? "Flip" : "Leave";
    return s;
  }
  if (isadoorwall(picnum)) {
    s.family = SW_DOOR;
    return s;
  }
  s.family = (base == SWITCH_MULTI_BASE) ? SW_MULTI : SW_TWOSTATE;
  return s;
}

inline const char* switch_label(SwitchFamily family) {
  return family == SW_DOOR ? ESP_DOOR_LABEL : ESP_SWITCH_LABEL;
}

// Same 0/21/23 -> blue/red/yellow as SW_ACCESS, so a card and its lock share a hue. 0 for no colour.
inline unsigned keycard_colour(int pal) {
  if (pal == ESP_PAL_KEY_BLUE)   return C_BLUE;
  if (pal == ESP_PAL_KEY_RED)    return C_RED;
  if (pal == ESP_PAL_KEY_YELLOW) return C_HAZARD;
  return 0;
}

inline bool esp_is_keycard(int picnum) { return picnum == ESP_PIC_ACCESSCARD; }

// Whether esp_world_tic's scan may spend an ESP_WALL_MAX slot on this wall: lotag == 0 is refused (an ordinary door
// would fill the snapshot); lotag -1 (end-of-level) stays capturable.
inline bool esp_wall_switch_capturable(int picnum, int lotag) {
  return switch_base_for(picnum) >= 0 && lotag != 0;
}
