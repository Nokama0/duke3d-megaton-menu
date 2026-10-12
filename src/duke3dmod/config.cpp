#include "config.h"
#include "common.h"
#include "log.h"
#include "fov.h"
#include "esp_layout.h"   // ESP_DRAWDIST_DEFAULT, esp_draw_distance_clamped
#include "target.h"       // AIM_CONE_DEFAULT, aim_cone_clamped, aim_smooth_clamped
#include "input.h"        // MOUSE_BUTTON_MIN/MAX for the legacy aimbutton SDL button
#include "bind.h"         // VK_BIND_XBUTTON2, bind_from_sdl_mouse, BIND_ROW_* enum
#include "freecam.h"      // FREECAM_SPEED_DEFAULT, freecam_speed_clamped
// ui.h: UI_SCALE_DEFAULT, ui_scale_clamped (the 75..200 bound, stated once).
#include "ui.h"
#include "mouseaim.h"     // MOUSEAIM_SENS/VSCALE_DEFAULT, their _clamped functions
#include "firerate.h"     // FIRERATE_MIN/MAX/STEP, firerate_rate_clamped
#include "tbot.h"
#include "anim.h"         // ANIM_OFF, anim_style_clamped
#include "autosave.h"     // AUTOSAVE_*_DEFAULT, autosave_mins_clamped, autosave_slot_clamped
// Every include above exists so a setting's bounds live once, with the code that acts on it.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Config g_cfg;
bool g_cfgDirty = false;

// Lines we did not recognise, kept verbatim so a newer config survives an older DLL.
// Capacity is deliberately limited; lines beyond it are silently dropped.
static char s_unknown[128][CONFIG_LINE_BUFSIZE];
static int  s_unknownCount = 0;

