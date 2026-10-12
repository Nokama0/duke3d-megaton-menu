#include "input.h"
#include "hooks.h"
#include "common.h"
#include "config.h"
#include "log.h"
#include "menu.h"
#include "entry.h"
#include "clipboard.h"
#include "bind.h"
#include "paths.h"
#include "engine.h"
#include "aim.h"
#include "target.h"
#include "esp_world.h"
#include "esp.h"
#include "trigger.h"
#include "secretpath.h"
#include "actorstate.h"
#include "firerate.h"
#include "wswitch.h"
#include "mouseaim.h"
#include "tbot.h"
#include "autosave.h"   // per-frame save timer, not a tic step
#include <windows.h>
#include <string.h>
#include <stdio.h>

UiInput g_ui = { 0, 0, false, false, false, false };

typedef int                  (__cdecl *PollEventFn)(void*);
typedef int                  (__cdecl *SetRelativeMouseModeFn)(int);
typedef int                  (__cdecl *GetRelativeMouseModeFn)(void);
typedef int                  (__cdecl *ShowCursorFn)(int);
typedef const unsigned char* (__cdecl *GetKeyboardStateFn)(int*);
typedef unsigned              (__cdecl *GetMouseStateFn)(int*, int*);
typedef void                  (__cdecl *ControlGetInputFn)(void*);
typedef void                  (__cdecl *GetInputFn)(short);
// Takes a short player index (decompile-confirmed); a wrong signature corrupts the stack every tic.
typedef void                  (__cdecl *ProcessInputFn)(short);
typedef void                  (__cdecl *WarpMouseInWindowFn)(void*, int, int);   // SDL_Window* kept opaque

static PollEventFn s_origPoll = nullptr;
static ControlGetInputFn s_origControlGetInput = nullptr;
static GetInputFn s_origGetInput = nullptr;
static ProcessInputFn s_origProcessInput = nullptr;
static WarpMouseInWindowFn s_origWarpMouseInWindow = nullptr;

// PENDING until the first hooked_poll reaches init; claimed by InterlockedCompareExchange (volatile alone is no
// once-only guarantee).
static volatile LONG s_initState = INIT_PENDING;

void input_mark_init_done() { s_initState = INIT_DONE; }

// No timer thread: processinput runs from app_main (VA 0x004ce730) on the thread that pumps SDL events
// (faketimerhandler, VA 0x004016a4, never calls it), so tic and render clocks cross with no race.
static volatile bool s_menuOpen = false;
static bool s_downWas = false;
static bool s_rdownWas = false;
static unsigned s_frameCount = 0;

// Gates every hardcoded-RVA access lacking its own check; logs once. Ungated on purpose: hooked_control_get_input's
// ControlInfo clear (`info` is the caller's pointer).
static bool s_loggedEngineNotVerified = false;
static bool engine_writes_ok() {
  const bool ok = engine_verified();
  if (!ok && !s_loggedEngineNotVerified) {
    s_loggedEngineNotVerified = true;
    LOGE("input: engine fingerprint check failed, refusing every raw hardcoded-RVA "
         "engine-memory read/write (keystatus, mouse deltas, the input gates, the "
         "view-freeze fields); the panel will open but have no effect on the game");
  }
  return ok;
}

// Engine input gates: saved on panel open, restored exactly on close, never written if the panel never opened.
static bool s_gatesSaved = false;
static unsigned char s_savedMouseAcquired = 0;
static unsigned char s_savedMoustat = 0;
static long s_savedControlMouseEnabled = 0;
static long s_savedControlJoystickEnabled = 0;

static bool s_loggedFirstOpenGate = false;
static bool s_loggedFirstCloseGate = false;
static bool s_loggedFrame900 = false;

// View-freeze state. All nine fields freeze together: freezing only horiz/ohoriz/ohorizoff let look_ang/horizoff/
// return_to_center advance and land at once on close. VF_* live in common.h (input.cpp cannot link into test_host).
static const unsigned VIEW_FIELD_OFFSETS[VF_COUNT] = {
  rva::PS_OFFSET_HORIZ, rva::PS_OFFSET_OHORIZ, rva::PS_OFFSET_OHORIZOFF,
  rva::PS_OFFSET_LOOK_ANG, rva::PS_OFFSET_HORIZOFF, rva::PS_OFFSET_RETURN_TO_CENTER,
  rva::PS_OFFSET_ANG, rva::PS_OFFSET_OANG, rva::PS_OFFSET_ANGVEL,
};

