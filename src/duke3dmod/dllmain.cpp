#include "common.h"
#include "hooks.h"
#include "config.h"
#include "log.h"
#include "paths.h"
#include "overlay.h"
#include "input.h"
#include "silent.h"
#include "freecam.h"
#include "nocull.h"
#include "dollhouse.h"
#include "world.h"
#include "shot.h"
#include "mouseaim.h"
#include "dmgnum.h"
#include "engine.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// tools/uninstall.ps1 deletes bin\duke3dmod.dll only when the file carries this string; exported so the linker keeps it.
extern "C" __declspec(dllexport) const char NukePanelMarker[] = "NUKEPANEL-DUKE3DMOD";

typedef void (__cdecl *SwapWindowFn)(void*);
static SwapWindowFn s_origSwap = nullptr;
static bool s_inited = false;
static unsigned s_frames = 0;

static char s_iniPath[MAX_PATH];
static char s_logPath[MAX_PATH];
// Set by DllMain when the SDL_PollEvent IAT hook refused to install; read once log_init can report it.
static bool s_entryHookFailed = false;
// Config as of the last logged diff; seeded in mod_deferred_init so the first frame does not report every switch changed.
static Config s_cfgLogged;

static void first_frame_init() {
  s_inited = true;
  LOGI("first frame; rendmode=%d bpp=%d xdim=%d ydim=%d",
       engine_rendmode(), *(int*)(g_base + rva::BPP),
       engine_xdim(),     engine_ydim());
}

static void __cdecl hooked_swap(void* window) {
  if (!s_inited) first_frame_init();
  s_frames++;
  if (s_frames == 600) LOGI("600 frames drawn, hook is stable");
  // Per-frame input bookkeeping before the panel draws or hit-tests: the click press-edge, and relative-mouse mode synced to
  // whether the panel is open.
  input_begin_frame();
  input_sync_mouse_mode();
  overlay_frame();
  // "Debug log" is LIVE, pushed into log.cpp every frame. Turning it OFF logs the diff BEFORE log_set_enabled(false), or the
  // line would be lost; other switches log after enabling (a fresh file gets its first line).
  const bool turningLogOff = g_cfgDirty && s_cfgLogged.debuglog != 0 && g_cfg.debuglog == 0;
  if (turningLogOff) {
    config_log_state_changes(s_cfgLogged, g_cfg);
    s_cfgLogged = g_cfg;
    config_save(s_iniPath);
    log_set_enabled(false);
  } else {
    log_set_enabled(g_cfg.debuglog != 0);
    if (g_cfgDirty) {
      config_log_state_changes(s_cfgLogged, g_cfg);
      s_cfgLogged = g_cfg;
      config_save(s_iniPath);
    }
  }
  s_origSwap(window);
}

static void build_paths() {
  s_iniPath[0] = 0;
  s_logPath[0] = 0;

  char exe[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, exe, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;

  char* slash = strrchr(exe, '\\');
  if (slash) *(slash + 1) = 0;
  else exe[0] = 0;

  // An over-long directory must degrade, not abort: build_mod_paths truncates safely, where sprintf_s's CRT handler would
  // terminate the process.
  if (!build_mod_paths(exe, s_iniPath, sizeof(s_iniPath), s_logPath, sizeof(s_logPath))) {
    // Truncated: empty paths mean defaults and no logging, and the game still starts.
    s_iniPath[0] = 0;
    s_logPath[0] = 0;
  }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  // UNDER THE LOADER LOCK: one module-handle query and one IAT slot write, nothing else.
  g_base = (unsigned char*)GetModuleHandleA(NULL);
  // If the write FAILS (hook_iat has four refusal paths) hooked_poll never runs, so the body runs inline rather than leave a
  // silently absent mod. DONE is marked BEFORE the body so a poll arriving another way cannot run the .text patches twice.
  if (init_entry_outcome(input_install_entry()) == INIT_ENTRY_INLINE) {
    s_entryHookFailed = true;
    input_mark_init_done();
    mod_deferred_init();
  }
  return TRUE;
}

// Runs once on the game thread from hooked_poll, or inline from DllMain if the entry hook failed. Patching live .text is safe:
// each install is ONE non-atomic five-byte memcpy and the game's one logic thread is inside hooked_poll throughout (this DLL
// creates none). Inert until it runs: g_cfg is a zeroed POD, log_write returns at once, no engine read happens.
void mod_deferred_init() {
  build_paths();
  config_load(s_iniPath);
  log_init(s_logPath, g_cfg.debuglog != 0);
  if (s_entryHookFailed)
    LOGE("deferred: entry hook failed; initialised inline under the loader lock");
  s_cfgLogged = g_cfg;
  LOGI("deferred: Nuke Panel attached, base=%p", g_base);
  hook_iat(rva::IAT_SDL_GL_SWAPWINDOW, (void*)&hooked_swap, (void**)&s_origSwap);
  input_install_deferred();
  // Thunks install BEFORE overlay_install(), which logs the final fingerprint verdict. Never fatal: a failure is logged and
  // the feature is off.
  silent_install();
  camera_install();
  shot_install();
  mouseaim_install();
  dmgnum_install();
  // Does NOT feed engine_mark_hook_result like the twelve thunks, so one mismatch here cannot take every feature down.
  nocull_install();
  dollhouse_install();
  // Hooks nothing and cannot fail: it arms nocull's scansector redirect and shot's displaymasks thunk, so it goes after both.
  world_install();
  // With mouseaim and nocull both done the session's combination is known; this logs the one case that verdict does not narrate.
  mouseaim_note_render_redirect_result();
  overlay_install();
  engine_install_badguy();
  // Page-protection hook for the accuracy PatchBracket; it fails closed, so losing it silently disables perfect accuracy.
  engine_install_accuracy_protect();
  // Action-pointer query hook for the death-state diagnostic; fails closed, so losing it reports every action unreadable.
  engine_install_action_query();
  LOGI("deferred: init complete on the game thread");
}