static void set_defaults() {
  g_cfg.menukey  = 0x2D;   // VK_INSERT
  g_cfg.debuglog = 0;
  g_cfg.uiscale  = UI_SCALE_DEFAULT;
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
  g_cfg.fovon    = 0;
  // FOV_DEFAULT, not a literal 90: the View tab's reset glyph restores the same constant.
  g_cfg.fov      = FOV_DEFAULT;
  g_cfg.espenemies = 0;
  // On, unlike espenemies: these do nothing until the parent switch is on, so defaulting them on makes it useful at
  // once.
  g_cfg.espboxes    = 1;
  g_cfg.esphealth   = 1;
  g_cfg.espnames    = 1;
  g_cfg.espdistance = 1;
  g_cfg.espitems    = 0;   // parent switch, every feature defaults off
  g_cfg.espsecrets  = 0;
  g_cfg.espexplodables = 0;
  g_cfg.espbreakables  = 0;
  g_cfg.espswitches    = 0;
  g_cfg.dmgnumbers     = 0;
  g_cfg.secrettriggers = 0;
  g_cfg.secretvolume   = 0;   // sub-switch of espsecrets
  g_cfg.espsecretpath  = 0;   // sub-switch of espsecrets too
  // A RANGE: the default is the MAXIMUM, so it culls nothing (ESP_DRAWDIST_DEFAULT, esp_layout.h).
  g_cfg.espdrawdist = ESP_DRAWDIST_DEFAULT;
  g_cfg.espnolimit = 0;
  g_cfg.aimon     = 0;
  g_cfg.aimhold   = 1;   // hold to aim: the conservative half of an off switch
  g_cfg.aimbind   = VK_BIND_XBUTTON2;   // Mouse 5, the outer thumb button
  g_cfg.aimtarget = 0;   // nearest the crosshair
  g_cfg.aimpoint  = 0;   // mid-body, the widest part of the target
  g_cfg.aimsmooth = AIM_SMOOTH_DEFAULT;   // snap: the whole remaining turn, every tic
  g_cfg.aimcone   = AIM_CONE_DEFAULT;
  g_cfg.aimlead   = 1;   // sub-option of a switch already off
  g_cfg.aimleadscale = AIM_LEADSCALE_DEFAULT;   // a tuning default, see config.h
  g_cfg.aimrange = AIM_RANGE_DEFAULT;    // independent of espdrawdist; defaults to max
  g_cfg.aimdebug  = 0;
  g_cfg.silenton       = 0;
  g_cfg.silentmaxangle = SILENT_MAXANGLE_DEFAULT;
  // Toggle key V, not F6: F6 is stock Megaton's Quick_Save and silently saved the game on each toggle; V is unbound.
  g_cfg.freecamon         = 0;
  g_cfg.freecamkey        = 0x56;   // 'V'
  g_cfg.freecamteleportkey = 0x54;  // 'T'
  g_cfg.freecamspeed      = FREECAM_SPEED_DEFAULT;
  g_cfg.freecamshowplayer = 0;
  g_cfg.freecamnocull     = 0;
  g_cfg.freecamnearest    = 0;
  g_cfg.freecamfog        = 0;
  g_cfg.freecamfullbright = 0;
  g_cfg.freecamshot       = 0;
  g_cfg.fullbright        = 0;
  g_cfg.mouseaimon     = 0;
  g_cfg.mouseaimsens   = MOUSEAIM_SENS_DEFAULT;
  g_cfg.mouseaimvscale = MOUSEAIM_VSCALE_DEFAULT;
  g_cfg.mouseaimleadcap = 0;
  // Perfect accuracy: the shot's own atwith decides everything else (accuracy.h).
  g_cfg.accpistol   = 0;
  g_cfg.accchaingun = 0;
  g_cfg.accshotgun  = 0;
  g_cfg.accexpander = 0;
  // Fire rate: a RANGE, 100 is its identity stop (nothing advanced below it).
  g_cfg.firerate = FIRERATE_DEFAULT;
  g_cfg.menuanim = ANIM_OFF;   // panel draws as it always did
  g_cfg.switchspeed = FIRERATE_DEFAULT;   // same identity-stop range as firerate
  g_cfg.tboton = 0;
  g_cfg.tbotwhen = TBOT_WHEN_CROSSHAIR;
  g_cfg.tbotdelay = TBOT_DELAY_MS_DEFAULT;
  g_cfg.autosavemins = AUTOSAVE_MINS_DEFAULT;   // 0: off, like every other feature
  g_cfg.autosaveslot = AUTOSAVE_SLOT_DEFAULT;
  g_cfg.freecamcutaway = 0;
  g_cfg.freecamcutawaysky = 1;
  g_cfg.freecamdebug = 0;
  g_cfg.freecamtransport = 1;
  g_cfg.freecamteleportmarker = 0;
}

