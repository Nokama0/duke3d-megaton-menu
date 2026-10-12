#pragma once

// config_load's per-line read buffer (config.cpp's `char line[...]`), shared so it cannot drift from the boundary tests.
const int CONFIG_LINE_BUFSIZE = 512;

// fgets truncation test that a NUL inside the line cannot fool: the caller sets buf[size-1] non-zero before fgets, which
// overwrites it only when it filled the buffer. True when the read stopped at the buffer's end short of a newline.
inline bool config_line_maybe_truncated(const char* buf, int size) {
  return buf[size - 1] == 0 && buf[size - 2] != '\n';
}

struct Config {
  int menukey;   // VK that opens the panel; default VK_INSERT (0x2D)
  int debuglog;
  int uiscale;   // percent, UI_SCALE_MIN..UI_SCALE_MAX (ui.h)
  // Panel box in logical pixels at UI scale 100. -1/-1/0/0 is "unset" (0,0 is a real corner, a real size is never <= 0).
  int panelx, panely, panelw, panelh;
  int fovon;
  int fov;       // degrees, 70..140, default 90
  int espenemies; // parent switch, default OFF
  // The four sub-options default ON (they only act while espenemies is on).
  int espboxes;
  int esphealth;
  int espnames;
  int espdistance;
  // Each parent switch below is independent and defaults off.
  int espitems;
  int espsecrets;
  int espexplodables;  // crack sprites (tic-clock wall scan over a seqlock)
  int espbreakables;   // breakable wall panels (same scan, own gate)
  int espswitches;     // switch tiles checkhitswitch recognises, plus a lock/combination hint
  int dmgnumbers;      // tic-clock hook
  int secrettriggers;  // chains from a hidden trigger to the secret sector it opens
  // Sub-switch of espsecrets (trigger.cpp). Renamed from `triggervolume`: config_load aliases the old key, config_save
  // writes only the new one.
  int secretvolume;
  // Sub-switch of espsecrets (secretpath.cpp). A hint: it cannot see a keycard, a locked activator or a one-way drop.
  int espsecretpath;
  // Draw distance in Build units (512/metre), shared by every ESP category, dmgnumbers and secrettriggers. A RANGE, so it
  // defaults to the maximum (ESP_DRAWDIST_MAX, esp_layout.h).
  int espdrawdist;
  int espnolimit;      // 1 turns the cull off, ignoring the slider
  // Aim assist: aimon is the parent switch (default off); the rest do nothing until it is on.
  int aimon;
  int aimhold;    // 1 = only while the hold button is down; default 1
  // Hold binding in the shared VK space (bind.h), default Mouse 5. Replaces `aimbutton`, a raw SDL button number (a stored
  // 5 read as VK would mean Mouse 4), migrated once on load via bind_from_sdl_mouse.
  int aimbind;
  int aimtarget;  // 0 nearest the crosshair, 1 nearest the player (TargetMode)
  int aimpoint;   // 0 mid-body, 1 upper body (AimPoint)
  int aimsmooth;  // default 0 (snap)
  int aimcone;    // degrees, crosshair mode only; default 30
  // Reaches the shot only with Duke's auto-aim OFF: with it on, `shoot` re-derives yaw and zvel from the target (the
  // auto_aim block in aim.h). The marker still shows the lead.
  int aimlead;
  // Marker on the selected target. Follows acquisition, not the aimbot (aim_acquire_should_run), so with aimhold=1 and
  // nothing held it shows what silent aim would shoot.
  int aimdebug;
  int aimleadscale;  // percent of the computed lead; 70, since Duke's burst-and-stop AI makes a full lead overshoot
  int aimrange;      // own range (aim_within_range), independent of espdrawdist; defaults to the maximum
  // `tbot`, not `trig`: secret trigger ESP owns that prefix.
  int tboton;
  int tbotwhen;   // TBOT_WHEN_CROSSHAIR (0) or TBOT_WHEN_AIMBOT (1)
  int tbotdelay;  // milliseconds the condition must hold, 0..500
  int autosavemins;  // 0..30, 0 = off (autosave.h)
  int autosaveslot;  // ini-only (no row): which of the game's ten save slots, 0..9
  // Independent of aimon since acquisition runs for either. Holding auto_aim off for the call stops the re-derive, but
  // the RPG's homing lock reads that result, so a redirected rocket flies the lead-solved angle.
  int silenton;
  int silentmaxangle;  // redirect yaw in degrees (SILENT_MAXANGLE_MIN..MAX); default 45
  // Free camera. freecamon only gates the toggle key; the camera starts inactive every launch (state in camera.cpp).
  int freecamon;
  int freecamkey;          // clamped 1..254. Default 0x56 (V): F6 collided with stock Megaton's Quick_Save
  int freecamteleportkey;  // ray-casts along the look direction, lands the player at the hit. Default 0x54 (T)
  int freecamspeed;        // percent of FREECAM_BASE_SPEED (freecam.h), 1..500
  // `freecamsens` is retired (turn rate derives from mouse aim); config_load still recognises the old key so an old ini
  // loses the line.
  int freecamshowplayer;
  int freecamnocull;    // off by default: the forced sort is O(bunches^2) (nocull.h)
  int freecamnearest;   // seed the renderer from the nearest-walls sector when out of bounds (nearsect.h)
  int freecamcutaway;   // draw by depth so the map reads from outside (dollhouse.h)
  int freecamcutawaysky;  // default ON: off, the sky is black
  int freecamdebug;     // its own switch so reading the numbers needs no log
  // Default ON: a teleporter or water surface carries the camera as it carries the player (freecam.h). A correction, not
  // a feature.
  int freecamtransport;
  int freecamteleportmarker;
  int freecamfog;         // light.h
  int freecamfullbright;
  int freecamshot;        // hides weapon, crosshair, HUD and markers while flying (shot.h)
  int fullbright;         // during play, camera or not
  int mouseaimon;      // modern (raw, equal-axis) mouse aim; default off
  int mouseaimsens;    // yaw degrees per raw count x1000 (MOUSEAIM_SENS_MIN..MAX, mouseaim.h)
  int mouseaimvscale;  // pitch as percent of the yaw rate; default 100
  int mouseaimleadcap;   // ini-only: per-frame yaw lead cap in angle units, 0 = the built-in MOUSEAIM_LEAD_CAP_UNITS
  // Perfect accuracy, each switch decided per shot from `atwith` (accuracy.h), independent even where the engine shares a
  // spread branch (chaingun and shotgun share patch sites, yet "chaingun on, shotgun off" patches only 0x9e8).
  int accpistol;    // atwith 0xa23
  int accchaingun;  // atwith 0x9e8
  int accshotgun;   // atwith 0xa35
  int accexpander;  // atwith 0x990
  int firerate;     // percent of stock (firerate.h); 100 writes no player byte
  int menuanim;     // 0 Off, 1 Simple, 2 Glass (anim.h)
  int switchspeed;  // weapon switch speed, percent of stock
};


