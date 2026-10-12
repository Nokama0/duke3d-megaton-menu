#include "firerate.h"
#include "engine.h"
#include "freecam.h"      // camera_active
#include "common.h"
#include "hooks.h"
#include "target.h"       // aim_throttle_due
#include "wswitch.h"      // wswitch_gate_reason
#include "config.h"
#include "log.h"
#include <windows.h>

// The fire rate, the one player-state write here; rules live in firerate.h. Runs from hooked_processinput's tail, AFTER the
// original. Never moves a counter at rest nor past the next engine event; refused while freeze/free camera owns the player.

// Carry turning a percentage into whole tic steps. Reset on every refused tic and weapon change: s_carry is shared by all
// weapons.
static double s_carry = 0.0;
static int    s_lastWeapon = -1;   // -1 seeds the very first tic as a "switch"

const unsigned FIRERATE_DIAG_WINDOW_MS = 3000;
static bool firerate_diag_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!aim_throttle_due((unsigned)now, (unsigned)s_lastMs, s_first, FIRERATE_DIAG_WINDOW_MS))
    return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

void firerate_tic(bool freezeActive) {
  // camera_ps_rva BOUNDS the player index before it becomes an address; no (short) cast: read_field_bytes sign-extends.
  unsigned psRva = 0;
  const bool havePs = engine_verified() && camera_ps_rva(engine_myconnectindex(), &psRva);
  const int weapon   = havePs ? (int)read_field_bytes(g_base, psRva + rva::PS_CURR_WEAPON, 2) : -1;
  const int kickback = havePs ? (int)read_field_bytes(g_base, psRva + rva::PS_KICKBACK_PIC, 2) : 0;
  const FireRateWeapon* t = firerate_weapon(weapon);
  const bool allow = firerate_gate(havePs, true, freezeActive, camera_active(),
                                   g_cfg.firerate, kickback, t != 0);
  int adv = kickback, steps = 0;
  if (!allow) {
    s_carry = 0.0;
  } else {
    s_carry = firerate_carry_for_weapon(s_carry, s_lastWeapon, weapon);
    s_lastWeapon = weapon;
    steps = firerate_steps(g_cfg.firerate, &s_carry);
    adv = firerate_advance(*t, kickback, steps);
    if (adv != kickback) write_field_bytes(g_base, psRva + rva::PS_KICKBACK_PIC, 2, (long)adv);
  }

  // Throttled and inside the debug-log gate; runs even when the gate refused, so gate= says why.
  if (g_cfg.debuglog != 0 && firerate_diag_due()) {
    const int wpos = havePs ? (int)(short)read_field_bytes(g_base, psRva + rva::PS_WEAPON_POS, 2) : 0;
    LOGI("firerate: weapon=%d kb=%d adv=%d steps=%d gate=%s switch=%s", weapon, kickback, adv, steps,
         firerate_gate_reason(havePs, true, freezeActive, camera_active(),
                              g_cfg.firerate, kickback, t != 0),
         wswitch_gate_reason(havePs, true, freezeActive, camera_active(), g_cfg.switchspeed, wpos));
  }
}