static void trim(char* s) {
  size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

void config_load(const char* path) {
  set_defaults();
  s_unknownCount = 0;
  g_cfgDirty = false;

  FILE* f = nullptr;
  if (fopen_s(&f, path, "r") != 0 || !f) return;

  // Whether the NEW `aimbind` was seen, so a legacy `aimbutton` after it cannot overwrite it.
  bool sawAimbind = false;
  // Whether the legacy value that decided aimbind was Mouse 1; reported after the whole file is read, so a value that
  // supersedes it is never falsely logged as reset.
  bool resetMouse1 = false;
  // `triggervolume` is the pre-rename spelling of `secretvolume`; same seen-first rule as sawAimbind.
  bool sawSecretVolume = false;
  char line[CONFIG_LINE_BUFSIZE];
  for (;;) {
    line[sizeof(line) - 1] = 1;            // fill mark for config_line_maybe_truncated
    if (!fgets(line, sizeof(line), f)) break;
    // A filled buffer means truncation OR a line exactly filling it: peek one char; EOF or '\n' means complete,
    // anything else means the rest must be drained.
    if (config_line_maybe_truncated(line, sizeof(line))) {
      int c = fgetc(f);
      if (c != EOF && c != '\n') {         // more of this line follows: truly truncated
        while (c != '\n' && c != EOF) c = fgetc(f);
        continue;                          // skip the fragment entirely
      }
    }

    trim(line);
    if (!line[0] || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char* key = line;
    int val = atoi(eq + 1);

    if      (!strcmp(key, "menukey"))  g_cfg.menukey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "debuglog")) g_cfg.debuglog = val ? 1 : 0;
    else if (!strcmp(key, "uiscale"))  g_cfg.uiscale  = ui_scale_clamped(val);
    else if (!strcmp(key, "panelx"))   g_cfg.panelx   = val < -1 ? -1 : val;
    else if (!strcmp(key, "panely"))   g_cfg.panely   = val < -1 ? -1 : val;
    else if (!strcmp(key, "panelw"))   g_cfg.panelw   = val < 0 ? 0 : val;
    else if (!strcmp(key, "panelh"))   g_cfg.panelh   = val < 0 ? 0 : val;
    else if (!strcmp(key, "fovon"))    g_cfg.fovon    = val ? 1 : 0;
    else if (!strcmp(key, "fov"))      g_cfg.fov      = clamp_int(val, FOV_MIN, FOV_MAX);
    else if (!strcmp(key, "espenemies")) g_cfg.espenemies = val ? 1 : 0;
    else if (!strcmp(key, "espboxes"))    g_cfg.espboxes    = val ? 1 : 0;
    else if (!strcmp(key, "esphealth"))   g_cfg.esphealth   = val ? 1 : 0;
    else if (!strcmp(key, "espnames"))    g_cfg.espnames    = val ? 1 : 0;
    else if (!strcmp(key, "espdistance")) g_cfg.espdistance = val ? 1 : 0;
    else if (!strcmp(key, "espitems"))    g_cfg.espitems    = val ? 1 : 0;
    else if (!strcmp(key, "espsecrets"))  g_cfg.espsecrets  = val ? 1 : 0;
    else if (!strcmp(key, "espexplodables")) g_cfg.espexplodables = val ? 1 : 0;
    else if (!strcmp(key, "espbreakables"))  g_cfg.espbreakables  = val ? 1 : 0;
    else if (!strcmp(key, "espswitches"))    g_cfg.espswitches    = val ? 1 : 0;
    else if (!strcmp(key, "dmgnumbers"))     g_cfg.dmgnumbers     = val ? 1 : 0;
    else if (!strcmp(key, "secrettriggers")) g_cfg.secrettriggers = val ? 1 : 0;
    else if (!strcmp(key, "secretvolume")) { g_cfg.secretvolume = val ? 1 : 0; sawSecretVolume = true; }
    // Pre-rename spelling, read once and never written back, so the rename happens on the first save.
    else if (!strcmp(key, "triggervolume")) { if (!sawSecretVolume) g_cfg.secretvolume = val ? 1 : 0; }
    else if (!strcmp(key, "espsecretpath")) g_cfg.espsecretpath = val ? 1 : 0;
    else if (!strcmp(key, "espdrawdist")) g_cfg.espdrawdist = esp_draw_distance_clamped(val);
    else if (!strcmp(key, "espnolimit")) g_cfg.espnolimit = clamp_int(val, 0, 1);
    else if (!strcmp(key, "aimon"))     g_cfg.aimon     = val ? 1 : 0;
    else if (!strcmp(key, "aimhold"))   g_cfg.aimhold   = val ? 1 : 0;
    // A binding (virtual key, not SDL button), clamped like menukey.
    else if (!strcmp(key, "aimbind")) { g_cfg.aimbind = clamp_int(val, 1, 254); sawAimbind = true; }
    // Pre-migration `aimbutton` held an SDL BUTTON NUMBER; reading it into aimbind would shift every hold button by one
    // (5 == VK XBUTTON1, not XBUTTON2), so bind_from_sdl_mouse converts. Out of range keeps the default (clamping would
    // bind Mouse 1, the click that arms a capture). `aimbind` wins wherever it appears.
    else if (!strcmp(key, "aimbutton")) {
      if (!sawAimbind) {
        const int vk = bind_from_sdl_mouse(val);
        // 0 covers out-of-range and Mouse 1; only Mouse 1 is worth logging, since the player chose it.
        g_cfg.aimbind = vk ? vk : VK_BIND_XBUTTON2;
        resetMouse1 = (vk == 0 && val == MOUSE_BUTTON_MIN);
      }
    }
    else if (!strcmp(key, "aimtarget")) g_cfg.aimtarget = val ? 1 : 0;
    else if (!strcmp(key, "aimpoint"))  g_cfg.aimpoint  = val ? 1 : 0;
    else if (!strcmp(key, "aimsmooth")) g_cfg.aimsmooth = aim_smooth_clamped(val);
    else if (!strcmp(key, "aimcone"))   g_cfg.aimcone   = aim_cone_clamped(val);
    else if (!strcmp(key, "aimlead"))   g_cfg.aimlead   = val ? 1 : 0;
    else if (!strcmp(key, "aimleadscale")) g_cfg.aimleadscale = aim_leadscale_clamped(val);
    else if (!strcmp(key, "aimrange")) g_cfg.aimrange = aim_range_clamped(val);   // independent of espdrawdist
    else if (!strcmp(key, "aimdebug"))  g_cfg.aimdebug  = val ? 1 : 0;
    else if (!strcmp(key, "silenton"))       g_cfg.silenton       = val ? 1 : 0;
    else if (!strcmp(key, "silentmaxangle")) g_cfg.silentmaxangle = silent_maxangle_clamped(val);
    else if (!strcmp(key, "freecamon"))         g_cfg.freecamon   = val ? 1 : 0;
    else if (!strcmp(key, "freecamkey"))        g_cfg.freecamkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamteleportkey")) g_cfg.freecamteleportkey = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamspeed"))      g_cfg.freecamspeed = freecam_speed_clamped(val);
    // Retired: turn rate derives from mouse aim's setting (freecam_sens_from_mouseaim, freecam.h). Recognised and
    // discarded so an old ini's freecamsens= does not survive forever in s_unknown's write-back.
    else if (!strcmp(key, "freecamsens")) { }
    else if (!strcmp(key, "freecamshowplayer")) g_cfg.freecamshowplayer = val ? 1 : 0;
    else if (!strcmp(key, "freecamnocull"))     g_cfg.freecamnocull     = val ? 1 : 0;
    else if (!strcmp(key, "freecamnearest"))    g_cfg.freecamnearest    = val ? 1 : 0;
    else if (!strcmp(key, "freecamfog"))        g_cfg.freecamfog        = val ? 1 : 0;
    else if (!strcmp(key, "freecamfullbright")) g_cfg.freecamfullbright = val ? 1 : 0;
    else if (!strcmp(key, "freecamshot"))       g_cfg.freecamshot       = val ? 1 : 0;
    else if (!strcmp(key, "fullbright"))        g_cfg.fullbright        = val ? 1 : 0;
    else if (!strcmp(key, "mouseaimon"))     g_cfg.mouseaimon     = val ? 1 : 0;
    else if (!strcmp(key, "mouseaimsens"))   g_cfg.mouseaimsens   = mouseaim_sens_clamped(val);
    else if (!strcmp(key, "mouseaimvscale")) g_cfg.mouseaimvscale = mouseaim_vscale_clamped(val);
    else if (!strcmp(key, "mouseaimleadcap")) g_cfg.mouseaimleadcap = clamp_int(val, 0, 1024);
    else if (!strcmp(key, "accpistol"))   g_cfg.accpistol   = val ? 1 : 0;
    else if (!strcmp(key, "accchaingun")) g_cfg.accchaingun = val ? 1 : 0;
    else if (!strcmp(key, "accshotgun"))  g_cfg.accshotgun  = val ? 1 : 0;
    else if (!strcmp(key, "accexpander")) g_cfg.accexpander = val ? 1 : 0;
    else if (!strcmp(key, "firerate")) g_cfg.firerate = firerate_rate_clamped(val);
    else if (!strcmp(key, "menuanim")) g_cfg.menuanim = anim_style_clamped(val);
    else if (!strcmp(key, "switchspeed")) g_cfg.switchspeed = firerate_rate_clamped(val);
    else if (!strcmp(key, "tboton"))    g_cfg.tboton = val ? 1 : 0;
    else if (!strcmp(key, "tbotwhen"))  g_cfg.tbotwhen = tbot_when_clamped(val);
    else if (!strcmp(key, "tbotdelay")) g_cfg.tbotdelay = tbot_delay_ms_clamped(val);
    else if (!strcmp(key, "autosavemins")) g_cfg.autosavemins = autosave_mins_clamped(val);
    else if (!strcmp(key, "autosaveslot")) g_cfg.autosaveslot = autosave_slot_clamped(val);
    else if (!strcmp(key, "freecamcutaway")) g_cfg.freecamcutaway = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamcutawaysky")) g_cfg.freecamcutawaysky = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamdebug")) g_cfg.freecamdebug = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamtransport")) g_cfg.freecamtransport = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamteleportmarker")) g_cfg.freecamteleportmarker = clamp_int(val, 0, 1);
    else if (s_unknownCount < 128) {
      *eq = '=';
      strncpy_s(s_unknown[s_unknownCount++], line, _TRUNCATE);
    }
  }
  fclose(f);
  // AFTER the whole file, not at the line: see resetMouse1's declaration.
  if (resetMouse1 && !sawAimbind) LOGI("%s", CONFIG_LOG_AIMBUTTON1_RESET);
}

