#include "wswitch.h"
#include "engine.h"
#include "freecam.h"
#include "common.h"
#include "hooks.h"
#include "config.h"

// Weapon switch speed: the one player-state write here, every decision a pure rule in wswitch.h. Called from
// hooked_processinput's tail: the counter read is what the engine's step left and the write is what the NEXT tic steps from.
static double s_carry = 0.0;

void wswitch_tic(bool freezeActive) {
  unsigned psRva = 0;
  const bool havePs = engine_verified() && camera_ps_rva(engine_myconnectindex(), &psRva);
  const int pos = havePs ? (int)(short)read_field_bytes(g_base, psRva + rva::PS_WEAPON_POS, 2) : 0;
  if (!wswitch_gate(havePs, true, freezeActive, camera_active(), g_cfg.switchspeed, pos)) {
    s_carry = 0.0;
    return;
  }
  const int adv = wswitch_advance(pos, firerate_steps(g_cfg.switchspeed, &s_carry));

  if (adv != pos) write_field_bytes(g_base, psRva + rva::PS_WEAPON_POS, 2, (long)adv);
}