static bool s_horizSaved = false;
static long s_savedView[VF_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static bool s_loggedHorizIndexInvalid = false;

static unsigned s_horizFreezeOpenLogCount = 0;
static unsigned s_horizFreezeCloseLogCount = 0;
static const unsigned HORIZ_FREEZE_LOG_LIMIT = 5;

// Diagnostic: per-frame samples after each open. Build interpolates between the previous and current tic value, so
// sampling both halves proves each axis pinned AND its pair equal.
static const int VIEW_SAMPLE_FRAMES = 30;
static long s_horizSamples[VIEW_SAMPLE_FRAMES];
static long s_ohorizSamples[VIEW_SAMPLE_FRAMES];
static long s_angSamples[VIEW_SAMPLE_FRAMES];
static long s_oangSamples[VIEW_SAMPLE_FRAMES];
static int s_viewSampleCount = 0;
static bool s_viewSampleLogged = false;

// Armed by close_menu(), counted down by hooked_processinput: covers a return_to_center countdown armed on the last
// zeroed tic. 2 tics is ~65ms.
static const int SETTLING_WINDOW_TICS = 2;
static volatile int s_settlingTicsRemaining = 0;

// SDL2 event type codes and field offsets. SDL_Event is a union of at least 56 bytes; event.type is the first Uint32.
enum {
  SDL_QUIT_EVENT      = 0x100,
  SDL_KEYDOWN         = 0x300,
  SDL_KEYUP           = 0x301,
  SDL_MOUSEMOTION     = 0x400,
  SDL_MOUSEBUTTONDOWN = 0x401,
  SDL_MOUSEBUTTONUP   = 0x402,
};
static const int OFF_REPEAT = 13, OFF_SCANCODE = 16, OFF_BUTTON = 16, OFF_X = 20, OFF_Y = 24;
// Relative motion (xrel 28, yrel 32) for the free camera; the panel reads the absolute pair.
static const int OFF_XREL = 28, OFF_YREL = 32;
// SDL_WindowEvent's `event` byte: an offset into the caller's live SDL_Event, not a hardcoded address.
static const int OFF_WINDOW_EVENT = 12;

// Toggle/teleport scancodes resolve live via bind.h; resolve_toggle_scancode() only logs install warnings.

bool input_menu_open() { return s_menuOpen; }

// Shot/hit pairing clock, bumped in hooked_processinput before the original call.
static unsigned s_ticCount = 0;

unsigned input_tic_count() { return s_ticCount; }

// The panel and free camera both stop the player's keys and mouse (freeze_wanted); only cursor and mouse-mode
// handling stay panel-only.
static bool input_lockout_active() {
  return freeze_wanted(s_menuOpen, camera_active());
}

bool input_lockout_now() { return input_lockout_active(); }

// One slot per SDL button, recorded before the panel's handling: a closed panel is when the aim hold matters.
static volatile bool s_mouseDown[MOUSE_BUTTON_MAX - MOUSE_BUTTON_MIN + 1] = { false };

bool input_mouse_button_down(int sdlButton) {
  if (!input_mouse_button_in_range(sdlButton)) return false;
  return s_mouseDown[sdlButton - MOUSE_BUTTON_MIN];
}

// Shift/Ctrl for numeric fields, from scancodes. A pair shares one flag (a count could go negative on a release
// predating the DLL); cleared with the panel and field so a missed release cannot stick.
static volatile bool s_shiftHeld = false;
static volatile bool s_ctrlHeld  = false;

static void clear_entry_mods() {
  s_shiftHeld = false;
  s_ctrlHeld  = false;
}

// Free camera accumulator: filled by hooked_poll, drained per tic via input_freecam_feed. Recorded ahead of the panel
// handling so a key released behind it does not read as held.
static volatile int  s_freecamDx = 0;
static volatile int  s_fcStats[FCS_COUNT] = { 0 };
void input_freecam_stats(int* out) { for (int k = 0; k < FCS_COUNT; k++) out[k] = s_fcStats[k]; }
static volatile int  s_freecamDy = 0;
static volatile bool s_freecamKeys[FKS_COUNT] = { false };
// Press edge, latched and consumed by the read: a held toggle flips once, not at the repeat rate.
static volatile bool s_freecamKeyEdge = false;
// Separate teleport edge: a repeating one would bank presses with no camera to spend them on.
static volatile bool s_freecamTeleportEdge = false;
static bool s_loggedFreecamKeyUnmapped = false;
static bool s_loggedFreecamTeleportKeyUnmapped = false;
static bool s_loggedFreecamKeyCollision = false;

void input_freecam_feed(FreecamInput* out) {
  if (!out) return;
  // Motion is consumed (a delta counted once, or it repeats every tic); keys are not (a level, true while held).
  out->dx = s_freecamDx;
  out->dy = s_freecamDy;
  s_freecamDx = 0;
  s_freecamDy = 0;
  out->fwd   = s_freecamKeys[FKS_FWD];
  out->back  = s_freecamKeys[FKS_BACK];
  out->left  = s_freecamKeys[FKS_LEFT];
  out->right = s_freecamKeys[FKS_RIGHT];
  out->up    = s_freecamKeys[FKS_UP];
  out->down  = s_freecamKeys[FKS_DOWN];
  out->fast  = s_freecamKeys[FKS_FAST];
  out->slow  = s_freecamKeys[FKS_SLOW];
}

bool input_freecam_key_edge() {
  const bool edge = s_freecamKeyEdge;
  s_freecamKeyEdge = false;
  return edge;
}

bool input_freecam_teleport_edge() {
  const bool edge = s_freecamTeleportEdge;
  s_freecamTeleportEdge = false;
  return edge;
}

void input_freecam_discard() {
  s_freecamDx = 0;
  s_freecamDy = 0;
  // camera_tic is the only drain: a press left here would fire on the next flight's first tic.
  s_freecamTeleportEdge = false;
}

static volatile bool s_haveWarp = false;
// The window the game's warp passes in, reused for re-centring. Null until the game has warped once.
static void* s_warpWindow = 0;
static volatile int  s_warpX = 0;
static volatile int  s_warpY = 0;

void input_note_warp_target(int x, int y) {
  s_warpX = x;
  s_warpY = y;
  s_haveWarp = true;
}

// One-shot: keystatus at W/A/S/D (0x11/1E/1F/20, Build indexes by DOS scancode) plus the four gates, before/after.
static void log_gate_transition(const char* label,
                                 const unsigned char keyBefore[4], const unsigned char keyAfter[4],
                                 unsigned char maBefore, unsigned char maAfter,
                                 unsigned char msBefore, unsigned char msAfter,
                                 long cmBefore, long cmAfter,
                                 long cjBefore, long cjAfter) {
  LOGI("input: lockout %s; keystatus[W,A,S,D] before=%02X %02X %02X %02X after=%02X %02X %02X %02X; "
       "mouseacquired %u->%u moustat %u->%u CONTROL_MouseEnabled %ld->%ld CONTROL_JoystickEnabled %ld->%ld",
       label,
       keyBefore[0], keyBefore[1], keyBefore[2], keyBefore[3],
       keyAfter[0], keyAfter[1], keyAfter[2], keyAfter[3],
       maBefore, maAfter, msBefore, msAfter, cmBefore, cmAfter, cjBefore, cjAfter);
}

static void write_back_view_fields();
static void clear_engine_input();

// Lockout transition shared by panel and camera: saving gates inside the panel's own open would save the zeros the
// camera set and "restore" them permanently. Only input_lockout_sync() drives these.
static bool s_freezeEngaged = false;

// Saves the four gates, zeroes them, captures the nine view fields, clears keystatus and mouse deltas now.
static void freeze_engage() {
  if (!engine_writes_ok()) {
    // Fingerprint failed: touch no hardcoded RVA; s_gatesSaved/s_horizSaved stay false so release does nothing.
    return;
  }

  unsigned char* mouseacquired = g_base + rva::RVA_MOUSEACQUIRED;
  unsigned char* moustat       = g_base + rva::RVA_MOUSTAT;
  long* ctrlMouse = (long*)(g_base + rva::RVA_CONTROL_MOUSEENABLED);
  long* ctrlJoy   = (long*)(g_base + rva::RVA_CONTROL_JOYSTICKENABLED);
  unsigned char* keystatus = g_base + rva::RVA_KEYSTATUS;

  const bool doLog = !s_loggedFirstOpenGate;
  unsigned char keyBefore[4] = { 0, 0, 0, 0 };
  unsigned char maBefore = 0, msBefore = 0;
  long cmBefore = 0, cjBefore = 0;
  if (doLog) {
    keyBefore[0] = keystatus[0x11]; keyBefore[1] = keystatus[0x1E];
    keyBefore[2] = keystatus[0x1F]; keyBefore[3] = keystatus[0x20];
    maBefore = *mouseacquired; msBefore = *moustat;
    cmBefore = *ctrlMouse;     cjBefore = *ctrlJoy;
  }

  s_savedMouseAcquired          = *mouseacquired;
  s_savedMoustat                = *moustat;
  s_savedControlMouseEnabled    = *ctrlMouse;
  s_savedControlJoystickEnabled = *ctrlJoy;
  s_gatesSaved = true;

  *mouseacquired = 0;
  *moustat       = 0;
  *ctrlMouse     = 0;
  *ctrlJoy       = 0;

  // Nine view fields saved so the tic hooks hold the view; false (invalid myconnectindex) means no freeze this session.
  unsigned fieldRvas[VF_COUNT];
  s_horizSaved = player_view_field_rvas(fieldRvas);
  if (s_horizSaved) {
    for (int i = 0; i < VF_COUNT; i++)
      s_savedView[i] = read_field_bytes(g_base, fieldRvas[i], VIEW_FIELD_WIDTHS[i]);
    // Also logs what collapse_view_values will write (equalised, angvel 0).
    if (s_horizFreezeOpenLogCount < HORIZ_FREEZE_LOG_LIMIT) {
      s_horizFreezeOpenLogCount++;
      const ViewCollapsedValues v = collapse_view_values(
          s_savedView[VF_HORIZ], s_savedView[VF_OHORIZ],
          s_savedView[VF_HORIZOFF], s_savedView[VF_OHORIZOFF],
          s_savedView[VF_LOOK_ANG], s_savedView[VF_RETURN_TO_CENTER],
          s_savedView[VF_ANG], s_savedView[VF_OANG], s_savedView[VF_ANGVEL]);
      LOGI("input: lockout engaged (#%u); captured horiz=%ld ohoriz=%ld ohorizoff=%ld "
           "look_ang=%ld horizoff=%ld return_to_center=%ld ang=%ld oang=%ld angvel=%ld; "
           "writing (collapsed) ohoriz=%ld ohorizoff=%ld oang=%ld angvel=%ld",
           s_horizFreezeOpenLogCount,
           s_savedView[VF_HORIZ], s_savedView[VF_OHORIZ], s_savedView[VF_OHORIZOFF],
           s_savedView[VF_LOOK_ANG], s_savedView[VF_HORIZOFF], s_savedView[VF_RETURN_TO_CENTER],
           s_savedView[VF_ANG], s_savedView[VF_OANG], s_savedView[VF_ANGVEL],
           v.ohoriz, v.ohorizoff, v.oang, v.angvel);
    }
  }

  clear_engine_input();

  if (doLog) {
    s_loggedFirstOpenGate = true;
    const unsigned char keyAfter[4] = { keystatus[0x11], keystatus[0x1E], keystatus[0x1F], keystatus[0x20] };
    log_gate_transition("engaged", keyBefore, keyAfter,
                         maBefore, *mouseacquired, msBefore, *moustat,
                         cmBefore, *ctrlMouse, cjBefore, *ctrlJoy);
  }
}

// Restores the gates, rewrites the view fields once to undo drift, arms the settling window.
static void freeze_release() {
  if (!engine_writes_ok()) return;

  unsigned char* mouseacquired = g_base + rva::RVA_MOUSEACQUIRED;
  unsigned char* moustat       = g_base + rva::RVA_MOUSTAT;
  long* ctrlMouse = (long*)(g_base + rva::RVA_CONTROL_MOUSEENABLED);
  long* ctrlJoy   = (long*)(g_base + rva::RVA_CONTROL_JOYSTICKENABLED);
  unsigned char* keystatus = g_base + rva::RVA_KEYSTATUS;

  const bool doLog = !s_loggedFirstCloseGate;
  unsigned char keyBefore[4] = { 0, 0, 0, 0 };
  unsigned char maBefore = 0, msBefore = 0;
  long cmBefore = 0, cjBefore = 0;
  if (doLog) {
    keyBefore[0] = keystatus[0x11]; keyBefore[1] = keystatus[0x1E];
    keyBefore[2] = keystatus[0x1F]; keyBefore[3] = keystatus[0x20];
    maBefore = *mouseacquired; msBefore = *moustat;
    cmBefore = *ctrlMouse;     cjBefore = *ctrlJoy;
  }

  // Only after freeze_engage() ran.
  if (s_gatesSaved) {
    *mouseacquired = s_savedMouseAcquired;
    *moustat       = s_savedMoustat;
    *ctrlMouse     = s_savedControlMouseEnabled;
    *ctrlJoy       = s_savedControlJoystickEnabled;
  }

  // Undo drift since the owner let go; no-op if the freeze never engaged.
  write_back_view_fields();

  if (s_horizFreezeCloseLogCount < HORIZ_FREEZE_LOG_LIMIT) {
    unsigned closeFieldRvas[VF_COUNT];
    if (player_view_field_rvas(closeFieldRvas)) {
      s_horizFreezeCloseLogCount++;
      const long horizNow = read_field_bytes(g_base, closeFieldRvas[VF_HORIZ], VIEW_FIELD_WIDTHS[VF_HORIZ]);
      LOGI("input: lockout released (#%u); horiz after final write = %ld",
           s_horizFreezeCloseLogCount, horizNow);
    }
  }

  // For SETTLING_WINDOW_TICS calls hooked_processinput forces the frozen fields back, cancelling a return_to_center
  // countdown armed on the last zeroed tic.
  s_settlingTicsRemaining = SETTLING_WINDOW_TICS;

  if (doLog) {
    s_loggedFirstCloseGate = true;
    const unsigned char keyAfter[4] = { keystatus[0x11], keystatus[0x1E], keystatus[0x1F], keystatus[0x20] };
    log_gate_transition("released", keyBefore, keyAfter,
                         maBefore, *mouseacquired, msBefore, *moustat,
                         cmBefore, *ctrlMouse, cjBefore, *ctrlJoy);
  }
}

void input_lockout_sync() {
  const bool wanted = freeze_wanted(s_menuOpen, camera_active());
  switch (freeze_transition(wanted, s_freezeEngaged)) {
    case FT_ENGAGE:  freeze_engage();  s_freezeEngaged = true;  break;
    case FT_RELEASE: freeze_release(); s_freezeEngaged = false; break;
    case FT_NONE:
    default: break;
  }
  // Idempotent; every sync, so an owner handover still clears what the engine refilled.
  clear_engine_input();
}

// Drop latched UI buttons: the matching mouse-up may land on the game. Engine release is input_lockout_sync's.
static void close_menu() {
  s_menuOpen = false;
  g_ui.down = false;
  g_ui.pressed = false;
  g_ui.rdown = false;
  g_ui.rpressed = false;
  // An armed capture would eat the next game key; a close, not a cancel.
  menu_keycap_close();
  // Key routing in hooked_poll sits above `if (!s_menuOpen)`, so an open field would keep eating digits.
  menu_entry_close();
  // A Ctrl held at close may never be seen released.
  clear_entry_mods();
  input_lockout_sync();
  LOGI("%s", MENU_LOG_CLOSE);
}

static int scancode_of(void* ev) { return *(int*)((char*)ev + OFF_SCANCODE); }

// Build's input state is filled by the engine's SDL handler, not SDL_PollEvent, so swallowing events cannot stop a held
// key. While locked this zeroes keystatus and mousex/mousey/mouseb/joyb.
static const int KEYSTATUS_SIZE = 256;

static void clear_engine_input() {
  // Either owner: a free camera the player can still walk and shoot under is not free.
  if (!input_lockout_active()) return;
  if (!engine_writes_ok()) return;

  unsigned char* keystatus = g_base + rva::RVA_KEYSTATUS;
  long* mousex = (long*)(g_base + rva::RVA_MOUSEX);
  long* mousey = (long*)(g_base + rva::RVA_MOUSEY);
  long* mouseb = (long*)(g_base + rva::RVA_MOUSEB);
  long* joyb   = (long*)(g_base + rva::RVA_JOYB);

  memset(keystatus, 0, KEYSTATUS_SIZE);
  *mousex = 0;
  *mousey = 0;
  *mouseb = 0;
  *joyb   = 0;
}

// With `loc` zeroed every tic return-to-centre would level horiz and discard the user's aim. myconnectindex indexes
// live memory, so it is validated; MAX_PLAYER_INDEX is 15 (PDB: `ps player_struct[16]`).

bool player_view_field_rvas(unsigned outRvas[VF_COUNT]) {
  const long myIdx = *(long*)(g_base + rva::RVA_MYCONNECTINDEX);
  if (!player_index_valid(myIdx, MAX_PLAYER_INDEX)) {
    if (!s_loggedHorizIndexInvalid) {
      s_loggedHorizIndexInvalid = true;
      LOGE("input: myconnectindex=%ld out of range, cannot freeze the view", myIdx);
    }
    return false;
  }
  for (int i = 0; i < VF_COUNT; i++)
    outRvas[i] = player_field_rva(rva::RVA_PS, myIdx, rva::PS_STRIDE, VIEW_FIELD_OFFSETS[i]);
  return true;
}

// Writes collapse_view_values output, not s_savedView verbatim: ohoriz/ohorizoff/oang collapse onto horiz/horizoff/
// ang and angvel is zeroed, else the view wobbled mid mouse-look. Tic-clock only.
static void write_back_view_fields() {
  if (!s_horizSaved) return;
  if (!engine_writes_ok()) return;
  unsigned fieldRvas[VF_COUNT];
  if (!player_view_field_rvas(fieldRvas)) return;

  const ViewCollapsedValues v = collapse_view_values(
      s_savedView[VF_HORIZ], s_savedView[VF_OHORIZ],
      s_savedView[VF_HORIZOFF], s_savedView[VF_OHORIZOFF],
      s_savedView[VF_LOOK_ANG], s_savedView[VF_RETURN_TO_CENTER],
      s_savedView[VF_ANG], s_savedView[VF_OANG], s_savedView[VF_ANGVEL]);

  write_field_bytes(g_base, fieldRvas[VF_HORIZ],            VIEW_FIELD_WIDTHS[VF_HORIZ],            v.horiz);
  write_field_bytes(g_base, fieldRvas[VF_OHORIZ],            VIEW_FIELD_WIDTHS[VF_OHORIZ],           v.ohoriz);
  write_field_bytes(g_base, fieldRvas[VF_HORIZOFF],          VIEW_FIELD_WIDTHS[VF_HORIZOFF],         v.horizoff);
  write_field_bytes(g_base, fieldRvas[VF_OHORIZOFF],         VIEW_FIELD_WIDTHS[VF_OHORIZOFF],        v.ohorizoff);
  write_field_bytes(g_base, fieldRvas[VF_LOOK_ANG],          VIEW_FIELD_WIDTHS[VF_LOOK_ANG],         v.lookAng);
  write_field_bytes(g_base, fieldRvas[VF_RETURN_TO_CENTER],  VIEW_FIELD_WIDTHS[VF_RETURN_TO_CENTER], v.returnToCenter);
  write_field_bytes(g_base, fieldRvas[VF_ANG],               VIEW_FIELD_WIDTHS[VF_ANG],              v.ang);
  write_field_bytes(g_base, fieldRvas[VF_OANG],               VIEW_FIELD_WIDTHS[VF_OANG],            v.oang);
  write_field_bytes(g_base, fieldRvas[VF_ANGVEL],             VIEW_FIELD_WIDTHS[VF_ANGVEL],          v.angvel);
}

// Mirror of close_menu(). The game's cfg is read once per open for the "Also bound in the game" note; any failure ends
// in menu_gamecfg_load(nullptr) so no row warns stale.
static void load_game_cfg() {
  char exe[MAX_PATH];
  const DWORD len = GetModuleFileNameA(NULL, exe, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) { menu_gamecfg_load(nullptr); return; }
  char* slash = strrchr(exe, '\\');
  if (slash) *(slash + 1) = 0;
  else exe[0] = 0;
  char cfg[MAX_PATH];
  // A truncated path names some other file.
  if (!build_gamecfg_path(exe, cfg, sizeof(cfg))) { menu_gamecfg_load(nullptr); return; }
  menu_gamecfg_load(cfg);
}

static void open_menu() {
  s_menuOpen = true;
  // Once per open: the player can edit the game's bindings between opens.
  load_game_cfg();
  // The panel's own instrument (proves the view is pinned while up), so it lives here, not in freeze_engage().
  s_viewSampleCount = 0;
  s_viewSampleLogged = false;
  input_lockout_sync();
  LOGI("%s", MENU_LOG_OPEN);
}

static int __cdecl hooked_poll(void* ev) {
  // Where the DLL starts. RUNNING is set before the body so polls its own installs trigger cannot re-enter;
  // init_should_run keeps the locked instruction to once per process.
  if (init_should_run((int)s_initState)
      && InterlockedCompareExchange(&s_initState, INIT_RUNNING, INIT_PENDING) == INIT_PENDING) {
    mod_deferred_init();
    s_initState = init_next_state(INIT_RUNNING);
  }
  for (;;) {
    const int got = s_origPoll(ev);
    if (!got) {
      // Clearing at drain-complete means the game's logic reads zeros this frame, not one late.
      clear_engine_input();
      // Autosave's per-frame step: dnSaveGame runs on this thread between frames, never in a tic; refuses while the
      // panel is open.
      autosave_frame();
      return 0;
    }
    const unsigned type = *(unsigned*)ev;

    if (type == SDL_QUIT_EVENT) return got;

    // Recorded before any `continue` below: a swallowed KEYUP would stick the modifier.
    if (type == SDL_KEYDOWN || type == SDL_KEYUP) {
      const int msc = scancode_of(ev);
      if (msc == SDL_SCANCODE_LSHIFT || msc == SDL_SCANCODE_RSHIFT)
        s_shiftHeld = (type == SDL_KEYDOWN);
      else if (msc == SDL_SCANCODE_LCTRL || msc == SDL_SCANCODE_RCTRL)
        s_ctrlHeld = (type == SDL_KEYDOWN);
    }

    if (type == SDL_KEYDOWN) {
      const bool repeat = *(unsigned char*)((char*)ev + OFF_REPEAT) != 0;
      const int sc = scancode_of(ev);
      // Ctrl+Insert: camera debug readout to the clipboard (debuglog on, camera flying).
      if (!repeat && s_ctrlHeld && !s_shiftHeld && sc == SDL_SCANCODE_INSERT
          && g_cfg.freecamdebug != 0 && camera_active() && !menu_entry_open()) {
        const char* text = esp_debug_readout_text();
        if (text && text[0]) {
          const bool ok = clipboard_write_text(text);
          LOGI("input: readout %s the clipboard (%d bytes)", ok ? "copied to" : "NOT copied to", (int)strlen(text));
        }
      }
      // Shift+Insert: move the camera to a copied readout's pose.
      if (!repeat && s_shiftHeld && !s_ctrlHeld && sc == SDL_SCANCODE_INSERT
          && g_cfg.freecamdebug != 0 && camera_active() && !menu_entry_open()) {
        char pasted[1200];
        FreecamPose pose;
        if (clipboard_read_text(pasted, (int)sizeof pasted) && freecam_parse_readout(pasted, &pose)) {
          camera_request_pose(pose);
        } else {
          LOGI("input: clipboard holds no \"cam x= y= z= ang= pitch=\" line; camera left alone");
        }
      }
      // An open numeric field owns these keys, tested first (typing "90" into FOV must not reach the weapon slots;
      // Escape closes the field, not the panel). Typing and `moving` repeat; Enter/Escape and `chording` do not.
      if (menu_entry_open()) {
        const bool typing = (sc == SDL_SCANCODE_BACKSPACE || entry_char_of_scancode(sc) != 0);
        const bool moving = (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT
                             || sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_END
                             || sc == SDL_SCANCODE_DELETE);
        const bool oneShot = (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN
                              || sc == SDL_SCANCODE_KP_ENTER);
        const bool chording = s_ctrlHeld
                              && (sc == SDL_SCANCODE_A || sc == SDL_SCANCODE_C
                                  || sc == SDL_SCANCODE_V || sc == SDL_SCANCODE_X);
        if (typing || moving || oneShot || chording) {
          if (typing || moving || ui_key_is_initial_press(repeat)) {
            if (chording && sc == SDL_SCANCODE_V) {
              char pasted[ENTRY_PASTE_CAP];
              if (clipboard_read_text(pasted, (int)sizeof pasted)) menu_entry_paste(pasted);
            } else {
              const char* clip = menu_entry_key_mod(sc, s_shiftHeld, s_ctrlHeld);
              if (clip) clipboard_write_text(clip);
            }
            // Enter/Escape/click-away can close the field inside menu_entry_key_mod; a held modifier may never be
            // seen released.
            if (!menu_entry_open()) clear_entry_mods();
          }
          continue;
        }
      }
      // One-shots fire on the initial press only. A repeat falls through: hidden while open, passed on when closed.
      if (ui_key_is_initial_press(repeat)) {
        // An armed capture owns the next key, before the toggle: binding the camera to the panel's key would close the
        // panel. A key with no VK falls through unbound rather than silently unbinding the row.
        if (menu_keycap_armed()) {
          if (sc == SDL_SCANCODE_ESCAPE) { menu_keycap_cancel(); continue; }
          const int vk = bind_from_sdl_scancode(sc);
          if (vk) { menu_keycap_bind(vk); continue; }
        }
        // Toggle never reaches the game; live config, so a rebind needs no restart. An unmappable menukey falls back to
        // Insert so the panel is never unreachable.
        if (!menu_entry_open()
            && bind_matches_key(bind_menu_key_effective(g_cfg.menukey), sc)) {
          if (s_menuOpen) close_menu(); else open_menu();
          continue;
        }
        // Escape closes the panel instead of opening the game menu behind it.
        if (s_menuOpen && sc == SDL_SCANCODE_ESCAPE) { close_menu(); continue; }
        // Camera toggle: latched, spent on the tic clock (CAMERA_TOGGLE_PHASE) so the lockout engages before that tic's
        // `loc` is read. Also while the camera is up, so turning the feature off mid-flight cannot strand it.
        if (!menu_entry_open()
            && (g_cfg.freecamon != 0 || camera_active())
            && bind_matches_key(g_cfg.freecamkey, sc)) {
          s_freecamKeyEdge = true;
          continue;
        }
        // Teleport: only while the camera is up (tying it to `freecamon` would take a letter key all session) and not
        // under an open field, since it moves the player.
        if (!menu_entry_open()
            && camera_active() && bind_matches_key(g_cfg.freecamteleportkey, sc)) {
          s_freecamTeleportEdge = true;
          continue;
        }
      }
    }

    // Button 1 is excluded (also in keycap_step): it arms the row, so capturing it would bind the row to its opening
    // click.
    if (type == SDL_MOUSEBUTTONDOWN) {
      const int btn = *(unsigned char*)((char*)ev + OFF_BUTTON);
      if (btn != 1) {
        if (menu_keycap_armed()) {
          const int vk = bind_from_sdl_mouse(btn);
          if (vk) { menu_keycap_bind(vk); continue; }
        }
        // All three key-driven bindings answer to a button as to a key (a panel key on Mouse 4 must not leave the panel
        // unreachable).
        if (!menu_entry_open()
            && bind_matches_mouse(bind_menu_key_effective(g_cfg.menukey), btn)) {
          if (s_menuOpen) close_menu(); else open_menu();
          continue;
        }
        if (!menu_entry_open()
            && (g_cfg.freecamon != 0 || camera_active())
            && bind_matches_mouse(g_cfg.freecamkey, btn)) {
          s_freecamKeyEdge = true;
          continue;
        }
        if (!menu_entry_open()
            && camera_active() && bind_matches_mouse(g_cfg.freecamteleportkey, btn)) {
          s_freecamTeleportEdge = true;
          continue;
        }
      }
    }

    // Buttons 1..5 for the aim hold, recorded before the early return. Out-of-range is dropped, not clamped.
    if (type == SDL_MOUSEBUTTONDOWN || type == SDL_MOUSEBUTTONUP) {
      const int btn = *(unsigned char*)((char*)ev + OFF_BUTTON);
      if (input_mouse_button_in_range(btn))
        s_mouseDown[btn - MOUSE_BUTTON_MIN] = (type == SDL_MOUSEBUTTONDOWN);
    }

    // Camera keys are levels, so repeats are not filtered.
    if (type == SDL_KEYDOWN || type == SDL_KEYUP) {
      const int slot = freecam_key_slot(scancode_of(ev));
      if (slot >= 0) s_freecamKeys[slot] = (type == SDL_KEYDOWN);
    }

    // Camera motion uses xrel/yrel (absolute coordinates stop at the window edge), summed and saturating, only while
    // the camera is up: only camera_tic clears the total, so otherwise it would bank the session's movement.
    if (type == SDL_MOUSEMOTION && camera_active()) {
      // Drop the warp's own echo (compares absolute position). Counts feed the camera: line.
      s_fcStats[FCS_EVENTS]++;
      s_fcStats[FCS_LASTX] = *(int*)((char*)ev + OFF_X);
      s_fcStats[FCS_LASTY] = *(int*)((char*)ev + OFF_Y);
      if (!freecam_motion_is_warp_echo(s_haveWarp, s_warpX, s_warpY,
                                       *(int*)((char*)ev + OFF_X),
                                       *(int*)((char*)ev + OFF_Y))) {
        const int xr = *(int*)((char*)ev + OFF_XREL);
        if (xr < 0) s_fcStats[FCS_NEGX] += -xr; else s_fcStats[FCS_POSX] += xr;
        const int yr = *(int*)((char*)ev + OFF_YREL);
        if (yr < 0) s_fcStats[FCS_NEGY] += -yr; else s_fcStats[FCS_POSY] += yr;
        s_freecamDx = freecam_accum(s_freecamDx, xr);
        s_freecamDy = freecam_accum(s_freecamDy, yr);
      } else {
        s_fcStats[FCS_ECHOES]++;
        const int xr = *(int*)((char*)ev + OFF_XREL);
        if (xr < 0) s_fcStats[FCS_ECHONEGX] += -xr; else s_fcStats[FCS_ECHOPOSX] += xr;
      }
      // Re-centre from the engine's screen size, not the game's warp target (it follows the cursor, never fires, and the
      // cursor walks to an edge). Anchor recorded first so the echo filter drops it.
      if (s_warpWindow && s_origWarpMouseInWindow && engine_verified()) {
        const int cx = freecam_screen_centre(*(const int*)(g_base + rva::XDIM));
        const int cy = freecam_screen_centre(*(const int*)(g_base + rva::YDIM));
        if (cx > 0 && cy > 0
            && freecam_should_recentre(true, cx, cy,
                                       *(int*)((char*)ev + OFF_X), *(int*)((char*)ev + OFF_Y),
                                       FREECAM_RECENTRE_PX)) {
          s_fcStats[FCS_RECENTRES]++;
          input_note_warp_target(cx, cy);
          s_origWarpMouseInWindow(s_warpWindow, cx, cy);
        }
      }
    }

    // Release on focus loss: s_mouseDown is a latch and the button-up goes to the focused window.
    if (type == SDL_WINDOWEVENT_TYPE && camera_active()) {
      const int we = *(unsigned char*)((char*)ev + OFF_WINDOW_EVENT);
      if (we == SDL_WINDOWEVENT_FOCUS_LOST || we == SDL_WINDOWEVENT_FOCUS_GAINED)
        LOGI("input: window focus %s while the camera flies; cursor last at (%d,%d), warp anchor (%d,%d)",
             we == SDL_WINDOWEVENT_FOCUS_LOST ? "lost" : "gained",
             s_fcStats[FCS_LASTX], s_fcStats[FCS_LASTY], s_warpX, s_warpY);
    }

    if (input_release_buttons_on(type, *(unsigned char*)((char*)ev + OFF_WINDOW_EVENT))) {
      for (int b = 0; b < MOUSE_BUTTON_MAX - MOUSE_BUTTON_MIN + 1; b++) s_mouseDown[b] = false;
      // Camera keys are latches too; motion across a focus change was nobody's movement.
      for (int k = 0; k < FKS_COUNT; k++) s_freecamKeys[k] = false;
      s_freecamDx = 0;
      s_freecamDy = 0;
      // A latched teleport press is a one-shot that moves the player.
      s_freecamTeleportEdge = false;
    }

    if (!s_menuOpen) return got;

    // Panel owns input while open: record what it needs, swallow the rest.
    switch (type) {
      case SDL_MOUSEMOTION:
        // Absolute x/y (offsets 20/24), never xrel/yrel: hooked_warp_mouse_in_window suppresses warps while open, so no
        // warp event snaps the cursor back.
        g_ui.mx = *(int*)((char*)ev + OFF_X);
        g_ui.my = *(int*)((char*)ev + OFF_Y);
        continue;
      case SDL_MOUSEBUTTONDOWN:
        if (*(unsigned char*)((char*)ev + OFF_BUTTON) == 1) {   // left button
          g_ui.mx = *(int*)((char*)ev + OFF_X);
          g_ui.my = *(int*)((char*)ev + OFF_Y);
          g_ui.down = true;
        } else if (*(unsigned char*)((char*)ev + OFF_BUTTON) == 3) {   // right button
          // Right-click with no prior motion event would test a stale position.
          g_ui.mx = *(int*)((char*)ev + OFF_X);
          g_ui.my = *(int*)((char*)ev + OFF_Y);
          g_ui.rdown = true;
        }
        continue;
      case SDL_MOUSEBUTTONUP:
        if (*(unsigned char*)((char*)ev + OFF_BUTTON) == 1) g_ui.down = false;
        else if (*(unsigned char*)((char*)ev + OFF_BUTTON) == 3) g_ui.rdown = false;
        continue;
      default:
        continue;
    }
  }
}

// Logging only: hooked_poll reads the live config. Stale by design (the panel can create the toggle/teleport
// collision later).
static void resolve_toggle_scancode() {
  if (bind_menu_key_effective(g_cfg.menukey) != g_cfg.menukey) {
    LOGE("input: menukey 0x%02X has no known SDL scancode mapping, falling back to Insert",
         g_cfg.menukey);
  }

  // No fallback for the camera key: an off-by-default feature may stay unbound rather than get a guessed key.
  int fsc = -1, tsc = -1;
  if (!bind_is_mouse(g_cfg.freecamkey) && !vk_to_sdl_scancode(g_cfg.freecamkey, &fsc)) {
    fsc = -1;
    if (!s_loggedFreecamKeyUnmapped) {
      s_loggedFreecamKeyUnmapped = true;
      LOGE("input: freecamkey 0x%02X has no known SDL scancode mapping, "
           "the free camera has no toggle key this session", g_cfg.freecamkey);
    }
  }

  if (!bind_is_mouse(g_cfg.freecamteleportkey)
      && !vk_to_sdl_scancode(g_cfg.freecamteleportkey, &tsc)) {
    tsc = -1;
    if (!s_loggedFreecamTeleportKeyUnmapped) {
      s_loggedFreecamTeleportKeyUnmapped = true;
      LOGE("input: freecamteleportkey 0x%02X has no known SDL scancode mapping, "
           "the free camera has no teleport key this session", g_cfg.freecamteleportkey);
    }
  }

  // Toggle wins on a shared key (tested first). Mouse-button keys have both scancodes -1, hence the second term.
  const bool collide = freecam_keys_collide(fsc, tsc)
      || (bind_is_mouse(g_cfg.freecamkey) && g_cfg.freecamkey == g_cfg.freecamteleportkey);
  if (collide && !s_loggedFreecamKeyCollision) {
    s_loggedFreecamKeyCollision = true;
    LOGE("input: freecamkey and freecamteleportkey both resolve to the same key "
         "(0x%02X); the toggle wins and the teleport will never fire until the two "
         "are set to different keys", g_cfg.freecamkey);
  }
}

// The game reads movement/fire from SDL_GetKeyboardState, which SDL updates while pumping, so swallowing events cannot
// stop them. Callers may cache the pointer, so this returns one owned buffer, synced every frame or zeroed while the
// DLL owns input.
static GetKeyboardStateFn s_origGetKeyboardState = nullptr;

// SDL_NUM_SCANCODES has been 512 across SDL2; clamped rather than trusted.
static const int MAX_KEYS = 512;
static unsigned char s_keyState[MAX_KEYS];
static int s_keyStateCount = 0;
static unsigned s_getKeyboardStateCalls = 0;
static bool s_loggedKeyboardDiag = false;
static bool s_loggedKeyCountClamp = false;

static void refresh_key_state() {
  int n = 0;
  const unsigned char* real = s_origGetKeyboardState ? s_origGetKeyboardState(&n) : nullptr;
  if (n > MAX_KEYS) {
    if (!s_loggedKeyCountClamp) {
      s_loggedKeyCountClamp = true;
      LOGE("input: SDL_GetKeyboardState numkeys=%d exceeds our %d-key buffer, clamping", n, MAX_KEYS);
    }
    n = MAX_KEYS;
  }
  s_keyStateCount = n;
  if (input_lockout_active() || !real) {
    // DLL owns input, or nothing real yet: the game must see no key held.
    memset(s_keyState, 0, sizeof(s_keyState));
  } else {
    memcpy(s_keyState, real, (size_t)n);
    if (n < MAX_KEYS) memset(s_keyState + n, 0, (size_t)(MAX_KEYS - n));
  }
}

// One lookup for both halves of the binding space; bounds-checked so a scancode past what SDL reported reads false.
bool input_bind_held(int vk) {
  if (bind_is_mouse(vk)) return input_mouse_button_down(bind_mouse_sdl_button(vk));
  int sc = -1;
  if (!vk_to_sdl_scancode(vk, &sc)) return false;
  if (sc < 0 || sc >= s_keyStateCount || sc >= MAX_KEYS) return false;
  return s_keyState[sc] != 0;
}

// Defence in depth: measured at only 5 calls in 300 frames with the panel open; movement comes from getinput/loc.
static const unsigned char* __cdecl hooked_get_keyboard_state(int* numkeys) {
  s_getKeyboardStateCalls++;
  // Refresh here too so a caller that re-fetches every frame sees fresh data.
  refresh_key_state();
  if (numkeys) *numkeys = s_keyStateCount;
  return s_keyState;
}

// Returns the mask by value, so no cached pointer. While the panel owns input it hides the buttons but still forwards
// x/y through the real call so no out-param is garbage.
static GetMouseStateFn s_origGetMouseState = nullptr;

// Defence in depth: getinput/loc is the proven mouse-look source.
static unsigned __cdecl hooked_get_mouse_state(int* x, int* y) {
  const unsigned real = s_origGetMouseState ? s_origGetMouseState(x, y) : 0;
  return input_lockout_active() ? 0u : real;
}

// The game warps the cursor each frame for mouse-look; with the panel open each warp event snapped g_ui.mx/my back.
// No-op the warp while the panel owns input.
static void __cdecl hooked_warp_mouse_in_window(void* window, int x, int y) {
  // Panel only: suppressing it for the free camera zeroed the camera's accumulator (a warp posts a motion event
  // cancelling the motion that provoked it), so yaw stopped after ~1720 counts. The camera filters the echo instead.
  if (s_menuOpen) return;
  if (camera_active()) s_fcStats[FCS_WARPS]++;
  s_warpWindow = window;
  input_note_warp_target(x, y);
  if (s_origWarpMouseInWindow) s_origWarpMouseInWindow(window, x, y);
}

// Defence in depth, not the movement fix: this 0x18-byte ControlInfo (JMP thunk, input_install_deferred) was all-zero
// with the panel open while the player kept moving.
static const int CONTROLINFO_SIZE = 0x18;
static unsigned s_controlGetInputOpenCalls = 0;

static void log_controlinfo_before_clear(unsigned callIndex, const unsigned char* bytes) {
  // Truncation-safe as in paths.h: a malformed log line must never crash the game.
  char hex[CONTROLINFO_SIZE * 3];
  char* p = hex;
  size_t remaining = sizeof(hex);
  for (int i = 0; i < CONTROLINFO_SIZE && remaining > 0; i++) {
    const int n = _snprintf_s(p, remaining, _TRUNCATE, i == 0 ? "%02X" : " %02X", bytes[i]);
    if (n < 0) break;
    p += n;
    remaining -= (size_t)n;
  }
  LOGI("input: CONTROL_GetInput call #%u while panel open, ControlInfo before clear = %s", callIndex, hex);
}

static void __cdecl hooked_control_get_input(void* info) {
  // Always call the original: the engine must drain the accumulators or a stale delta snaps the view on close.
  if (s_origControlGetInput) s_origControlGetInput(info);
  if (!info) return;
  if (!input_lockout_active()) return;

  // One-shot: first ten calls, logged before the clear.
  if (s_controlGetInputOpenCalls < 10)
    log_controlinfo_before_clear(s_controlGetInputOpenCalls + 1, (const unsigned char*)info);
  s_controlGetInputOpenCalls++;

  memset(info, 0, CONTROLINFO_SIZE);
}

// The actual choke point (CONTROL_GetInput was falsified): faketimerhandler calls getinput to fill `loc`, then
// accumulates it, so zeroing `loc` after the real call lands between. Reached via THUNK_GETINPUT_RVA's JMP thunk.
static const int LOC_SIZE = 12;
static volatile unsigned s_getInputOpenCalls = 0;

// Logged once at the camera's first activation tic: evidence the toggle was spent before `loc` is read; non-zero bytes
// are the step/shot that used to leak.
static bool s_loggedFreecamEngageLoc = false;

static void log_loc_before_clear(const char* why, unsigned callIndex, const unsigned char* bytes) {
  char hex[LOC_SIZE * 3];
  char* p = hex;
  size_t remaining = sizeof(hex);
  for (int i = 0; i < LOC_SIZE && remaining > 0; i++) {
    const int n = _snprintf_s(p, remaining, _TRUNCATE, i == 0 ? "%02X" : " %02X", bytes[i]);
    if (n < 0) break;
    p += n;
    remaining -= (size_t)n;
  }
  LOGI("input: getinput call #%u while %s, loc before clear = %s", callIndex, why, hex);
}

static void __cdecl hooked_getinput(short player) {
  // Always call the original, as in hooked_control_get_input.
  if (s_origGetInput) s_origGetInput(player);

  // Mouse aim's tic step: `loc` is filled and not yet accumulated, so raw counts compose with the keyboard/gamepad turn.
  // `player` lets it refuse non-local calls.
  mouseaim_tic_apply(player);

  // Hold view: the aimbot writes ang/horiz at the previous tic's tail and the engine adds the mouse turn at this tic's
  // top, so a pull lands between solve and shot. `loc`'s turn bytes are the only early-enough point.
  if ((int)player == engine_myconnectindex()) {
    const bool holdView = aim_hold_view_rule(aim_view_held(), input_menu_open(),
                                             input_lockout_now());
    aim_view_hold_note(holdView);
    if (holdView) engine_input_clear_turn();
  }

  // Triggerbot tic: same record and moment as mouse aim, before the lockout clears `loc`.
  tbot_tic(player);

  // Deferred camera release, a no-op normally: covers processinput never running (paused), which would strand the
  // lockout. Below the original because its nine writes may not precede the engine's update.
  camera_release_tic();

  // Camera toggle sits between the engine filling `loc` and the clear: faketimerhandler accumulates `loc` on return, so
  // a later lockout leaked a shot per activation. Edges are discarded while paused.
  if (freecam_edge_discarded_by_pause(engine_paused())) {
    (void)input_freecam_key_edge();
    (void)input_freecam_teleport_edge();
  } else if (CAMERA_TOGGLE_PHASE == TIC_GETINPUT) {
    camera_toggle_tic((int)player);
  }

  if (!input_lockout_active()) return;

  unsigned char* loc = g_base + rva::RVA_LOC;

  if (s_getInputOpenCalls < 10)
    log_loc_before_clear("this DLL owns input", s_getInputOpenCalls + 1, loc);
  s_getInputOpenCalls++;

  // Once, at the free camera's first engaged tic (ten panel opens could exhaust the bound above).
  if (camera_active() && !s_loggedFreecamEngageLoc) {
    s_loggedFreecamEngageLoc = true;
    log_loc_before_clear("the free camera engaged THIS tic", 0, loc);
  }

  // Raw write gated; the original above is unconditional so the engine's accumulators keep draining.
  if (engine_writes_ok()) memset(loc, 0, LOC_SIZE);

  // Pin the view fields here too: writing only from processinput left a same-tic window of live fields.
  write_back_view_fields();
}

// Tic-clock writer of the nine view fields: input_begin_frame's restore ran on the render clock and processinput on the
// tic clock, which was the recentre-on-close bug.
static const int PS_HORIZ_WIDTH = 4;   // VIEW_FIELD_WIDTHS[VF_HORIZ]
static const int PS_RTC_WIDTH   = 1;   // VIEW_FIELD_WIDTHS[VF_RETURN_TO_CENTER]
static volatile unsigned s_processInputOpenCalls = 0;

static void __cdecl hooked_processinput(short player) {
  // Shot/hit pairing clock, bumped before the original (load-bearing): the weapon fires inside that call (hooked_shoot
  // reads it) and the hit arrives after it in the same tic.
  if ((int)player == engine_myconnectindex()) s_ticCount++;

  // State-cache generation, turned at the head: silent aim's re-solve asks from inside the original, and a tail turn
  // would hand it the previous tic's state. Local player only.
  if ((int)player == engine_myconnectindex())
    actor_state_generation_begin(input_tic_count());

  // Camera toggle before the freeze decision: the camera engages the freeze. Dead while CAMERA_TOGGLE_PHASE is
  // TIC_GETINPUT (one step late for `loc`); kept so moving the phase is a one-line change.
  if (CAMERA_TOGGLE_PHASE == TIC_PROCESSINPUT) camera_toggle_tic((int)player);

  // The settling window forces all nine fields like the open state: restoring only after the real call is one tic late
  // (measured: return_to_center 9->0 with horiz already moved), and a free field lost a horiz unit per cycle.
  const ViewFreezeAction action =
      view_freeze_action(s_menuOpen, camera_active(), s_settlingTicsRemaining);
  const bool freezeActive = (action != VFA_NONE);

  // Logging resolved before the real call; bounded to ten open calls and SETTLING_WINDOW_TICS settling calls.
  unsigned fieldRvas[VF_COUNT];
  bool haveRvas = false;
  const bool doOpenLog     = (action == VFA_FULL) && (s_processInputOpenCalls < 10);
  const bool doSettlingLog = (action == VFA_SETTLING);
  long horizBefore = 0, rtcBefore = 0;
  if ((doOpenLog || doSettlingLog) && engine_writes_ok()) {
    haveRvas = player_view_field_rvas(fieldRvas);
    if (haveRvas) {
      horizBefore = read_field_bytes(g_base, fieldRvas[VF_HORIZ], PS_HORIZ_WIDTH);
      rtcBefore   = read_field_bytes(g_base, fieldRvas[VF_RETURN_TO_CENTER], PS_RTC_WIDTH);
    }
  }

  if (freezeActive) write_back_view_fields();

  // Always call the original: the full per-tic player update must run.
  if (s_origProcessInput) s_origProcessInput(player);

  // Read after the original; hoisted because the log blocks and acquisition both read it.
  const PlayerAim pa = engine_player_aim();
  const bool localPlayer = pa.index >= 0 && (int)player == pa.index;

  if (doOpenLog && haveRvas) {
    const long horizAfter = read_field_bytes(g_base, fieldRvas[VF_HORIZ], PS_HORIZ_WIDTH);
    const long rtcAfter   = read_field_bytes(g_base, fieldRvas[VF_RETURN_TO_CENTER], PS_RTC_WIDTH);
    LOGI("input: processinput call #%u while panel open, "
         "horiz %ld->%ld return_to_center %ld->%ld",
         s_processInputOpenCalls + 1, horizBefore, horizAfter, rtcBefore, rtcAfter);
  }
  if (doOpenLog) s_processInputOpenCalls++;

  if (doSettlingLog && haveRvas) {
    const long horizAfter = read_field_bytes(g_base, fieldRvas[VF_HORIZ], PS_HORIZ_WIDTH);
    const long rtcAfter   = read_field_bytes(g_base, fieldRvas[VF_RETURN_TO_CENTER], PS_RTC_WIDTH);
    LOGI("input: processinput settling tic, %d tic(s) remaining before this call, "
         "horiz %ld->%ld return_to_center %ld->%ld",
         s_settlingTicsRemaining, horizBefore, horizAfter, rtcBefore, rtcAfter);
  }

  s_settlingTicsRemaining = settling_tics_after_call(s_menuOpen, s_settlingTicsRemaining);

  if (freezeActive) write_back_view_fields();

  // Acquisition last: earlier would use last tic's positions; the freeze path clears rather than skips. The shot fires
  // inside the original, so it lags a moving target by ~33ms.
  if (freezeActive) {
    aim_clear();
  } else if (localPlayer) {
    aim_tic(pa);
    aim_apply(pa);
  }

  // Camera integration, last: camera_tic drains the input feed even when it does not step.
  if (localPlayer && camera_active()) camera_tic();

  // Fire rate, the one player-state write here: after aim_apply, before camera_release_tic (whose write-back stays
  // last). localPlayer-gated, or a refusing multiplayer iteration would reset s_carry.
  if (localPlayer) firerate_tic(freezeActive);

  // Weapon switch speed: same tail, order and gate as firerate_tic.
  if (localPlayer) wswitch_tic(freezeActive);

  // Deferred release, last on purpose: switching off ends in freeze_release's nine writes, which may not land ahead of
  // the original. Holding the lockout also keeps flying keys from the game on the exit tic.
  camera_release_tic();

  // Wall scan, localPlayer-gated (this hook runs per connected player). Reads map memory only.
  if (localPlayer) esp_world_tic();

  // Secret-exit scan: rescans at most once a level (a numwalls change).
  if (localPlayer) esp_secret_exit_tic();

  // Actor-state scan, after esp_world_tic and never interleaved: engine.cpp's per-walk cycle counter is single-slot, so
  // interleaved status-list walks would corrupt both budgets.
  if (localPlayer) actorstate_tic();

  // Secret trigger scan; secret_volume_tic answers a different switch ("Secret areas") and reads sectors/walls only.
  if (localPlayer) { trigger_tic(); secret_volume_tic(); }

  // Secret-path search, last: it sweeps sectors/walls THEN walks statnum 9, so it must not interleave with the sprite
  // walks above (shared cycle counter).
  if (localPlayer) secretpath_tic();
}

// The one hook DllMain installs: no config, log or engine reads. It logs nothing because it runs before log_init;
// mod_deferred_init reports the result.
bool input_install_entry() {
  return hook_iat(rva::IAT_SDL_POLLEVENT, (void*)&hooked_poll, (void**)&s_origPoll);
}

// Split off because it reads g_cfg, not yet loaded when the entry half runs.
void input_install_deferred() {
  resolve_toggle_scancode();
  if (!hook_iat(rva::IAT_SDL_GETKEYBOARDSTATE, (void*)&hooked_get_keyboard_state, (void**)&s_origGetKeyboardState))
    LOGE("input: SDL_GetKeyboardState hook failed, held keys will keep reaching the game while the panel is open");
  if (!hook_iat(rva::IAT_SDL_GETMOUSESTATE, (void*)&hooked_get_mouse_state, (void**)&s_origGetMouseState))
    LOGE("input: SDL_GetMouseState hook failed, mouse buttons will keep reaching the game while the panel is open");
  // Never fatal: a stuck-looking cursor is a nuisance, not a crash.
  if (!hook_iat(rva::IAT_SDL_WARPMOUSEINWINDOW, (void*)&hooked_warp_mouse_in_window, (void**)&s_origWarpMouseInWindow))
    LOGE("input: SDL_WarpMouseInWindow hook failed, the panel's own cursor may appear stuck while open");
  else
    LOGI("input: SDL_WarpMouseInWindow hook installed");

  // Defence in depth: falsified as the movement choke point, harmless installed.
  void* origControlTarget = nullptr;
  const bool controlGetInputOk = hook_jmp_thunk(rva::THUNK_CONTROL_GETINPUT_RVA, rva::THUNK_CONTROL_GETINPUT_BYTES,
                                                 (void*)&hooked_control_get_input, &origControlTarget);
  engine_mark_hook_result(controlGetInputOk);
  if (controlGetInputOk) {
    s_origControlGetInput = (ControlGetInputFn)origControlTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origControlTarget - g_base);
    LOGI("input: CONTROL_GetInput thunk hook installed; recovered original target rva=%08X (expect 0000B830)%s",
         origRva, origRva == 0x0000B830 ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("input: CONTROL_GetInput thunk hook failed, panel will rely on the SDL-layer/engine-global "
         "clears above alone, which prior in-game testing showed insufficient by themselves");
  }

  // Primary: faketimerhandler's one call site goes through hooked_getinput.
  void* origGetInputTarget = nullptr;
  const bool getInputOk = hook_jmp_thunk(rva::THUNK_GETINPUT_RVA, rva::THUNK_GETINPUT_BYTES,
                                          (void*)&hooked_getinput, &origGetInputTarget);
  engine_mark_hook_result(getInputOk);
  if (getInputOk) {
    s_origGetInput = (GetInputFn)origGetInputTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origGetInputTarget - g_base);
    LOGI("input: getinput thunk hook installed; recovered original target rva=%08X (expect 000F9080)%s",
         origRva, origRva == 0x000F9080 ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("input: getinput thunk hook failed, panel will rely on the CONTROL_GetInput/SDL-layer/"
         "engine-global clears above alone, which prior in-game testing showed insufficient by themselves");
  }

  // Primary view-freeze mechanism: domovethings' call site goes through us.
  void* origProcessInputTarget = nullptr;
  const bool processInputOk = hook_jmp_thunk(rva::THUNK_PROCESSINPUT_RVA, rva::THUNK_PROCESSINPUT_BYTES,
                                              (void*)&hooked_processinput, &origProcessInputTarget);
  engine_mark_hook_result(processInputOk);
  if (processInputOk) {
    s_origProcessInput = (ProcessInputFn)origProcessInputTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origProcessInputTarget - g_base);
    LOGI("input: processinput thunk hook installed; recovered original target rva=%08X (expect 000FA450)%s",
         origRva, origRva == 0x000FA450 ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("input: processinput thunk hook failed, the view freeze will rely on hooked_getinput's "
         "once-per-tic write plus the close-time/settling-window writes alone, without the "
         "additional in-phase reinforcement right after processinput itself runs, which prior "
         "in-game testing showed insufficient by itself for holding return_to_center and horiz "
         "steady across a full tic");
  }
  // The twelfth thunk check (setaspect) runs in overlay_install(), the last install call.
}

void input_begin_frame() {
  g_ui.pressed = ui_press_edge(g_ui.down, s_downWas);
  s_downWas = g_ui.down;
  // Right button, same rule: a right-click on a slider opens numeric entry.
  g_ui.rpressed = ui_press_edge(g_ui.rdown, s_rdownWas);
  s_rdownWas = g_ui.rdown;

  refresh_key_state();

  // Redundant with the SDL_PollEvent drain on purpose: the order of the engine's event pump and control read is not
  // guaranteed every frame.
  clear_engine_input();

  // No render-clock write here (it left the aim jittery). Read-only sampler of horiz/ohoriz/ang/oang proving each
  // field pinned and each pair equal.
  if (s_menuOpen && engine_writes_ok() && s_viewSampleCount < VIEW_SAMPLE_FRAMES) {
    unsigned sampleFieldRvas[VF_COUNT];
    if (player_view_field_rvas(sampleFieldRvas)) {
      s_horizSamples[s_viewSampleCount] =
          read_field_bytes(g_base, sampleFieldRvas[VF_HORIZ], VIEW_FIELD_WIDTHS[VF_HORIZ]);
      s_ohorizSamples[s_viewSampleCount] =
          read_field_bytes(g_base, sampleFieldRvas[VF_OHORIZ], VIEW_FIELD_WIDTHS[VF_OHORIZ]);
      s_angSamples[s_viewSampleCount] =
          read_field_bytes(g_base, sampleFieldRvas[VF_ANG], VIEW_FIELD_WIDTHS[VF_ANG]);
      s_oangSamples[s_viewSampleCount] =
          read_field_bytes(g_base, sampleFieldRvas[VF_OANG], VIEW_FIELD_WIDTHS[VF_OANG]);
      s_viewSampleCount++;
      if (s_viewSampleCount == VIEW_SAMPLE_FRAMES && !s_viewSampleLogged) {
        s_viewSampleLogged = true;
        const SampleSummary horizSum  = summarize_samples(s_horizSamples, VIEW_SAMPLE_FRAMES);
        const SampleSummary ohorizSum = summarize_samples(s_ohorizSamples, VIEW_SAMPLE_FRAMES);
        const SampleSummary angSum    = summarize_samples(s_angSamples, VIEW_SAMPLE_FRAMES);
        const SampleSummary oangSum   = summarize_samples(s_oangSamples, VIEW_SAMPLE_FRAMES);
        LOGI("input: horiz render-clock samples for this open: %d frames, min=%ld max=%ld "
             "distinct=%d first=%ld last=%ld",
             VIEW_SAMPLE_FRAMES, horizSum.minVal, horizSum.maxVal, horizSum.distinctCount,
             s_horizSamples[0], s_horizSamples[VIEW_SAMPLE_FRAMES - 1]);
        LOGI("input: ohoriz render-clock samples for this open: %d frames, min=%ld max=%ld "
             "distinct=%d first=%ld last=%ld",
             VIEW_SAMPLE_FRAMES, ohorizSum.minVal, ohorizSum.maxVal, ohorizSum.distinctCount,
             s_ohorizSamples[0], s_ohorizSamples[VIEW_SAMPLE_FRAMES - 1]);
        LOGI("input: ang render-clock samples for this open: %d frames, min=%ld max=%ld "
             "distinct=%d first=%ld last=%ld",
             VIEW_SAMPLE_FRAMES, angSum.minVal, angSum.maxVal, angSum.distinctCount,
             s_angSamples[0], s_angSamples[VIEW_SAMPLE_FRAMES - 1]);
        LOGI("input: oang render-clock samples for this open: %d frames, min=%ld max=%ld "
             "distinct=%d first=%ld last=%ld",
             VIEW_SAMPLE_FRAMES, oangSum.minVal, oangSum.maxVal, oangSum.distinctCount,
             s_oangSamples[0], s_oangSamples[VIEW_SAMPLE_FRAMES - 1]);
      }
    }
  }

  // Does the game call SDL_GetKeyboardState every frame or fetch the pointer once? Logged once.
  s_frameCount++;
  if (s_frameCount == 300 && !s_loggedKeyboardDiag) {
    s_loggedKeyboardDiag = true;
    LOGI("input: SDL_GetKeyboardState numkeys=%d, called %u times in first 300 frames",
         s_keyStateCount, s_getKeyboardStateCalls);
  }

  // With the panel open, is anything re-setting the movement scancodes after clear_engine_input() zeroed them?
  if (s_frameCount == 900 && !s_loggedFrame900 && engine_writes_ok()) {
    s_loggedFrame900 = true;
    unsigned char* keystatus = g_base + rva::RVA_KEYSTATUS;
    LOGI("input: frame 900; menuOpen=%d keystatus[W,A,S,D] = %02X %02X %02X %02X",
         s_menuOpen ? 1 : 0,
         keystatus[0x11], keystatus[0x1E], keystatus[0x1F], keystatus[0x20]);
  }
}

// duke3d.exe imports SDL_ShowCursor, SDL_SetWindowGrab and SDL_WarpMouseInWindow (warp-based mouse-look), so forcing
// relative mode on at close could break it: query the real value on open, restore exactly it.
static bool s_mouseSyncResolved = false;
static SetRelativeMouseModeFn s_setRel = nullptr;
static GetRelativeMouseModeFn s_getRel = nullptr;
static ShowCursorFn s_showCursor = nullptr;
static bool s_relativeApiAvailable = false;

static bool s_mouseWasOpen = false;
static int  s_savedRelativeMode = 0;
static int  s_savedCursorVisible = 1;

static bool s_loggedFirstOpen = false;
static bool s_loggedFirstClose = false;

void input_sync_mouse_mode() {
  if (!s_mouseSyncResolved) {
    s_mouseSyncResolved = true;
    // Lazily from the first swap hook: SDL2.dll is certainly loaded and initialised by then.
    HMODULE sdl = GetModuleHandleA("SDL2.dll");
    if (sdl) {
      s_setRel     = (SetRelativeMouseModeFn)GetProcAddress(sdl, "SDL_SetRelativeMouseMode");
      s_getRel     = (GetRelativeMouseModeFn)GetProcAddress(sdl, "SDL_GetRelativeMouseMode");
      s_showCursor = (ShowCursorFn)GetProcAddress(sdl, "SDL_ShowCursor");
    }
    // Both required: without the getter the prior mode is unknowable, and guessing is the bug being fixed.
    s_relativeApiAvailable = (s_setRel != nullptr && s_getRel != nullptr);
    if (!s_relativeApiAvailable)
      LOGE("input: SDL_SetRelativeMouseMode/SDL_GetRelativeMouseMode not fully resolved, leaving relative mouse mode untouched");
    if (!s_showCursor)
      LOGE("input: SDL_ShowCursor not found, cursor visibility will not be restored");
  }

  const bool open = input_menu_open();
  if (open == s_mouseWasOpen) return;
  s_mouseWasOpen = open;

  if (open) {
    // Setter only when the queried mode is 1 (SDL's -1 failure and an already-off 0 mean leave it alone).
    int relBefore = -1, relAfter = -1;
    if (s_relativeApiAvailable) {
      s_savedRelativeMode = s_getRel();
      relBefore = s_savedRelativeMode;
      relAfter = s_savedRelativeMode;
      if (s_savedRelativeMode == 1) {
        s_setRel(0);
        relAfter = 0;
      }
    }
    int curBefore = -1, curAfter = -1;
    if (s_showCursor) {
      s_savedCursorVisible = s_showCursor(-1 /* SDL_QUERY: read without changing */);
      curBefore = s_savedCursorVisible;
      curAfter = ui_saved_toggle_value(/*panelOwnsInput=*/true, /*openValue=*/1, s_savedCursorVisible);
      s_showCursor(curAfter);
    }
    if (!s_loggedFirstOpen) {
      s_loggedFirstOpen = true;
      LOGI("input: panel opened; relative mouse mode was %d (setting to %d), cursor visibility was %d (setting to %d)",
           relBefore, relAfter, curBefore, curAfter);
    }
  } else {
    // Restore to 1 only if this code turned it off; unconditional would break warp-based mouse-look.
    int relRestored = -1, curRestored = -1;
    if (s_relativeApiAvailable && s_savedRelativeMode == 1) {
      s_setRel(1);
      relRestored = 1;
    }
    if (s_showCursor) {
      curRestored = ui_saved_toggle_value(/*panelOwnsInput=*/false, /*openValue=*/1, s_savedCursorVisible);
      s_showCursor(curRestored);
    }
    if (!s_loggedFirstClose) {
      s_loggedFirstClose = true;
      LOGI("input: panel closed; relative mouse mode restored to %d (was %d), cursor visibility restored to %d (was %d)",
           relRestored, s_savedRelativeMode, curRestored, s_savedCursorVisible);
    }
  }
}