bool config_save(const char* path) {
  FILE* f = nullptr;
  if (fopen_s(&f, path, "w") != 0 || !f) return false;
  fputs("[nukepanel]\n", f);
  fprintf(f, "menukey=%d\n",  g_cfg.menukey);
  fprintf(f, "debuglog=%d\n", g_cfg.debuglog);
  fprintf(f, "uiscale=%d\n",  g_cfg.uiscale);
  fprintf(f, "panelx=%d\n",   g_cfg.panelx);
  fprintf(f, "panely=%d\n",   g_cfg.panely);
  fprintf(f, "panelw=%d\n",   g_cfg.panelw);
  fprintf(f, "panelh=%d\n",   g_cfg.panelh);
  fprintf(f, "fovon=%d\n",    g_cfg.fovon);
  fprintf(f, "fov=%d\n",      g_cfg.fov);
  fprintf(f, "espenemies=%d\n", g_cfg.espenemies);
  fprintf(f, "espboxes=%d\n",    g_cfg.espboxes);
  fprintf(f, "esphealth=%d\n",   g_cfg.esphealth);
  fprintf(f, "espnames=%d\n",    g_cfg.espnames);
  fprintf(f, "espdistance=%d\n", g_cfg.espdistance);
  fprintf(f, "espitems=%d\n",    g_cfg.espitems);
  fprintf(f, "espsecrets=%d\n",  g_cfg.espsecrets);
  fprintf(f, "espexplodables=%d\n", g_cfg.espexplodables);
  fprintf(f, "espbreakables=%d\n",  g_cfg.espbreakables);
  fprintf(f, "espswitches=%d\n",    g_cfg.espswitches);
  fprintf(f, "dmgnumbers=%d\n",     g_cfg.dmgnumbers);
  fprintf(f, "secrettriggers=%d\n", g_cfg.secrettriggers);
  fprintf(f, "secretvolume=%d\n",   g_cfg.secretvolume);   // legacy triggervolume never written back
  fprintf(f, "espsecretpath=%d\n", g_cfg.espsecretpath);
  fprintf(f, "espdrawdist=%d\n", g_cfg.espdrawdist);
  fprintf(f, "espnolimit=%d\n", g_cfg.espnolimit);
  fprintf(f, "aimon=%d\n",     g_cfg.aimon);
  fprintf(f, "aimhold=%d\n",   g_cfg.aimhold);
  fprintf(f, "aimbind=%d\n", g_cfg.aimbind);   // legacy aimbutton never written back
  fprintf(f, "aimtarget=%d\n", g_cfg.aimtarget);
  fprintf(f, "aimpoint=%d\n",  g_cfg.aimpoint);
  fprintf(f, "aimsmooth=%d\n", g_cfg.aimsmooth);
  fprintf(f, "aimcone=%d\n",   g_cfg.aimcone);
  fprintf(f, "aimlead=%d\n",   g_cfg.aimlead);
  fprintf(f, "aimleadscale=%d\n", g_cfg.aimleadscale);
  fprintf(f, "aimrange=%d\n", g_cfg.aimrange);
  fprintf(f, "aimdebug=%d\n",  g_cfg.aimdebug);
  fprintf(f, "silenton=%d\n",       g_cfg.silenton);
  fprintf(f, "silentmaxangle=%d\n", g_cfg.silentmaxangle);
  fprintf(f, "freecamon=%d\n",         g_cfg.freecamon);
  fprintf(f, "freecamkey=%d\n",        g_cfg.freecamkey);
  fprintf(f, "freecamteleportkey=%d\n", g_cfg.freecamteleportkey);
  fprintf(f, "freecamspeed=%d\n",      g_cfg.freecamspeed);
  fprintf(f, "freecamshowplayer=%d\n", g_cfg.freecamshowplayer);
  fprintf(f, "freecamnocull=%d\n",     g_cfg.freecamnocull);
  fprintf(f, "freecamnearest=%d\n",    g_cfg.freecamnearest);
  fprintf(f, "freecamfog=%d\n",        g_cfg.freecamfog);
  fprintf(f, "freecamfullbright=%d\n", g_cfg.freecamfullbright);
  fprintf(f, "freecamshot=%d\n",       g_cfg.freecamshot);
  fprintf(f, "fullbright=%d\n",        g_cfg.fullbright);
  fprintf(f, "mouseaimon=%d\n",       g_cfg.mouseaimon);
  fprintf(f, "mouseaimsens=%d\n",     g_cfg.mouseaimsens);
  fprintf(f, "mouseaimvscale=%d\n",   g_cfg.mouseaimvscale);
  fprintf(f, "mouseaimleadcap=%d\n",  g_cfg.mouseaimleadcap);
  fprintf(f, "accpistol=%d\n",   g_cfg.accpistol);
  fprintf(f, "accchaingun=%d\n", g_cfg.accchaingun);
  fprintf(f, "accshotgun=%d\n",  g_cfg.accshotgun);
  fprintf(f, "accexpander=%d\n", g_cfg.accexpander);
  fprintf(f, "firerate=%d\n",    g_cfg.firerate);
  fprintf(f, "menuanim=%d\n",    g_cfg.menuanim);
  fprintf(f, "switchspeed=%d\n", g_cfg.switchspeed);
  fprintf(f, "tboton=%d\n",    g_cfg.tboton);
  fprintf(f, "tbotwhen=%d\n",  g_cfg.tbotwhen);
  fprintf(f, "tbotdelay=%d\n", g_cfg.tbotdelay);
  fprintf(f, "autosavemins=%d\n", g_cfg.autosavemins);
  fprintf(f, "autosaveslot=%d\n", g_cfg.autosaveslot);   // ini-only, no panel row
  fprintf(f, "freecamcutaway=%d\n", g_cfg.freecamcutaway);
  fprintf(f, "freecamcutawaysky=%d\n", g_cfg.freecamcutawaysky);
  fprintf(f, "freecamdebug=%d\n", g_cfg.freecamdebug);
  fprintf(f, "freecamtransport=%d\n", g_cfg.freecamtransport);
  fprintf(f, "freecamteleportmarker=%d\n", g_cfg.freecamteleportmarker);
  for (int i = 0; i < s_unknownCount; i++) fprintf(f, "%s\n", s_unknown[i]);
  fclose(f);
  g_cfgDirty = false;
  return true;
}

