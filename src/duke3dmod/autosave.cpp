#include "common.h"
#include "hooks.h"
#include "autosave.h"
#include "config.h"
#include "engine.h"
#include "freecam.h"
#include "input.h"
#include "shot.h"       // rva::PS_GM
#include "trigger.h"    // trigger_map_signature
#include "wallclock.h"
#include "log.h"
#include <string.h>

// Autosave's engine half: the reads feeding autosave.h's rule and the call into dnSaveGame. Not a tic-clock write: it runs from
// hooked_poll's drain-complete point, the quicksave key's window.

typedef int (__cdecl *SaveGameFn)(int slot);

// ps.gm 4 is the game proper; menu, end of level, demo and typing states are no moment to save.
static const int PS_GM_INGAME = 4;

static AutosaveState s_state = { 0, 0, 0 };
// -1 not asked, 0 mismatch, 1 ok; cached so a wrong binary is refused without re-logging.
static int s_bodyVerdict = -1;

static bool savegame_body_ok() {
  if (s_bodyVerdict < 0) {
    s_bodyVerdict = memcmp(g_base + rva::DNSAVEGAME_BODY_RVA, rva::DNSAVEGAME_BODY_BYTES,
                           sizeof rva::DNSAVEGAME_BODY_BYTES) == 0 ? 1 : 0;
    LOGI("autosave: dnSaveGame fingerprint %s", s_bodyVerdict ? "ok" : "mismatch");
  }
  return s_bodyVerdict == 1;
}

void autosave_frame() {
  unsigned psRva = 0;
  const bool havePs = engine_verified() && camera_ps_rva(engine_myconnectindex(), &psRva);
  const int gm = havePs ? (int)read_field_bytes(g_base, psRva + rva::PS_GM, 1) : 0;
  // A dead player's save would overwrite the slot with the death they are about to reload.
  int health = 0;
  if (havePs) {
    const int mySprite = (int)(short)read_field_bytes(g_base, psRva + rva::PS_I, 2);
    health = engine_sprite_field_s(mySprite, SPR_EXTRA);
  }
  const int inPlay = (havePs && gm == PS_GM_INGAME && health > 0
                      && !engine_paused() && !input_menu_open() && !camera_active()) ? 1 : 0;
  // Same-map hint as the per-level scans; a collision costs one restarted interval.
  const unsigned sig = trigger_map_signature(engine_numsectors(), engine_numwalls(), 0);

  if (!autosave_step(&s_state, g_cfg.autosavemins, wallclock_ms(), sig, inPlay)) return;
  if (!engine_verified() || !savegame_body_ok()) return;

  const int slot = autosave_slot_clamped(g_cfg.autosaveslot);
  const SaveGameFn fn = (SaveGameFn)(g_base + rva::DNSAVEGAME_BODY_RVA);
  const int rc = fn(slot);
  LOGI("autosave: slot %d saved (rc=%d)", slot, rc);
}
