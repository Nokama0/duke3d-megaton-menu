#include "nocull.h"
#include "common.h"
#include "config.h"
#include "engine.h"
#include "freecam.h"
#include "hooks.h"
#include "log.h"
#include "mouseaim.h"
#include "world.h"
#include "dollhouse.h"
#include <windows.h>

// "Disable culling", the DLL half: redirects a live call site and reads gotsector from engine memory, so it cannot link
// into test_host. Rules are in nocull.h.

typedef void (__cdecl* ScanSectorFn)(long);
static ScanSectorFn s_origScanSector = 0;

static bool diag_throttle_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!s_first && (now - s_lastMs) < 1000) return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

// hooked_polymost_scansector: the original runs first on every path; the rest is extra calls for sectors the seed's BFS
// missed. cameraActive is the render-clock snapshot, not the tic flag. nocull_budget_allows reserves half the free engine
// counters for the engine's own second scansector call (polymost_drawalls, VA 0x0048125B) after this hook returns.
static void __cdecl hooked_polymost_scansector(long sectnum) {
  // Must be first: polymost reads the six view doubles while scanning walls, so mouse aim's fractional view must be in
  // place before the seed scan.
  mouseaim_render_override();

  // Must be here: the one moment the projection matrix resizeglcheck loaded is current, modelview is still identity and the
  // projection doubles hold this pass's values. Read-only; world_capture decides itself whether to record.
  world_capture();
  dollhouse_frame_begin();


  if (dollhouse_skip_seed(engine_in_prepare_mirror())) return;

  if (s_origScanSector) s_origScanSector(sectnum);

  FreecamState fc;
  const bool haveSnap = camera_snapshot(&fc);
  const bool cameraActive = haveSnap && fc.active;
  const bool bracketArmed = camera_render_armed();
  const bool inMirror = engine_in_prepare_mirror();

  if (!nocull_should_scan(g_cfg.freecamnocull != 0, cameraActive, bracketArmed, inMirror)) return;

  const int numscansEntry = engine_numscans();
  const int numbunchesEntry = engine_numbunches();

  const int numsectors = engine_numsectors();
  const unsigned char* gotsector = g_base + rva::GOTSECTOR;

  unsigned scanned = 0;
  int from = -1;
  for (;;) {
    const int s = nocull_next_unscanned(gotsector, numsectors, from);
    if (s < 0) break;
    const int numscansNow = engine_numscans();
    const int numbunchesNow = engine_numbunches();
    const int wallnum = engine_sector_wallnum(s);
    if (!nocull_budget_allows(numscansEntry, numbunchesEntry, numscansNow, numbunchesNow, wallnum)) break;
    if (s_origScanSector) s_origScanSector(s);
    scanned++;
    from = s;
  }

  if (g_cfg.debuglog != 0 && scanned > 0 && diag_throttle_due()) {
    LOGI("nocull: scanned %u sectors beyond the seed this frame (numsectors=%d)",
         scanned, numsectors);
  }
}

// Own install step, called once from mod_deferred_init; failure is never fatal (the row shows "(Unavailable)"). Not in
// engine_mark_hook_result: that aggregate would make a lone call-site mismatch suspect the whole binary.

bool nocull_install() {
  void* origTarget = 0;
  const bool ok = hook_call_site(rva::NOCULL_CALL_SITE_RVA, rva::NOCULL_CALL_SITE_BYTES,
                                 (void*)&hooked_polymost_scansector, &origTarget);
  if (ok) {
    s_origScanSector = (ScanSectorFn)origTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
    LOGI("nocull: scansector call site redirected; recovered original target rva=%08X "
         "(expect %08X)%s",
         origRva, rva::POLYMOST_SCANSECTOR_BODY_RVA,
         origRva == rva::POLYMOST_SCANSECTOR_BODY_RVA ? "" : " -- MISMATCH, this may not be the right call site");
  } else {
    nocull_mark_unavailable();
    LOGE("nocull: scansector call site did not fingerprint-match; \"Disable culling\" stays "
         "unavailable for this session, everything else keeps running");
  }
  return ok;
}