// The four capture rows drive DIFFERENT Config members; the row -> field mapping lives here once. An out-of-range row
// reads 0 and writes nothing (the index arrives from a click).
int config_bind_get(const Config& c, int row) {
  switch (row) {
    case BIND_ROW_MENU:     return c.menukey;
    case BIND_ROW_AIM:      return c.aimbind;
    case BIND_ROW_CAMERA:   return c.freecamkey;
    case BIND_ROW_TELEPORT: return c.freecamteleportkey;
    default:                return 0;
  }
}

void config_bind_set(Config& c, int row, int vk) {
  switch (row) {
    case BIND_ROW_MENU:     c.menukey            = vk; break;
    case BIND_ROW_AIM:      c.aimbind            = vk; break;
    case BIND_ROW_CAMERA:   c.freecamkey         = vk; break;
    case BIND_ROW_TELEPORT: c.freecamteleportkey = vk; break;
    default: break;
  }
}

// Every 0/1 field in Config, in ini-key order (matches config_save's order).
// Pointer-to-member, so a table entry naming the wrong field is a compile error.
struct ConfigSwitch { const char* name; int Config::*field; };
static const ConfigSwitch SWITCHES[] = {
  { "debuglog",          &Config::debuglog },
  { "fovon",              &Config::fovon },
  { "espenemies",         &Config::espenemies },
  { "espboxes",           &Config::espboxes },
  { "esphealth",          &Config::esphealth },
  { "espnames",           &Config::espnames },
  { "espdistance",        &Config::espdistance },
  { "espitems",           &Config::espitems },
  { "espsecrets",         &Config::espsecrets },
  { "espexplodables",     &Config::espexplodables },
  { "espbreakables",      &Config::espbreakables },
  { "espswitches",        &Config::espswitches },
  { "dmgnumbers",         &Config::dmgnumbers },
  { "secrettriggers",     &Config::secrettriggers },
  { "secretvolume",       &Config::secretvolume },
  { "espsecretpath",      &Config::espsecretpath },
  { "espnolimit",         &Config::espnolimit },
  { "aimon",              &Config::aimon },
  { "aimhold",            &Config::aimhold },
  { "aimtarget",          &Config::aimtarget },
  { "aimpoint",           &Config::aimpoint },
  { "aimlead",            &Config::aimlead },
  { "aimdebug",           &Config::aimdebug },
  { "silenton",           &Config::silenton },
  { "freecamon",          &Config::freecamon },
  { "freecamshowplayer",  &Config::freecamshowplayer },
  { "freecamnocull",      &Config::freecamnocull },
  { "freecamnearest",     &Config::freecamnearest },
  { "freecamfog",         &Config::freecamfog },
  { "freecamfullbright",  &Config::freecamfullbright },
  { "freecamshot",        &Config::freecamshot },
  { "fullbright",         &Config::fullbright },
  { "mouseaimon",         &Config::mouseaimon },
  { "accpistol",          &Config::accpistol },
  { "accchaingun",        &Config::accchaingun },
  { "accshotgun",         &Config::accshotgun },
  { "accexpander",        &Config::accexpander },
  { "tboton",             &Config::tboton },
};
static const int SWITCH_COUNT = sizeof(SWITCHES) / sizeof(SWITCHES[0]);