// Longest "state: <key> <old> -> <new>" line plus room, so a caller sizes one stack buffer and never truncates.
const int CONFIG_STATE_LINE_CAP = 96;

// Every 0/1 field in Config, in ini-key order, so no exception list can drift. RANGES are excluded so a dragged slider
// does not write a line per pixel.
int config_switch_count();
const char* config_switch_name(int i);
int config_switch_value(const Config& c, int i);

// "state: <key> <old> -> <new>" into buf; empty string for a null key or buffer or non-positive cap.
const char* config_state_line(const char* key, int oldV, int newV, char* buf, int cap);

// Indices of differing table entries, never more than cap.
int config_state_diff(const Config& prev, const Config& cur, int* outIdx, int cap);

// One LOGI per differing switch.
void config_log_state_changes(const Config& prev, const Config& cur);

// The four binding rows (BIND_ROW_*, bind.h) map to four Config members; out-of-range reads 0 and writes nothing.
int  config_bind_get(const Config& c, int row);
void config_bind_set(Config& c, int row, int vk);

// An old ini could hold `aimbutton=1` (Mouse 1), no longer bindable: reset to Mouse 5 rather than change silently.
const char* const CONFIG_LOG_AIMBUTTON1_RESET =
    "aim button Mouse 1 is no longer bindable, reset to Mouse 5";

extern Config g_cfg;
extern bool g_cfgDirty;

void config_load(const char* path);
bool config_save(const char* path);
