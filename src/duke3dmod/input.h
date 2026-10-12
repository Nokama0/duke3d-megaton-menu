#pragma once
#include "ui.h"
#include "common.h"
#include "freecam.h"

extern UiInput g_ui;

// DllMain runs under the loader lock: it installs only the SDL_PollEvent IAT hook; the rest waits for the game thread.
bool input_install_entry();
void input_install_deferred();

void input_mark_init_done();

void mod_deferred_init();

bool input_menu_open();

// Shot/hit pairing clock: a shotgun pull writes up to seven `aim shot:` lines per call, so hits pair by tic.
unsigned input_tic_count();

// SDL button numbering (1 left .. 5 thumb); also config.cpp's clamp for a legacy `aimbutton`.
const int MOUSE_BUTTON_MIN = 1;
const int MOUSE_BUTTON_MAX = 5;

inline bool input_mouse_button_in_range(int sdlButton) {
  return sdlButton >= MOUSE_BUTTON_MIN && sdlButton <= MOUSE_BUTTON_MAX;
}

const int SDL_WINDOWEVENT_TYPE       = 0x200;
const int SDL_WINDOWEVENT_FOCUS_LOST = 13;
const int SDL_WINDOWEVENT_FOCUS_GAINED = 12;

// Alt-tab with a button held sends the button-up elsewhere, so the latch clears on focus loss.
inline bool input_release_buttons_on(int eventType, int windowEvent) {
  return eventType == SDL_WINDOWEVENT_TYPE && windowEvent == SDL_WINDOWEVENT_FOCUS_LOST;
}

bool input_mouse_button_down(int sdlButton);

// One lookup for mouse and keyboard bindings (bind.h); false for unbound 0.
bool input_bind_held(int vk);

void input_begin_frame();

// Live RVAs of the nine player view fields; false (outRvas untouched) if out of range.
bool player_view_field_rvas(unsigned outRvas[VF_COUNT]);

// The free camera flies on keys/motion the player is locked out of, read in hooked_poll's SDL drain. SDL2 scancodes;
// A and C double as Ctrl+A/Ctrl+C in a numeric field (entry.h).
const int SDL_SCANCODE_W      = 26;
const int SDL_SCANCODE_A      = 4;
const int SDL_SCANCODE_S      = 22;
const int SDL_SCANCODE_D      = 7;
const int SDL_SCANCODE_C      = 6;
const int SDL_SCANCODE_SPACE  = 44;
const int SDL_SCANCODE_LCTRL  = 224;
const int SDL_SCANCODE_LSHIFT = 225;

enum FreecamKeySlot {
  FKS_FWD = 0, FKS_BACK, FKS_LEFT, FKS_RIGHT,
  FKS_UP, FKS_DOWN, FKS_FAST, FKS_SLOW,
  FKS_COUNT
};

inline int freecam_key_slot(int scancode) {
  switch (scancode) {
    case SDL_SCANCODE_W:      return FKS_FWD;
    case SDL_SCANCODE_S:      return FKS_BACK;
    case SDL_SCANCODE_A:      return FKS_LEFT;
    case SDL_SCANCODE_D:      return FKS_RIGHT;
    case SDL_SCANCODE_SPACE:  return FKS_UP;
    case SDL_SCANCODE_C:      return FKS_DOWN;
    case SDL_SCANCODE_LSHIFT: return FKS_FAST;
    case SDL_SCANCODE_LCTRL:  return FKS_SLOW;
    default:                  return -1;
  }
}

// The lockout's `loc` clear lives in the getinput hook, the only point early enough to land between fill and accumulate.
enum TicPhase { TIC_GETINPUT = 0, TIC_PROCESSINPUT = 1 };
const int CAMERA_TOGGLE_PHASE = TIC_GETINPUT;

inline bool lockout_engaged_before_loc_read(int phase) { return phase <= TIC_GETINPUT; }

// Mouse-look warps the cursor to an anchor every frame; suppressing it let the cursor drift off-screen, so the warp
// runs and its echo is dropped by absolute position.
inline bool freecam_motion_is_warp_echo(bool haveWarp, int warpX, int warpY,
                                        int evX, int evY) {
  return haveWarp && evX == warpX && evY == warpY;
}

// A cursor at the screen edge stops producing motion, so the camera recentres it itself whenever it strays this far.
const int FREECAM_RECENTRE_PX = 192;
// Measured against the WINDOW CENTRE, not the game's warp target (that test never fires). 0 for a tiny screen.
inline int freecam_screen_centre(int dim) { return dim > 64 ? dim / 2 : 0; }
inline bool freecam_should_recentre(bool haveWarp, int warpX, int warpY,
                                    int evX, int evY, int limit) {
  if (!haveWarp || limit <= 0) return false;
  const int dx = evX - warpX, dy = evY - warpY;
  return (dx > limit || dx < -limit || dy > limit || dy < -limit);
}

void input_note_warp_target(int x, int y);

// Drops accumulated motion and a latched teleport press on the camera's off transition.
void input_freecam_discard();

// Guard, not a sensitivity limit: ~50 full turns per tic; stops signed overflow while pause gates processinput.
const int FREECAM_MOTION_LIMIT = 100000;

inline int freecam_accum(int acc, int delta) {
  const long long sum = (long long)acc + delta;
  if (sum > FREECAM_MOTION_LIMIT) return FREECAM_MOTION_LIMIT;
  if (sum < -FREECAM_MOTION_LIMIT) return -FREECAM_MOTION_LIMIT;
  return (int)sum;
}

// Drains this tic's mouse motion into `out`; once per tic, or a paused drain dumps a pause's motion in one lurch.
void input_freecam_feed(FreecamInput* out);
// Camera SDL motion mix since launch; NEGY/POSY/LASTY are the vertical axis.
enum { FCS_EVENTS = 0, FCS_ECHOES, FCS_NEGX, FCS_POSX, FCS_ECHONEGX, FCS_ECHOPOSX, FCS_WARPS, FCS_LASTX,
       FCS_RECENTRES, FCS_NEGY, FCS_POSY, FCS_LASTY, FCS_COUNT };
void input_freecam_stats(int* out);

// Press edge for g_cfg.freecamkey (via vk_to_sdl_scancode).
bool input_freecam_key_edge();

// hooked_poll tests toggle first, so a colliding teleport is never latched.
inline bool freecam_keys_collide(int toggleScancode, int teleportScancode) {
  return toggleScancode >= 0 && toggleScancode == teleportScancode;
}

// Same for g_cfg.freecamteleportkey, from SDL events (the lockout zeroes `keystatus`); latched only while the camera is up.
bool input_freecam_teleport_edge();

inline bool freecam_edge_discarded_by_pause(bool paused) {
  return paused;
}

// Brings the engine-side lockout into agreement with whether the panel or camera wants it (freeze_wanted, common.h).
void input_lockout_sync();

bool input_lockout_now();

// While the panel is open forces SDL relative-mouse mode off and the cursor visible; closed, restores the values seen at open.
void input_sync_mouse_mode();