int config_switch_count() { return SWITCH_COUNT; }

const char* config_switch_name(int i) {
  return (i >= 0 && i < SWITCH_COUNT) ? SWITCHES[i].name : nullptr;
}

int config_switch_value(const Config& c, int i) {
  if (i < 0 || i >= SWITCH_COUNT) return 0;
  return (c.*(SWITCHES[i].field)) ? 1 : 0;
}

const char* config_state_line(const char* key, int oldV, int newV, char* buf, int cap) {
  if (!key || !buf || cap <= 0) return "";
  _snprintf_s(buf, cap, _TRUNCATE, "state: %s %d -> %d", key, oldV ? 1 : 0, newV ? 1 : 0);
  return buf;
}

int config_state_diff(const Config& prev, const Config& cur, int* outIdx, int cap) {
  if (!outIdx || cap <= 0) return 0;
  int n = 0;
  for (int i = 0; i < SWITCH_COUNT && n < cap; i++) {
    if (config_switch_value(prev, i) != config_switch_value(cur, i)) outIdx[n++] = i;
  }
  return n;
}

void config_log_state_changes(const Config& prev, const Config& cur) {
  int idx[SWITCH_COUNT];
  const int n = config_state_diff(prev, cur, idx, SWITCH_COUNT);
  for (int i = 0; i < n; i++) {
    char buf[CONFIG_STATE_LINE_CAP];
    config_state_line(config_switch_name(idx[i]),
                      config_switch_value(prev, idx[i]),
                      config_switch_value(cur, idx[i]),
                      buf, sizeof(buf));
    LOGI("%s", buf);
  }
}
