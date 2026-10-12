#include "overlay.h"
#include "canvas.h"
#include "common.h"
#include "hooks.h"
#include "log.h"
#include "input.h"
#include "engine.h"
#include "menu.h"
#include "config.h"
#include "fov.h"
#include "esp.h"
#include "shot.h"
#include "dmgnum.h"
#include "trigger.h"

// FOV: intercept setaspect, don't call it: the renderer reads values DERIVED from viewingrange, and the engine calls
// setaspect on its own. The argument is SCALED, never substituted: on a non-4:3 display it arrives pre-scaled by the aspect
// correction (3440x1440 at 90 degrees: 117418, not 65536), and an absolute value would narrow every FOV by ~1.79x.
typedef void (__cdecl *SetAspectFn)(long viewingrange, long yxaspect);
static SetAspectFn s_origSetAspect = nullptr;

// Current FOV multiplier (16.16; 65536 == 1.0 at 90 degrees), seeded at install so an early setaspect call scales correctly.
static int s_fovTarget = 65536;

static bool s_wasOn = false;
static int  s_diagRemaining = 0;  // hooked_setaspect calls left to log for this enable, capped at 10

static void apply_fov() {
  const bool on = g_cfg.fovon;
  if (on && !s_wasOn) s_diagRemaining = 10;
  s_wasOn = on;
  if (!on) return;
  s_fovTarget = fov_to_viewingrange(g_cfg.fov);
}

// Scales the engine's viewingrange by our multiplier while on. Calls the original ALWAYS so the engine's aspect state keeps
// updating; yxaspect is never touched: hardcoding it breaks it.
static void __cdecl hooked_setaspect(long viewingrange, long yxaspect) {
  const bool on = g_cfg.fovon;
  const long substituted = on ? fov_scale_viewingrange(viewingrange, s_fovTarget) : viewingrange;
  const bool doLog = on && s_diagRemaining > 0;

  if (s_origSetAspect) s_origSetAspect(substituted, yxaspect);

  // Bounded diagnostic, first 10 calls after each enable: equal values on every call means the interception is complete.
  if (doLog) {
    const int readback = engine_viewingrange();
    const int recip     = engine_viewingrangerecip();
    LOGI("fov diag: engine passed=%ld substituted=%ld readback=%d viewingrangerecip=%d %s",
         viewingrange, substituted, readback, recip,
         readback == (int)substituted ? "(sticking)" : "(overwritten)");
    s_diagRemaining--;
  }
}

// Installs the setaspect intercept after g_base is set and config_load has run. Never fatal: a failed hook leaves FOV inert.
void overlay_install() {
  s_fovTarget = fov_to_viewingrange(g_cfg.fov);

  void* origTarget = nullptr;
  const bool setAspectOk = hook_jmp_thunk(rva::THUNK_SETASPECT_RVA, rva::THUNK_SETASPECT_BYTES,
                                           (void*)&hooked_setaspect, &origTarget);
  engine_mark_hook_result(setAspectOk);
  if (setAspectOk) {
    s_origSetAspect = (SetAspectFn)origTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
    LOGI("overlay: setaspect thunk hook installed; recovered original target rva=%08X (expect %08X)%s",
         origRva, rva::SETASPECT,
         origRva == rva::SETASPECT ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("overlay: setaspect thunk hook failed, FOV toggle will have no visible effect");
  }

  // The twelfth and final jmp-thunk fingerprint check, so engine_verified() is final here; logged once.
  if (!engine_verified())
    LOGE("overlay: engine binary fingerprint check failed (a jmp-thunk byte mismatch, see above), "
         "every raw hardcoded-RVA engine-memory read/write in this DLL (keystatus, mouse deltas, "
         "the input gates, the view-freeze fields, and the targeting reads and writes) is now "
         "disabled; the panel will open but have no effect on the game");
}

void overlay_frame() {
  const int rendmode = engine_rendmode();
  if (rendmode != 3) {
    static bool warned = false;
    if (!warned) { warned = true; LOGE("rendmode %d is not GL, overlay disabled", rendmode); }
    return;
  }
  const int w = engine_xdim();
  const int h = engine_ydim();
  if (w <= 0 || h <= 0) return;

  apply_fov();

  // Touch no GL state with nothing to draw: canvas_begin/end bracket glPushAttrib(GL_ALL_ATTRIB_BITS), two matrix pushes
  // and ~nine toggles. menuOpen includes the closing animation so the panel fades out.
  const bool menuOpen = input_menu_open() || menu_closing_animation_active();
  const bool anyEspOn = esp_any_on();
  if (!overlay_should_draw(menuOpen, anyEspOn, g_cfg.dmgnumbers != 0,
                           g_cfg.secrettriggers != 0)) return;

  canvas_begin(w, h);
  // Screenshot mode hides the markers but NOT the panel, the only way to turn it off. World overlays only while a level
  // is played: the tally and the game's menus sit over a stale world.
  if (!shot_hides_overlays(shot_active_now())
      && overlay_world_should_draw(engine_player_in_play())) {
    esp_draw(w, h);
    // Same gate, after the markers, so a number sits on top of its box.
    dmgnum_draw(w, h);
    // Same gate, last: its LINES cross the screen, so anything under them stays readable.
    trigger_draw(w, h);
  }
  // menu_draw has no open/closed guard: this `if` alone stops the panel drawing permanently when an ESP category is on.

  if (menuOpen) menu_draw(w, h);
  canvas_end();
}
