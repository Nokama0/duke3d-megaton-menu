#include "test_common.h"
#include "../dmgnum.h"
#include "../trigger.h"
#include "../firerate.h"
#include "../anim.h"

// Damage-number rules: event filter, ring, expiry, rise/fade curves, colour and same-victim stacking (pure, dmgnum.h).
void tb_0096_phase_8_task1_damage_numbers() {
  CHECK( dmgnum_event_wanted(17, 17, true,  25));
  CHECK(!dmgnum_event_wanted(18, 17, true,  25));
  CHECK(!dmgnum_event_wanted(17, 17, false, 25));
  CHECK(!dmgnum_event_wanted(17, 17, true,   0));
  CHECK(!dmgnum_event_wanted(17, 17, true,  -1));
  CHECK(!dmgnum_event_wanted(-1, 17, true,  25));

  DmgRing r; dmgnum_reset(&r);
  CHECK(r.next == 0);
  for (int i = 0; i < DMGNUM_MAX; i++) CHECK(!r.e[i].live);
  DmgEvent ev = { 100, 200, 300, 25, false, 7, 1000u, true };
  for (int i = 0; i < DMGNUM_MAX + 3; i++) { ev.damage = i; dmgnum_push(&r, ev); }
  CHECK(r.next == 3);
  CHECK(r.e[0].damage == DMGNUM_MAX);
  CHECK(r.e[2].damage == DMGNUM_MAX + 2);
  CHECK(r.e[3].damage == 3);

  ev.t0 = 1000u;
  CHECK( dmgnum_is_live(ev, 1000u));
  CHECK( dmgnum_is_live(ev, 1000u + DMGNUM_LIFE_MS - 1));
  CHECK(!dmgnum_is_live(ev, 1000u + DMGNUM_LIFE_MS));
  ev.t0 = 0xFFFFFF00u;                          // 256 ms before the wrap
  CHECK( dmgnum_is_live(ev, 0x00000100u));
  CHECK(!dmgnum_is_live(ev, 0x00000200u));
  CHECK(dmgnum_progress(1000u, 1000u) == 0.0);
  CHECK(fabs(dmgnum_progress(1000u, 1375u) - 0.5) < 1e-9);
  CHECK(dmgnum_progress(1000u, 5000u) == 1.0);

  CHECK(dmgnum_rise_px(0.0, 20) == 0);
  CHECK(dmgnum_rise_px(1.0, 20) == 20);
  CHECK(dmgnum_rise_px(0.5, 20) > 10);
  int last = -1; bool mono = true;
  for (int i = 0; i <= 100; i++) { const int v = dmgnum_rise_px(i / 100.0, 20); if (v < last) mono = false; last = v; }
  CHECK(mono);

  // Alpha: opaque for the first two thirds, then linear to zero.
  CHECK(dmgnum_alpha(0.0) == 255);
  CHECK(dmgnum_alpha(2.0 / 3.0) == 255);
  // The 2/3 reading alone does not pin where the opaque stretch ends (ramp anchored so 255*(1-p)*3==255 at p=2/3); the
  // midpoint and sweep do.
  CHECK(dmgnum_alpha(0.5) == 255);
  int lastA = 256; bool fade = true;
  for (int i = 0; i <= 100; i++) {
    const int a = dmgnum_alpha(i / 100.0);
    if (a < 0 || a > 255 || a > lastA) fade = false;
    lastA = a;
  }
  CHECK(fade);
  CHECK(dmgnum_alpha(5.0 / 6.0) > 100 && dmgnum_alpha(5.0 / 6.0) < 155);
  CHECK(dmgnum_alpha(1.0) == 0);
  CHECK(canvas_with_alpha(0xFF112233u, 0x80) == 0x80112233u);
  CHECK(canvas_with_alpha(0x00112233u, 300) == 0xFF112233u);
  CHECK(canvas_with_alpha(0xFF112233u, -5) == 0x00112233u);

  CHECK(dmgnum_colour(false, 0xFFAAAAAAu, 0xFFBBBBBBu) == 0xFFAAAAAAu);
  CHECK(dmgnum_colour(true,  0xFFAAAAAAu, 0xFFBBBBBBu) == 0xFFBBBBBBu);

  // Stacking: the number of LIVE events on the same victim pushed before this one.
  dmgnum_reset(&r);
  DmgEvent a = { 0, 0, 0, 10, false, 7, 1000u, true };
  DmgEvent b = a; b.victim = 9;
  DmgEvent c = a; c.t0 = 1100u;
  DmgEvent d = a; d.t0 = 1200u;
  dmgnum_push(&r, a); dmgnum_push(&r, b); dmgnum_push(&r, c); dmgnum_push(&r, d);
  CHECK(dmgnum_stack_index(r, 0, 1250u) == 0);
  CHECK(dmgnum_stack_index(r, 1, 1250u) == 0);
  CHECK(dmgnum_stack_index(r, 2, 1250u) == 1);
  CHECK(dmgnum_stack_index(r, 3, 1250u) == 2);
  CHECK(dmgnum_stack_index(r, 3, 1000u + DMGNUM_LIFE_MS) == 1);

  // The ifhitbyweapon thunk decodes to the documented body, so the five fingerprint bytes and IFHITBYWEAPON_BODY_RVA
  // cannot drift apart (the pin every other thunk carries).
  #define P8_REL32(b) ((unsigned)(b)[1] | ((unsigned)(b)[2] << 8) | ((unsigned)(b)[3] << 16) | ((unsigned)(b)[4] << 24))
  CHECK(thunk_decode_target(rva::THUNK_IFHITBYWEAPON_RVA, P8_REL32(rva::THUNK_IFHITBYWEAPON_BYTES)) == rva::IFHITBYWEAPON_BODY_RVA);
  #undef P8_REL32
  CHECK(rva::HITTYPE_PICNUM == 2 && rva::HITTYPE_ANG == 4 &&
        rva::HITTYPE_EXTRA == 6 && rva::HITTYPE_OWNER == 8);
  CHECK(rva::HITTYPE_OWNER + 2 <= rva::HITTYPE_STRIDE);

  CHECK(!overlay_should_draw(false, false, false, false));
  CHECK( overlay_should_draw(false, false, true,  false));

  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0) == 1);
}

// Secret-trigger resolver: which trigger/activator pairs show, what counts as "at or beside a secret", chain naming and
// rescan timing (pure, trigger.h).
void tb_0097_phase_8_task2_trigger_resolver() {
  // Map: sectors 0 (street, lotag 0), 1 (door, 20), 2 (secret, 32767), 3 (found secret, 0), 4 (locked door, 20 |
  // 0x4000). Walls: sector 1 has walls 0..3, wall 1 faces sector 2; sector 4 has walls 4..7, wall 5 faces sector 3.
  TrigSector sec[5] = { {0, 8, 4}, {20, 0, 4}, {32767, 12, 4}, {0, 16, 4}, {20 | 0x4000, 4, 4} };
  short wallNext[20]; for (int i = 0; i < 20; i++) wallNext[i] = -1;
  wallNext[1] = 2;  wallNext[5] = 3;
  CHECK( trigger_sector_near_secret(1, sec, 5, wallNext, 20));
  CHECK(!trigger_sector_near_secret(4, sec, 5, wallNext, 20));
  CHECK( trigger_sector_near_secret(2, sec, 5, wallNext, 20));
  CHECK(!trigger_sector_near_secret(0, sec, 5, wallNext, 20));
  CHECK(!trigger_sector_near_secret(7, sec, 5, wallNext, 20));
  TrigSector bad = { 0, 18, 10 };                                // wallptr + wallnum past nwall
  TrigSector sec2[1] = { bad };
  CHECK(!trigger_sector_near_secret(0, sec2, 1, wallNext, 20));

  // nextsector == -1 is the engine's "no neighbour", never sector 0: sector 0 here IS a secret, and sector 1's wall
  // carrying -1 must not read as bordering it.
  TrigSector secB2[2] = { { 32767, 0, 0 }, { 0, 0, 1 } };
  short wallNextB2[1] = { -1 };
  CHECK(!trigger_sector_near_secret(1, secB2, 2, wallNextB2, 1));

  TrigSector sec6[6] = { {0, 8, 4}, {20, 0, 4}, {32767, 12, 4}, {0, 16, 4},
                         {20 | 0x4000, 4, 4}, {32767, 0, 0} };
  short wallNext6[20]; for (int i = 0; i < 20; i++) wallNext6[i] = -1;
  wallNext6[8] = 5;                                              // sector 0's first wall faces "sector 5"
  CHECK(!trigger_sector_near_secret(0, sec6, 5, wallNext6, 20));

  // Sprites: 0 a touchplate in the street, lotag 7; 1 an activator in the door sector, lotag 7 (the couch case); 2 an
  // activator lotag 9 in sector 0 (leads nowhere); 3 a switch lotag 11; 4 a LOCKED activator lotag 11 in sector 4; 5 an
  // exhausted touchplate lotag 7; 6 a touchplate lotag 9.
  TrigSprite sp[7] = {
    { 0, TRIG_TOUCHPLATE, 0, 0,  7, 0, 0, 100, 100, 0, false },
    { 1, -1,              1, 0,  7, 0, 1, 500, 500, 0, false },
    { 2, -1,              1, 0,  9, 0, 0, 900, 900, 0, false },
    { 3, TRIG_SWITCH,     0, 0, 11, 0, 0, 200, 200, 0, false },
    { 4, -1,              1, 1, 11, 0, 4, 600, 600, 0, false },
    { 5, TRIG_TOUCHPLATE, 0, 0,  7, 2, 0, 300, 300, 0, true  },
    { 6, TRIG_TOUCHPLATE, 0, 0,  9, 0, 0, 400, 400, 0, false },
  };
  TrigChain out[TRIG_MAX_CHAINS];
  const int n = trigger_resolve(sp, 7, sec, 5, wallNext, 20, out, TRIG_MAX_CHAINS);
  CHECK(n == 1);                                   // the couch case only: 9 leads nowhere, the switch's door lost its secret, sprite 5 is used up
  CHECK(out[0].kind == TRIG_TOUCHPLATE && out[0].trigger == 0 && out[0].activator == 1 && out[0].sector == 1);
  CHECK(out[0].effect == TRIG_EFFECT_OPENS);
  CHECK(out[0].tx == 100 && out[0].ax == 500);

  // Restore the found secret and the locked chain appears with the "Unlocks" effect from the switch.
  sec[3].lotag = 32767;
  const int n2 = trigger_resolve(sp, 7, sec, 5, wallNext, 20, out, TRIG_MAX_CHAINS);
  CHECK(n2 == 2);
  int locked = -1; for (int i = 0; i < n2; i++) if (out[i].activator == 4) locked = i;
  CHECK(locked >= 0 && out[locked].kind == TRIG_SWITCH && out[locked].trigger == 3 && out[locked].effect == TRIG_EFFECT_UNLOCKS);

  CHECK(trigger_effect(32767, true,  false) == TRIG_EFFECT_SECRET);
  CHECK(trigger_effect(20,    false, false) == TRIG_EFFECT_OPENS);
  CHECK(trigger_effect(20,    false, true)  == TRIG_EFFECT_UNLOCKS);
  CHECK(streq(trigger_effect_label(TRIG_EFFECT_SECRET),  "Secret"));
  CHECK(streq(trigger_effect_label(TRIG_EFFECT_OPENS),   "Opens secret"));
  CHECK(streq(trigger_effect_label(TRIG_EFFECT_UNLOCKS), "Locked"));
  CHECK(streq(trigger_step_label(), "Step here"));

  CHECK(trigger_world_dist2d(0, 0, 3, 4) == 5);
  CHECK(trigger_world_dist2d(100, 200, 100, 200) == 0);
  CHECK(trigger_world_dist2d(-3, -4, 0, 0) == 5);
  CHECK(trigger_world_dist2d(7, 11, 3, 4) == trigger_world_dist2d(3, 4, 7, 11));
  CHECK(trigger_world_dist2d(-2000000000, 0, 2000000000, 0) == 0x7FFFFFFF);
  CHECK(trigger_world_dist2d(0, -2000000000, 0, 2000000000) == 0x7FFFFFFF);

  CHECK( trigger_end_in_range(1000, 6144));
  CHECK( trigger_end_in_range(6144, 6144));
  CHECK(!trigger_end_in_range(6145, 6144));
  CHECK( trigger_end_in_range(0,    6144));
  CHECK(!trigger_end_in_range(-1,   6144));

  // Where the segment crosses the near plane, as a parameter from the trigger end toward the activator end.
  CHECK(fabs(trigger_segment_clip_t(2.0, 10.0, 4.0) - 0.25) < 1e-12);
  CHECK(fabs(trigger_segment_clip_t(10.0, 2.0, 4.0) - 0.75) < 1e-12);
  CHECK(trigger_segment_clip_t(10.0, 10.0, 4.0) ==  0.0);
  CHECK(trigger_segment_clip_t( 2.0,  2.0, 4.0) == -1.0);
  CHECK(trigger_segment_clip_t( 4.0, 10.0, 4.0) ==  0.0);
  CHECK(trigger_segment_clip_t( 1.0,  1.0, 4.0) == -1.0);
  // Never outside [0, 1] over a sweep of depths on both sides of the plane, and the sweep reaches the clipping case (a
  // stub returning a constant would pass the bound alone).
  {
    bool bounded = true, sawClip = false;
    for (int ai = -40; ai <= 40; ai++)
      for (int bi = -40; bi <= 40; bi++) {
        const double t = trigger_segment_clip_t(ai * 0.5, bi * 0.5, 4.0);
        if (t < 0.0) { if (t != -1.0) bounded = false; continue; }
        if (t > 1.0) bounded = false;
        if (t > 0.0) sawClip = true;
      }
    CHECK(bounded && sawClip);
  }

  // The band case: 4.5 is past PROJ_NEAR_CLIP (4.0) but inside the old 1.0 margin's band, which refused it entirely.
  CHECK(trigger_segment_clip_t(4.5, 4.5, PROJ_NEAR_CLIP + 0.01) ==  0.0);
  CHECK(trigger_segment_clip_t(4.5, 4.5, PROJ_NEAR_CLIP + 1.0)  == -1.0);

  {
    int cx = 0, cy = 0, cz = 0;
    trigger_lerp_point(100, 200, 300, 500, 600, 700, 0.0, &cx, &cy, &cz);
    CHECK(cx == 100 && cy == 200 && cz == 300);
    trigger_lerp_point(100, 200, 300, 500, 600, 700, 1.0, &cx, &cy, &cz);
    CHECK(cx == 500 && cy == 600 && cz == 700);
    trigger_lerp_point(100, 200, 300, 500, 600, 700, 0.5, &cx, &cy, &cz);
    CHECK(cx == 300 && cy == 400 && cz == 500);
    trigger_lerp_point(0, 0, 0,  3,  3,  3, 0.5, &cx, &cy, &cz);
    CHECK(cx == 2 && cy == 2 && cz == 2);
    trigger_lerp_point(0, 0, 0, -3, -3, -3, 0.5, &cx, &cy, &cz);
    CHECK(cx == -2 && cy == -2 && cz == -2);
    trigger_lerp_point(-2000000000, 0, 0, 2000000000, 0, 0, 2.0, &cx, &cy, &cz);
    CHECK(cx == 0x7FFFFFFF);
  }

  // The clip follows `visible`, not depth alone: PROJ_REJECT_PITCH can refuse a nearby, vertically offset end with
  // gdepth still in front of the near plane. The draw walks the refused end toward the projecting one; these are the
  // walk's two pure pieces: the step, and the bound on how many may run.
  CHECK(trigger_bisect_toward(0.0, 1.0) == 0.5);
  CHECK(trigger_bisect_toward(1.0, 0.0) == 0.5);
  CHECK(trigger_bisect_toward(0.25, 0.25) == 0.25);
  CHECK(fabs(trigger_bisect_toward(0.5, 1.0) - 0.75) < 1e-12);
  CHECK(fabs(trigger_bisect_toward(1.0, 0.5) - 0.75) < 1e-12);
  {
    // Load-bearing: the loop's bound (unpinnable directly, it runs on the render thread). TRIG_CLIP_STEPS halvings land
    // within 1/256 (~a quarter metre at max draw distance) of the projecting end; the lower bound catches a stub that
    // jumps there.
    CHECK(TRIG_CLIP_STEPS == 8);
    double u = 0.0;
    for (int k = 0; k < TRIG_CLIP_STEPS; k++) u = trigger_bisect_toward(u, 1.0);
    CHECK(1.0 - u < 0.004 && 1.0 - u > 0.003);
    double d = 1.0;
    for (int k = 0; k < TRIG_CLIP_STEPS; k++) d = trigger_bisect_toward(d, 0.0);
    CHECK(d < 0.004 && d > 0.003);
  }

  // The clip plane is project_world's own near clip (project.h), so trigger.cpp cannot carry a drifting copy.
  CHECK(PROJ_NEAR_CLIP == 4.0);

  TrigChain one[1];
  CHECK(trigger_resolve(sp, 7, sec, 5, wallNext, 20, one, 1) == 1);
  CHECK(trigger_map_signature(100, 400, 12) != trigger_map_signature(100, 400, 13));
  CHECK(trigger_map_signature(100, 400, 12) == trigger_map_signature(100, 400, 12));

  // The secret lotag this header carries must match the engine's own (pinned against each other, not one including the
  // other).
  CHECK(TRIG_SECRET_LOTAG == SECRET_SECTOR_LOTAG);

  CHECK(!overlay_should_draw(false, false, false, false));
  CHECK( overlay_should_draw(false, false, false, true));
  CHECK( overlay_should_draw(false, false, true,  false));

  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0) == 1);
  CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0) == 2);

  // The ini key: off by default, any nonzero value reads back as 1, and survives a save/reload.
  {
    const char* t = "test_trigger_task2_phase8.ini";
    FILE* f = nullptr;
    config_load(tb_path("Z:|does|not|exist.ini"));   // the defaults, unreadable path
    CHECK(g_cfg.secrettriggers == 0);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nsecrettriggers=7\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.secrettriggers == 1);
    CHECK(config_save(t));
    config_load(t);
    CHECK(g_cfg.secrettriggers == 1);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nsecrettriggers=-3\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.secrettriggers == 1);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nsecrettriggers=0\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.secrettriggers == 0);

    remove(t);
    config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults in place
  }
}

// Fire-rate rules: per-weapon event tables, the 50-step clamp, fractional carry, advance and gate (pure, firerate.h).

// The engine's per-tic step for each weapon, transcribed from docs/re/damage-triggers-weapons.md C, so the suite can
// simulate a whole sequence with the advance applied and count shots. `held` = fire button.
static int fr_engine_step(int weapon, int kb, bool held, int* shots) {
  switch (weapon) {
    case 0:  kb++; if (kb == 7) (*shots)++; else if (kb == 14) kb = held ? 1 : 0; return kb;
    case 1:  if (kb == 1) (*shots)++; kb++; if (kb > 4) { /* ammo not a clip boundary */ kb = 0; } if (kb == 27) kb = 0; return kb;
    case 2:  kb++; if (kb == 4) (*shots)++; if (kb == 17 || kb == 20 || kb == 24) kb++; if (kb == 31) kb = 0; return kb;
    case 3:  kb++; if (kb < 13) { if (kb % 3 == 0) { (*shots)++; if (!held) kb = 0; } } else if (kb > 10) kb = held ? 1 : 0; return kb;
    case 4:  kb++; if (kb == 4) (*shots)++; else if (kb == 20) kb = 0; return kb;
    case 6:  if (kb < 11) kb++; else { kb = 0; (*shots)++; } return kb;
    case 7:  if (kb) { kb++; if (kb & 1) (*shots)++; if (kb > 5) kb = 0; } return kb;
    case 9:  if (kb < 4) { kb++; if (kb == 3) (*shots)++; } else kb = held ? 1 : 0; return kb;
    case 11: if (kb < 4) kb++; else { kb = 0; (*shots)++; } return kb;
  }
  return kb;
}

void tb_0098_phase_8_task3_fire_rate() {
  CHECK(firerate_rate_clamped(100) == 100 && firerate_rate_clamped(400) == 400);
  CHECK(firerate_rate_clamped(0) == 100 && firerate_rate_clamped(999) == 400);
  CHECK(firerate_rate_clamped(174) == 150 && firerate_rate_clamped(175) == 200 && firerate_rate_clamped(226) == 250);

  const int with[9] = { 0, 1, 2, 3, 4, 6, 7, 9, 11 };
  for (int i = 0; i < 9; i++) CHECK(firerate_weapon(with[i]) != 0);
  CHECK(firerate_weapon(5) == 0 && firerate_weapon(8) == 0 && firerate_weapon(10) == 0 && firerate_weapon(12) == 0 && firerate_weapon(-1) == 0);
  const FireRateWeapon& sg = *firerate_weapon(2);
  CHECK(sg.nEvents == 7 && sg.events[0] == 3 && sg.events[6] == 30);

  // Whole transcription, weapon by weapon. The simulation below asks the same table for the next event, so a missing
  // event there would be self-consistent.
  {
    const int nEv[9] = { 2, 5, 7, 5, 2, 1, 3, 3, 1 };
    const int ev[9][FIRERATE_EVENTS_MAX] = {
      { 6, 13 }, { 1, 2, 4, 7, 26 }, { 3, 12, 14, 16, 19, 23, 30 }, { 2, 5, 8, 11, 12 },
      { 3, 19 }, { 11 }, { 2, 4, 5 }, { 2, 3, 4 }, { 4 },
    };
    for (int i = 0; i < 9; i++) {
      const FireRateWeapon& t = *firerate_weapon(with[i]);
      CHECK(t.nEvents == nEv[i]);
      for (int j = 0; j < nEv[i] && j < t.nEvents; j++) CHECK(t.events[j] == ev[i][j]);
    }
  }

  double carry = 0.0;
  CHECK(firerate_steps(100, &carry) == 0 && carry == 0.0);
  int s = 0; for (int i = 0; i < 10; i++) s += firerate_steps(150, &carry); CHECK(s == 5);
  carry = 0.0; CHECK(firerate_steps(400, &carry) == 3 && carry == 0.0);
  carry = 0.0; CHECK(firerate_steps(250, &carry) == 1); CHECK(firerate_steps(250, &carry) == 2);
  CHECK(carry >= 0.0 && carry < 1.0);

  const FireRateWeapon& rpg = *firerate_weapon(4);          // events {3, 19}
  CHECK(firerate_next_event(rpg, 0) == 3 && firerate_next_event(rpg, 3) == 3 && firerate_next_event(rpg, 4) == 19);
  CHECK(firerate_next_event(rpg, 19) == 19 && firerate_next_event(rpg, 20) == -1);
  CHECK(firerate_advance(rpg, 1, 1) == 2 && firerate_advance(rpg, 1, 5) == 3);
  CHECK(firerate_advance(rpg, 3, 99) == 3);
  CHECK(firerate_advance(rpg, 4, 99) == 19);
  CHECK(firerate_advance(rpg, 19, 3) == 19 && firerate_advance(rpg, 5, 0) == 5 && firerate_advance(rpg, 5, -2) == 5);
  CHECK(firerate_advance(rpg, 0, 3) == 0);

  // Whole-sequence simulation, every weapon and rate: as many shots as stock over 600 tics or MORE, never a doubled
  // shot in one engine step, and the cycle never shorter than the events allow.
  const int rates[7] = { 100, 150, 200, 250, 300, 350, 400 };
  for (int wi = 0; wi < 9; wi++) {
    const int w = with[wi];
    const FireRateWeapon& t = *firerate_weapon(w);
    int stockShots = 0; int kb = 1;
    for (int tic = 0; tic < 600; tic++) { kb = fr_engine_step(w, kb, true, &stockShots); if (kb == 0) kb = 1; }
    for (int ri = 0; ri < 7; ri++) {
      int shots = 0; kb = 1; double c = 0.0; bool landedOnEvent = false, skipped = false;
      for (int tic = 0; tic < 600; tic++) {
        const int before = kb;
        kb = fr_engine_step(w, kb, true, &shots);
        if (kb == 0) { kb = 1; c = 0.0; continue; }        // the fire press
        const int nxt = firerate_next_event(t, kb);
        const int adv = firerate_advance(t, kb, firerate_steps(rates[ri], &c));
        if (nxt > 0 && adv > nxt) skipped = true;
        if (adv < kb) skipped = true;
        kb = adv; (void)before; (void)landedOnEvent;
      }
      CHECK(!skipped);
      CHECK(shots >= stockShots);
      if (rates[ri] == 100) CHECK(shots == stockShots);
    }
  }

  CHECK( firerate_gate(true,  true,  false, false, 200, 5, true));
  CHECK(!firerate_gate(false, true,  false, false, 200, 5, true));
  CHECK(!firerate_gate(true,  false, false, false, 200, 5, true));
  CHECK(!firerate_gate(true,  true,  true,  false, 200, 5, true));
  CHECK(!firerate_gate(true,  true,  false, true,  200, 5, true));
  CHECK(!firerate_gate(true,  true,  false, false, 100, 5, true));
  CHECK(!firerate_gate(true,  true,  false, false, 200, 0, true));
  CHECK(!firerate_gate(true,  true,  false, false, 200, 5, false));

  CHECK(streq(firerate_gate_reason(true,  true,  false, false, 200, 5, true),  "on"));
  CHECK(streq(firerate_gate_reason(false, true,  false, false, 200, 5, true),  "unverified"));
  CHECK(streq(firerate_gate_reason(true,  false, false, false, 200, 5, true),  "unverified"));
  CHECK(streq(firerate_gate_reason(true,  true,  true,  false, 200, 5, true),  "frozen"));
  CHECK(streq(firerate_gate_reason(true,  true,  false, true,  200, 5, true),  "camera"));
  CHECK(streq(firerate_gate_reason(true,  true,  false, false, 100, 5, true),  "off"));
  CHECK(streq(firerate_gate_reason(true,  true,  false, false, 200, 0, true),  "rest"));
  CHECK(streq(firerate_gate_reason(true,  true,  false, false, 200, 5, false), "notable"));

  // The carry survives a tic on the same weapon and is dropped the instant the weapon differs (firerate.cpp seeds it
  // -1).
  CHECK(firerate_carry_for_weapon(0.7, 3, 3)  == 0.7);
  CHECK(firerate_carry_for_weapon(0.7, 3, 4)  == 0.0);
  CHECK(firerate_carry_for_weapon(0.4, -1, 0) == 0.0);

  // The ini key is a range, not a switch: default 100 (identity, no player byte written), snapped to the slider's stops
  // on load, survives a save/reload.
  {
    const char* t = "test_firerate_task3_phase8.ini";
    FILE* f = nullptr;
    config_load(tb_path("Z:|does|not|exist.ini"));   // the defaults, unreadable path
    CHECK(g_cfg.firerate == FIRERATE_MIN);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nfirerate=174\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.firerate == 150);
    CHECK(config_save(t));
    config_load(t);
    CHECK(g_cfg.firerate == 150);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nfirerate=99999\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.firerate == FIRERATE_MAX);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nfirerate=-40\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.firerate == FIRERATE_MIN);

    remove(t);
    config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults in place
  }

  // The Weapons tab row: drawn, percent readout, short hint, drag reaches both ends, every drag-storable value is a
  // stop.
  {
    const int savedTab = menu_active_tab();
    const WeapState savedWeap = weap_snapshot();
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_set_tab(TAB_WEAPONS);
    g_cfg.firerate = FIRERATE_MIN;

    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const CanvasOp* rowLbl = canvas_record_find_text("Fire rate");
    CHECK(rowLbl != nullptr);
    CHECK(strlen("Compresses every frame that is not a shot.") < 63);
    if (rowLbl) {
      const CanvasOp* readout = find_readout_at_y(rowLbl->y);
      CHECK(readout && streq(readout->text, "100%"));
    }
    g_cfg.firerate = FIRERATE_MAX;
    CHECK(streq(tooltip_for_row("Fire rate", 3440, 1440),
                "Compresses every frame that is not a shot."));
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* lbl = canvas_record_find_text("Fire rate");
      CHECK(lbl);
      if (lbl) {
        const CanvasOp* readout = find_readout_at_y(lbl->y);
        CHECK(readout && streq(readout->text, "400%"));
      }
    }
    g_cfg.firerate = FIRERATE_MIN;

    CHECK(slider_row_reaches_ends(TAB_WEAPONS, "Fire rate", 3440, 1440,
                                  FIRERATE_MIN, FIRERATE_MAX,
                                  &g_cfg.firerate, FIRERATE_MIN));

    // A dragged 174 stores 150: the row snaps through firerate_rate_clamped. Walked across the whole row since which
    // pixel maps to 174 is layout's business.
    CHECK(firerate_rate_clamped(174) == 150);
    {
      const UiRect p = menu_panel_rect(3440, 1440);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* lbl = canvas_record_find_text("Fire rate");
      CHECK(lbl);
      bool allStops = true, saw150 = false, saw250 = false;
      if (lbl) {
        for (int x = p.x; x < p.x + p.w; x++) {
          g_cfg.firerate = FIRERATE_MIN;
          menu_set_tab(TAB_WEAPONS);
          g_ui.mx = x; g_ui.my = lbl->y; g_ui.down = true; g_ui.pressed = false;
          menu_draw(3440, 1440);
          const int v = g_cfg.firerate;
          if (v != firerate_rate_clamped(v)) allStops = false;
          if (v == 150) saw150 = true;
          if (v == 250) saw250 = true;
        }
      }
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      CHECK(allStops);
      CHECK(saw150 && saw250);
      g_cfg.firerate = FIRERATE_MIN;
    }

    {
      WeapState start = weap_snapshot();
      start.pistol = start.chaingun = start.shotgun = start.expander = 0;
      start.firerate = FIRERATE_MIN;
      bool multiHeld = false;
      const int mask = weap_sweep(3440, 1440, start, true, &multiHeld);
      CHECK((mask & WF_FIRERATE) != 0);
      CHECK(!multiHeld);
    }

    weap_restore(savedWeap);
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }
}

// Animation core (anim.h: ease, progress, tween, dt clamp, springs, style name/clamp) and the Settings tab: existence,
// placement, row-sweep, and a mid-transition click still landing on its logical row.
void tb_0099_phase_8_task4_anim_core_and_settings_tab() {
  CHECK(anim_ease_out(0.0) == 0.0 && anim_ease_out(1.0) == 1.0);
  CHECK(anim_ease_out(0.5) > 0.8);
  CHECK(anim_ease_out(-1.0) == 0.0 && anim_ease_out(2.0) == 1.0);

  CHECK(anim_progress(1000u, 1000u, 150) == 0.0);
  CHECK(fabs(anim_progress(1000u, 1075u, 150) - 0.5) < 1e-9);
  CHECK(anim_progress(1000u, 9000u, 150) == 1.0);
  CHECK(anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) > 0.25 && anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) < 0.27);
  CHECK(anim_progress(0u, 5u, 0) == 1.0);

  AnimTween tw = { 10.0, 20.0, 1000u, 100 };
  CHECK(anim_tween_value(tw, 1000u) == 10.0 && anim_tween_value(tw, 1100u) == 20.0);
  CHECK(anim_tween_value(tw, 1050u) > 15.0);

  // dt clamp: never negative, never over 50 ms (a hitch does not fling a spring).
  CHECK(anim_dt_clamped(1000u, 1016u) == 16.0);
  CHECK(anim_dt_clamped(1000u, 3000u) == 50.0);
  CHECK(anim_dt_clamped(1000u,  999u) == 0.0);

  // Integration order at the first step from rest: semi-implicit Euler moves position by the NEW velocity, so even the
  // first 16ms call carries the spring; explicit Euler cannot. ANIM_SPRING_SUBSTEP_MS cuts one call into 4 substeps of
  // 4ms; hand-computed semi-implicit gives x=0.05402 (explicit 0.0344), so 0.05 discriminates.
  {
    AnimSpring one = { 0.0, 0.0 };
    anim_spring_step(&one, 1.0, 400.0, 40.0, 16.0);
    CHECK(one.x > 0.05);
  }

  AnimSpring s = { 0.0, 0.0 }; bool crossed = false;
  for (int i = 0; i < 200; i++) { anim_spring_step(&s, 1.0, 400.0, 40.0, 16.0); if (s.x > 1.0 + 1e-6) crossed = true; }
  CHECK(!crossed && anim_spring_settled(s, 1.0, 1e-3));
  AnimSpring g = { 0.0, 0.0 }; bool over = false;
  for (int i = 0; i < 200; i++) { anim_spring_step(&g, 1.0, 300.0, 22.0, 16.0); if (g.x > 1.02) over = true; }
  CHECK(over && anim_spring_settled(g, 1.0, 1e-3));
  AnimSpring z = { 0.5, 0.0 }; anim_spring_step(&z, 0.5, 300.0, 22.0, 16.0); CHECK(z.x == 0.5 && z.v == 0.0);

  // Stable at the dt clamp's ceiling: at 50ms omega*dt hit 1.0 exactly, which semi-implicit Euler turns into a
  // permanent 0,1,0,1 oscillation, so anim_spring_step integrates in fixed substeps of at most ANIM_SPRING_SUBSTEP_MS.
  {
    AnimSpring c50 = { 0.0, 0.0 }; bool crossed50 = false;
    for (int i = 0; i < 200; i++) { anim_spring_step(&c50, 1.0, 400.0, 40.0, 50.0); if (c50.x > 1.0 + 1e-6) crossed50 = true; }
    CHECK(!crossed50 && anim_spring_settled(c50, 1.0, 1e-3));
    AnimSpring g50 = { 0.0, 0.0 }; bool over50 = false;
    for (int i = 0; i < 200; i++) { anim_spring_step(&g50, 1.0, 300.0, 22.0, 50.0); if (g50.x > 1.02) over50 = true; }
    CHECK(over50 && anim_spring_settled(g50, 1.0, 1e-3));
    AnimSpring one50 = { 0.0, 0.0 }, many50 = { 0.0, 0.0 };
    anim_spring_step(&one50, 1.0, 400.0, 40.0, 50.0);
    for (int i = 0; i < 13; i++) anim_spring_step(&many50, 1.0, 400.0, 40.0, 50.0 / 13.0);
    CHECK(fabs(one50.x - many50.x) < 1e-9 && fabs(one50.v - many50.v) < 1e-9);
    AnimSpring z50 = { 0.25, 0.0 };
    anim_spring_step(&z50, 0.25, 400.0, 40.0, 50.0);
    CHECK(z50.x == 0.25 && z50.v == 0.0);
    AnimSpring n50 = { 0.4, 1.5 };
    anim_spring_step(&n50, 1.0, 400.0, 40.0, 0.0);
    CHECK(n50.x == 0.4 && n50.v == 1.5);
  }

  CHECK(anim_style_clamped(-1) == ANIM_OFF && anim_style_clamped(7) == ANIM_GLASS && anim_style_clamped(1) == ANIM_SIMPLE);

  CHECK(TAB_SETTINGS == 4 && TAB_COUNT == 5);

  // Animations is a plain switch over the three-valued style: anim_switch_apply (anim.h) is covered by tb_0116; this
  // checks the row is wired to it, so a stored Glass survives a redraw with no click.
  {
    const int savedTab = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    g_cfg.menuanim = ANIM_GLASS;
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    CHECK(g_cfg.menuanim == ANIM_GLASS);
    const CanvasOp* rowLbl = canvas_record_find_text("Animations");
    CHECK(rowLbl != nullptr);
    CHECK(strlen("Fades and slides in the panel") < 63);

    // A click on the row turns a stored Glass OFF: the switch reads "on" for Glass as for Simple, so no special case.
    if (rowLbl) {
      // Swept from past the tab rail only: the row's y falls inside the rail's span, so starting at the panel edge
      // would switch tabs.
      const UiRect p = menu_panel_rect(3440, 1440);
      const int contentX = p.x + 1 + RAIL_W;
      bool flipped = false;
      for (int x = contentX; x < p.x + p.w && !flipped; x++) {
        g_ui.mx = x; g_ui.my = rowLbl->y; g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
        if (g_cfg.menuanim == ANIM_OFF) flipped = true;
      }
      CHECK(flipped);

      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(3440, 1440);
      flipped = false;
      for (int x = contentX; x < p.x + p.w && !flipped; x++) {
        g_ui.mx = x; g_ui.my = rowLbl->y; g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
        if (g_cfg.menuanim == ANIM_SIMPLE) flipped = true;
      }
      CHECK(flipped);
    }

    CHECK(streq(tooltip_for_row("Animations", 3440, 1440),
                "Fades and slides in the panel"));

    g_cfg.menuanim = savedAnim;
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }

  // Row-placement sweep: moved rows land on TAB_SETTINGS, never TAB_VIEW; View's survivors stay put; the rail carries
  // all five tab names. Run once per style, settled, so Simple/Glass exercise settled state too.
  {
    const int savedTab = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    const int savedFovOn = g_cfg.fovon, savedFov = g_cfg.fov;
    const int savedFreecamOn = g_cfg.freecamon, savedDbg = g_cfg.debuglog;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.fovon = 0; g_cfg.freecamon = 1; g_cfg.debuglog = 0;

    const int styles[3] = { ANIM_OFF, ANIM_SIMPLE, ANIM_GLASS };
    for (int si = 0; si < 3; si++) {
      g_cfg.menuanim = styles[si];
      menu_set_now_ms(1000000u);   // settled: every duration this task names is under 250 ms

      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_find_text("UI scale") != nullptr);
      CHECK(canvas_record_find_text("Animations") != nullptr);
      CHECK(canvas_record_find_text("Debug log") != nullptr);
      CHECK(canvas_record_find_text("Custom field of view") == nullptr);
      CHECK(canvas_record_find_text("Free camera") == nullptr);
      CHECK(canvas_record_find_text("Screenshot mode") == nullptr);

      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_find_text("Custom field of view") != nullptr);
      CHECK(canvas_record_find_text("Free camera") != nullptr);
      CHECK(canvas_record_find_text("Screenshot mode") != nullptr);
      CHECK(canvas_record_find_text("UI scale") == nullptr);
      CHECK(canvas_record_find_text("Debug log") == nullptr);

      CHECK(canvas_record_find_text("Visuals") != nullptr);
      CHECK(canvas_record_find_text("Combat") != nullptr);
      CHECK(canvas_record_find_text("Weapons") != nullptr);
      CHECK(canvas_record_find_text("View") != nullptr);
      CHECK(canvas_record_find_text("Settings") != nullptr);

      if (styles[si] != ANIM_OFF) {
        menu_set_now_ms(2000000u);
        menu_set_tab(TAB_SETTINGS);   // a fresh switch, so tabSwitchMs is this frame's
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(!canvas_record_overflowed());
        menu_set_now_ms(2000010u);    // 10 ms later: mid every one of this style's own transitions
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(!canvas_record_overflowed());
      }
    }

    g_cfg.menuanim = savedAnim;
    g_cfg.fovon = savedFovOn; g_cfg.fov = savedFov;
    g_cfg.freecamon = savedFreecamOn; g_cfg.debuglog = savedDbg;
    menu_set_tab(savedTab);
    menu_set_now_ms(0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }

  // The 63-character rule is swept: every FONT_SMALL text op the panel drew, on every tab (headings, rail summaries and
  // status line share the font), so a hint added later cannot escape. CanvasOp::text is char[64] with _TRUNCATE, so an
  // overlong string arrives as exactly 63 chars and fails rather than being hidden.
  {
    const int savedTab = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.menuanim = ANIM_OFF;
    menu_set_now_ms(4000000u);

    int swept = 0;
    bool allShort = true;
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      menu_set_tab(tab);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const int n = canvas_record_count();
      for (int i = 0; i < n; i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (!o || o->kind != CANVAS_OP_TEXT || o->font != FONT_SMALL) continue;
        swept++;
        if (strlen(o->text) >= 63) allShort = false;
      }
    }
    CHECK(allShort);
    // The floor stops the sweep passing vacuously: 14 group headings across five tabs plus each tab's status line and
    // "Ins closes" (5*2+14=24). Tooltips are not drawn with the cursor off panel; the rail's names are FONT_UI.
    CHECK(swept >= 24);

    g_cfg.menuanim = savedAnim;
    menu_set_tab(savedTab);
    menu_set_now_ms(0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }

  // A click mid-transition still lands on the logical row: Simple, a fresh tab switch, then 10ms later (longest
  // transition is ANIM_TAB_MS = 120ms).
  {
    const int savedTab = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    const int savedDbg = g_cfg.debuglog;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    g_cfg.menuanim = ANIM_SIMPLE;
    g_cfg.debuglog = 0;
    menu_set_now_ms(3000000u);
    menu_set_tab(TAB_VIEW);          // draw some other tab first, settled
    menu_draw(3440, 1440);
    menu_set_tab(TAB_SETTINGS);      // the switch this check is timed from
    menu_set_now_ms(3000010u);       // 10 ms later: mid-transition
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    const CanvasOp* rDbg = canvas_record_find_text("Debug log");
    CHECK(rDbg);
    if (rDbg) {
      const UiRect p = menu_panel_rect(3440, 1440);
      bool flipped = false;
      for (int x = p.x; x < p.x + p.w && !flipped; x++) {
        g_ui.mx = x; g_ui.my = rDbg->y; g_ui.down = false; g_ui.pressed = true;
        menu_set_now_ms(3000010u);
        menu_draw(3440, 1440);
        if (g_cfg.debuglog != 0) flipped = true;
      }
      CHECK(flipped);
    }

    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.menuanim = savedAnim;
    g_cfg.debuglog = savedDbg;
    menu_set_tab(savedTab);
    menu_set_now_ms(0);
  }
}

// The Glass style: three geometry helpers (anim.h: pill squash/stretch, backdrop wobble, rounded-corner tessellation),
// the recording twins, and the style sweep: no bevel, captures ground once a frame, draws every label Simple does,
// leaves Off's recorded ops untouched.
static CanvasOp s_glassOffOps[CANVAS_RECORD_MAX];
static int      s_glassOffCount = 0;
static char     s_glassText[192][64];
static int      s_glassTextCount = 0;

static int glass_count_kind(int kind) {
  int n = 0;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (o && o->kind == kind) n++;
  }
  return n;
}

void tb_0100_phase_8_task5_glass_primitives_and_geometry() {
  AnimPill p0 = anim_pill_stretch(100.0, 60.0, 0.0, 0.05, 0.4);
  CHECK(p0.w == 60.0 && p0.x == 100.0);
  AnimPill p1 = anim_pill_stretch(100.0, 60.0, 200.0, 0.05, 0.4);
  CHECK(p1.w > 60.0 && p1.w <= 60.0 * 1.4 + 1e-9);
  CHECK(fabs((p1.x + p1.w / 2.0) - (100.0 + 30.0)) < 1e-9);
  AnimPill p2 = anim_pill_stretch(100.0, 60.0, -200.0, 0.05, 0.4);
  CHECK(fabs(p2.w - p1.w) < 1e-9);
  AnimPill p3 = anim_pill_stretch(100.0, 60.0, 1e9, 0.05, 0.4);
  CHECK(fabs(p3.w - 84.0) < 1e-9);

  CHECK(anim_wobble(0.0, 4.0) == 0.0 && anim_wobble(1.0, 4.0) == 0.0);
  double peak = 0.0; for (int i = 1; i < 100; i++) { const double v = fabs(anim_wobble(i / 100.0, 4.0)); if (v > peak) peak = v; }
  CHECK(peak > 1.0 && peak <= 4.0 + 1e-9);
  CHECK(fabs(anim_wobble(0.9, 4.0)) < fabs(anim_wobble(0.1, 4.0)) || fabs(anim_wobble(0.1, 4.0)) < 1e-9);

  CHECK(anim_round_rect_segments(0) == 3 && anim_round_rect_segments(4) >= 3 && anim_round_rect_segments(200) == 12);

  // Blur's per-tap alphas sum to the tint alpha exactly: C_GLASS_TINT's alpha is how much of the scene shows through,
  // so a heavier accumulation would over-opaque the slab.
  {
    const int aList[4] = { 1, 8, 100, 255 };
    const int tList[3] = { 1, 5, 9 };
    bool sums = true, nonNeg = true, spread = true;
    for (int ai = 0; ai < 4; ai++) {
      for (int ti = 0; ti < 3; ti++) {
        int sum = 0, lo = 1000, hi = -1000;
        for (int i = 0; i < tList[ti]; i++) {
          const int a = canvas_tap_alpha(aList[ai], tList[ti], i);
          if (a < 0) nonNeg = false;
          sum += a;
          if (a < lo) lo = a;
          if (a > hi) hi = a;
        }
        if (sum != aList[ai]) sums = false;
        // No tap carries more than one more than any other: the remainder is spread, not dumped on one pass (a bright
        // ghost).
        if (hi - lo > 1) spread = false;
      }
    }
    CHECK(sums);
    CHECK(nonNeg);
    CHECK(spread);
    CHECK(canvas_tap_alpha(255, 0, 0) == 0);
    CHECK(canvas_tap_alpha(255, 9, -1) == 0 && canvas_tap_alpha(255, 9, 9) == 0);
    CHECK(canvas_tap_alpha(-5, 9, 0) == 0 && canvas_tap_alpha(9999, 1, 0) == 255);
  }

  canvas_set_transform(0, 0, 1.0, 0, 0);
  canvas_record_begin();
  canvas_round_rect(10, 20, 100, 50, 8, 0xFF112233u);
  canvas_round_rect_outline(30, 40, 120, 60, 8, 1, 0xFF445566u);
  canvas_gradient_rect(1, 2, 3, 4, 0xFFAAAAAAu, 0xFFBBBBBBu);
  canvas_glow_rect(5, 6, 7, 8, 0xFFCCCCCCu);
  CHECK(canvas_backdrop_capture(0, 0, 640, 480));
  canvas_backdrop_draw(0, 0, 640, 480, 8, 0x80FFFFFFu, 0.0, 0.0, 9, 2);
  canvas_record_end();
  CHECK(canvas_record_count() == 6);
  const CanvasOp* rr = canvas_record_find_kind(CANVAS_OP_ROUNDRECT, 0xFF112233u);
  CHECK(rr && rr->x == 10 && rr->y == 20 && rr->w == 100 && rr->h == 50);
  const CanvasOp* ro = canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, 0xFF445566u);
  CHECK(ro && ro->x == 30 && ro->y == 40 && ro->w == 120 && ro->h == 60);
  CHECK(canvas_record_find_kind(CANVAS_OP_GRADIENT, 0xFFAAAAAAu) != 0);
  CHECK(canvas_record_find_kind(CANVAS_OP_GLOW, 0xFFCCCCCCu) != 0);
  CHECK(canvas_record_find_kind(CANVAS_OP_BACKDROP, 0x80FFFFFFu) != 0);

  // The style sweep, one tab at a time. The capture has its own op kind (CANVAS_OP_CAPTURE), not CANVAS_OP_BACKDROP: a
  // Glass frame must contain exactly one backdrop draw.
  {
    const int savedTab  = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    for (int tab = 0; tab < TAB_COUNT; tab++) {
      // Off first, and its ops are the pin: nothing this adds may reach it. The tab switch is drawn (settled) outside
      // the recording.
      g_cfg.menuanim = ANIM_OFF;
      menu_set_now_ms(0);
      menu_set_tab(tab);
      menu_draw(3440, 1440);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      s_glassOffCount = canvas_record_count();
      for (int i = 0; i < s_glassOffCount; i++) s_glassOffOps[i] = *canvas_record_op(i);
      CHECK(glass_count_kind(CANVAS_OP_BEVEL) > 0);
      // A hotkey field is a round rect (a well with a rim) in every style, Off included, so it does not read as a bevel
      // in Off. One of each per bind row; every other tab draws neither.
      const int bindFields = (tab == TAB_SETTINGS) ? BIND_ROW_COUNT : 0;
      CHECK(glass_count_kind(CANVAS_OP_ROUNDRECT) == bindFields);
      CHECK(glass_count_kind(CANVAS_OP_ROUNDOUTLINE) == bindFields);
      CHECK(glass_count_kind(CANVAS_OP_GRADIENT) == 0);
      CHECK(glass_count_kind(CANVAS_OP_GLOW) == 0);
      CHECK(glass_count_kind(CANVAS_OP_CAPTURE) == 0);
      CHECK(glass_count_kind(CANVAS_OP_BACKDROP) == 0);

      // SIMPLE: every label this tab draws must survive the change of style. 50000u, not 2001u: Animations is a SWITCH,
      // so this OFF-to-SIMPLE transition stamps that row's toggleMs, and 2001u is also a GLASS instant below, which
      // would land the later "long settled" reading on this toggle's own timestamp.
      g_cfg.menuanim = ANIM_SIMPLE;
      menu_set_now_ms(50000u);
      menu_draw(3440, 1440);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      s_glassTextCount = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (!o || o->kind != CANVAS_OP_TEXT || s_glassTextCount >= 192) continue;
        strncpy_s(s_glassText[s_glassTextCount++], 64, o->text, _TRUNCATE);
      }
      CHECK(s_glassTextCount > 0);

      // Glass at three instants: 1ms (backdrop condensing over its 250ms), 300ms (inside the highlight sweep's
      // 250-650ms window), 2001ms (settled). The open clock stamps to 0 on the suite's first menu_draw.
      g_cfg.menuanim = ANIM_GLASS;
      const unsigned when[3] = { 1u, 300u, 2001u };
      for (int wi = 0; wi < 3; wi++) {
        menu_set_now_ms(when[wi]);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(!canvas_record_overflowed());
        CHECK(glass_count_kind(CANVAS_OP_CAPTURE)  == 1);
        CHECK(glass_count_kind(CANVAS_OP_BACKDROP) == 1);
        CHECK(glass_count_kind(CANVAS_OP_BEVEL)    == 0);
        CHECK(glass_count_kind(CANVAS_OP_GRADIENT) == 1);
        CHECK(glass_count_kind(CANVAS_OP_ROUNDRECT) > 0);
        // The capture must be the first op: it reads the back buffer, so anything before it would be blurred back onto
        // the slab as if it were the scene.
        const CanvasOp* firstOp = canvas_record_op(0);
        CHECK(firstOp && firstOp->kind == CANVAS_OP_CAPTURE);
        // The slab's rim is exactly one ring (a filled shape cannot be an edge on a see-through slab); hotkey fields
        // draw their own rings on top, counted separately.
        CHECK(glass_count_kind(CANVAS_OP_ROUNDOUTLINE) == 1 + bindFields);
        // The one highlight sweep, inside its window only: nothing hovered or toggled, so it is the only additive draw.
        if (when[wi] == 300u) CHECK(glass_count_kind(CANVAS_OP_GLOW) >= 1);
        else                  CHECK(glass_count_kind(CANVAS_OP_GLOW) == 0);
        bool allText = true;
        for (int i = 0; i < s_glassTextCount; i++)
          if (!canvas_record_find_text(s_glassText[i])) allText = false;
        CHECK(allText);
      }

      // Back to Off, op for op: Glass in between leaves no residue Off can see.
      g_cfg.menuanim = ANIM_OFF;
      menu_set_now_ms(0);
      menu_draw(3440, 1440);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      bool same = canvas_record_count() == s_glassOffCount;
      for (int i = 0; same && i < s_glassOffCount; i++) {
        const CanvasOp* o = canvas_record_op(i);
        same = o != nullptr && o->kind == s_glassOffOps[i].kind &&
               o->x == s_glassOffOps[i].x && o->y == s_glassOffOps[i].y &&
               o->w == s_glassOffOps[i].w && o->h == s_glassOffOps[i].h &&
               o->colour == s_glassOffOps[i].colour &&
               streq(o->text, s_glassOffOps[i].text);
      }
      CHECK(same);
    }

    g_cfg.menuanim = savedAnim;
    menu_set_tab(savedTab);
    menu_set_now_ms(0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }

  // The open stamp survives an Off-style close: overlay.cpp draws only while input_menu_open() ||
  // menu_closing_animation_active(), false under ANIM_OFF, so only close_menu()'s menu_keycap_close() learns of a real
  // close. Simulated by never drawing between the close and the style switch.
  {
    const int savedTab = menu_active_tab();
    const int savedAnim = g_cfg.menuanim;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    menu_set_tab(TAB_VISUALS);
    menu_stub_set_open(false);
    menu_draw(3440, 1440);              // a real drawn close: wasOpen -> 0
    g_cfg.menuanim = ANIM_OFF;
    menu_set_now_ms(1000u);
    menu_stub_set_open(true);
    menu_draw(3440, 1440);              // opens under Off: openStartMs stamps to 1000

    menu_stub_set_open(false);          // the close overlay.cpp never draws under Off
    menu_keycap_close();                // close_menu()'s own unconditional call

    g_cfg.menuanim = ANIM_GLASS;        // switched while the panel is shut (an ini edit)
    menu_set_now_ms(1500u);             // 500 ms later: inside the OLD stamp's sweep window
    menu_stub_set_open(true);           // reopen

    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    // No highlight sweep on the first reopened frame: openStartMs restamps to this frame's clock (age 0). The bug left
    // it at 1000u, so age (500) fell inside the sweep window.
    CHECK(glass_count_kind(CANVAS_OP_GLOW) == 0);

    g_cfg.menuanim = savedAnim;
    menu_set_tab(savedTab);
    menu_set_now_ms(0);
    menu_stub_set_open(true);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }
}

// The panel transform. One canvas-wide transform (canvas_set_transform) moves everything the panel draws while the
// logical layout every hit test reads stays put. Pins: identity is exact, a mid-open frame is the settled frame moved,
// a click still lands on the logical row. Clock steps are 16ms; 50ms is unstable for the critical pair.
static CanvasOp s_fbRef[CANVAS_RECORD_MAX];
static int      s_fbRefCount = 0;

static int fb_snapshot(CanvasOp* out) {
  const int n = canvas_record_count();
  for (int i = 0; i < n && i < CANVAS_RECORD_MAX; i++) out[i] = *canvas_record_op(i);
  return n;
}

static unsigned fb_run(unsigned t, int frames) {
  for (int i = 0; i < frames; i++) { t += 16; menu_set_now_ms(t); menu_draw(3440, 1440); }
  return t;
}

static unsigned fb_record(unsigned t) {
  t += 16;
  menu_set_now_ms(t);
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  return t;
}

static int fb_find_text(const CanvasOp* ops, int n, const char* s) {
  for (int i = 0; i < n; i++)
    if (ops[i].kind == CANVAS_OP_TEXT && streq(ops[i].text, s)) return i;
  return -1;
}

static int fb_find_kind(const CanvasOp* ops, int n, int kind) {
  for (int i = 0; i < n; i++) if (ops[i].kind == kind) return i;
  return -1;
}

void tb_0101_phase_8_fixb_panel_transform() {
  const int savedTab   = menu_active_tab();
  const int savedAnim  = g_cfg.menuanim;
  const int savedScale = g_cfg.uiscale;
  const int savedDbg   = g_cfg.debuglog;
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_cfg.uiscale = 100;          // logical and device agree, so an offset reads directly
  unsigned t = 400000u;

  // The closing animation is reachable: menu_stubs.cpp's input_menu_open() is settable, so
  // menu_closing_animation_active()'s `!input_menu_open()` term can hold and the close path is exercised, not just
  // read.
  {
    g_cfg.menuanim = ANIM_SIMPLE;
    menu_stub_set_open(true);
    menu_set_tab(TAB_VISUALS);
    t = fb_run(t, 60);                                  // open, settled
    CHECK(!menu_closing_animation_active());
    menu_stub_set_open(false);
    CHECK(menu_closing_animation_active());
    t = fb_run(t, 8);                                   // 128 ms into the fade
    CHECK(menu_closing_animation_active());
    t = fb_run(t, 100);
    CHECK(!menu_closing_animation_active());

    // ANIM_OFF has no close to animate: the panel is simply gone.
    g_cfg.menuanim = ANIM_OFF;
    menu_stub_set_open(true);
    t = fb_run(t, 3);
    menu_stub_set_open(false);
    CHECK(!menu_closing_animation_active());
    t = fb_run(t, 1);
    CHECK(!menu_closing_animation_active());
    menu_stub_set_open(true);
    t = fb_run(t, 3);

    // A spring nothing has stepped for a second is at rest: menu_draw alone advances it, and overlay_frame can return
    // early (non-GL rendmode, zero-sized frame), leaving a half-faded spring that would reopen the GL bracket for a
    // fade nobody saw.
    g_cfg.menuanim = ANIM_SIMPLE;
    menu_stub_set_open(true);
    t = fb_run(t, 60);                                  // open, settled
    menu_stub_set_open(false);
    t = fb_run(t, 1);                                   // one frame into the fade
    CHECK(menu_closing_animation_active());
    t += ANIM_STALE_MS + 1;
    menu_set_now_ms(t);                                 // a stretch with NOTHING drawn
    CHECK(!menu_closing_animation_active());
    menu_stub_set_open(true);
    t = fb_run(t, 3);
  }

  // The WHOLE panel slides, and the hit test does not.
  {
    g_cfg.menuanim = ANIM_SIMPLE;
    menu_set_tab(TAB_SETTINGS);
    t = fb_run(t, 80);                                  // open and settled under Simple
    t = fb_record(t);
    CHECK(!canvas_record_overflowed());
    s_fbRefCount = fb_snapshot(s_fbRef);
    CHECK(s_fbRefCount > 0);
    {
      const CanvasTransform st = canvas_record_transform();
      CHECK(st.dx == 0 && st.dy == 0 && st.scale == 1.0);
    }

    // Mid-open: close, let the spring rest, re-open and take ONE 16 ms step.
    menu_stub_set_open(false);
    t = fb_run(t, 140);
    menu_stub_set_open(true);
    t = fb_record(t);
    CHECK(!canvas_record_overflowed());
    const CanvasTransform mid = canvas_record_transform();
    CHECK(mid.dx == 0 && mid.scale == 1.0);
    CHECK(mid.dy < 0);
    CHECK(canvas_record_count() == s_fbRefCount);

    // Every recorded op moved by exactly that offset, not the border alone.
    bool moved = true;
    for (int i = 0; i < s_fbRefCount; i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o || o->kind != s_fbRef[i].kind || o->x != s_fbRef[i].x ||
          o->w != s_fbRef[i].w || o->h != s_fbRef[i].h ||
          o->y != s_fbRef[i].y + mid.dy) moved = false;
    }
    CHECK(moved);
    CHECK(fb_find_kind(s_fbRef, s_fbRefCount, CANVAS_OP_BEVEL) >= 0);
    CHECK(fb_find_text(s_fbRef, s_fbRefCount, "Visuals") >= 0);
    CHECK(fb_find_text(s_fbRef, s_fbRefCount, "Debug log") >= 0);

    // A click at the row's LOGICAL position still toggles it mid-slide: the drawn y moved, the hit test did not.
    const CanvasOp* rDbg = canvas_record_find_text("Debug log");
    CHECK(rDbg);
    if (rDbg) {
      const int logicalY = rDbg->y - mid.dy;
      CHECK(logicalY != rDbg->y);
      const int before = g_cfg.debuglog;
      const UiRect pr = menu_panel_rect(3440, 1440);
      bool flipped = false;
      for (int x = pr.x; x < pr.x + pr.w && !flipped; x++) {
        g_ui.mx = x; g_ui.my = logicalY; g_ui.down = false; g_ui.pressed = true;
        menu_set_now_ms(t);                  // the same instant: the slide stays put
        menu_draw(3440, 1440);
        if (g_cfg.debuglog != before) flipped = true;
      }
      CHECK(flipped);
      g_ui.mx = -1; g_ui.my = -1; g_ui.pressed = false;
      g_cfg.debuglog = before;
    }
  }

  // Simple, settled, draws exactly what Off draws. Not a stored golden (the suite carries none): the property is that
  // Simple SETTLES to the stock drawing, a comparison between two recordings taken in the same run.
  {
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_stub_set_open(true);
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      g_cfg.menuanim = ANIM_OFF;
      menu_set_tab(tab);
      t = fb_run(t, 4);                     // Off snaps the open spring to exactly 1
      t = fb_record(t);
      CHECK(!canvas_record_overflowed());
      s_fbRefCount = fb_snapshot(s_fbRef);

      g_cfg.menuanim = ANIM_SIMPLE;
      // A tab switch snaps every row spring to its logical value (exact), then far past ANIM_TAB_MS for the crossfade.
      menu_set_tab(tab == 0 ? TAB_VIEW : TAB_VISUALS);
      t = fb_run(t, 2);
      menu_set_tab(tab);
      t = fb_run(t, 40);
      t = fb_record(t);
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_count() == s_fbRefCount);

      // Op for op, except where the panel draws the style itself: on the Settings tab the Animations switch's box (a
      // bevel, fill colour on/off) and knob (a rect that moves AND changes colour) cannot match between Off and Simple.
      // Everything else must.
      bool same = true;
      int loose = 0;
      for (int i = 0; same && i < s_fbRefCount; i++) {
        const CanvasOp* o = canvas_record_op(i);
        const CanvasOp& r = s_fbRef[i];
        if (!o || o->kind != r.kind) { same = false; break; }
        if (tab == TAB_SETTINGS && o->kind == CANVAS_OP_BEVEL &&
            o->x == r.x && o->y == r.y && o->w == r.w && o->h == r.h &&
            o->colour != r.colour) { loose++; continue; }
        if (tab == TAB_SETTINGS && o->kind == CANVAS_OP_RECT &&
            o->y == r.y && o->h == r.h && o->w == r.w &&
            (o->x != r.x || o->colour != r.colour)) { loose++; continue; }
        if (o->x != r.x || o->y != r.y || o->w != r.w || o->h != r.h ||
            o->colour != r.colour || !streq(o->text, r.text)) same = false;
      }
      CHECK(same);
      // The allowance is exactly those two ops, on the Settings tab only.
      CHECK(loose == (tab == TAB_SETTINGS ? 2 : 0));
    }
  }

  // Under Glass the WHOLE panel condenses about its own centre.
  {
    g_cfg.menuanim = ANIM_GLASS;
    menu_stub_set_open(true);
    menu_set_tab(TAB_SETTINGS);
    t = fb_run(t, 80);                      // open, condensed, past the sweep's window
    t = fb_record(t);
    CHECK(!canvas_record_overflowed());
    s_fbRefCount = fb_snapshot(s_fbRef);
    {
      // A settled Glass frame is the identity too: slab at full size, panel where the logical layout says.
      const CanvasTransform st = canvas_record_transform();
      CHECK(st.scale == 1.0 && st.dx == 0 && st.dy == 0);
    }
    t = fb_record(t);
    {
      bool stable = canvas_record_count() == s_fbRefCount;
      for (int i = 0; stable && i < s_fbRefCount; i++) {
        const CanvasOp* o = canvas_record_op(i);
        stable = o && o->kind == s_fbRef[i].kind && o->x == s_fbRef[i].x &&
                 o->y == s_fbRef[i].y && o->w == s_fbRef[i].w && o->h == s_fbRef[i].h;
      }
      CHECK(stable);
    }

    // Mid-open, one 16 ms step after the panel re-opens.
    menu_stub_set_open(false);
    t = fb_run(t, 140);
    menu_stub_set_open(true);
    t = fb_record(t);
    CHECK(!canvas_record_overflowed());
    const CanvasTransform mid = canvas_record_transform();
    CHECK(mid.dx == 0 && mid.dy == 0);
    CHECK(mid.scale > 0.9 && mid.scale < 1.0);
    CHECK(canvas_record_count() == s_fbRefCount);

    // Every op is the settled op scaled about the same centre by the same factor, within a pixel (each coordinate
    // rounds).
    bool scaled = true;
    for (int i = 0; i < s_fbRefCount; i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o || o->kind != s_fbRef[i].kind) { scaled = false; break; }
      const double ex = ((double)s_fbRef[i].x - mid.cx) * mid.scale + mid.cx;
      const double ey = ((double)s_fbRef[i].y - mid.cy) * mid.scale + mid.cy;
      if (fabs((double)o->x - ex) > 1.0 || fabs((double)o->y - ey) > 1.0) scaled = false;
      if (fabs((double)o->w - (double)s_fbRef[i].w * mid.scale) > 1.0) scaled = false;
      if (fabs((double)o->h - (double)s_fbRef[i].h * mid.scale) > 1.0) scaled = false;
    }
    CHECK(scaled);

    // The three named ops must show ACTUAL movement, not a prediction an identity transform would also satisfy near the
    // centre. The slab is its rim (CANVAS_OP_ROUNDOUTLINE); the FIRST outline is drawn before any content, so the
    // Settings tab's hotkey rings cannot be found instead.
    const int slabI = fb_find_kind(s_fbRef, s_fbRefCount, CANVAS_OP_ROUNDOUTLINE);
    const int railI = fb_find_text(s_fbRef, s_fbRefCount, "Visuals");
    const int rowI  = fb_find_text(s_fbRef, s_fbRefCount, "Debug log");
    CHECK(slabI >= 0 && railI >= 0 && rowI >= 0);
    if (slabI >= 0 && railI >= 0 && rowI >= 0) {
      const int idx[3] = { slabI, railI, rowI };
      bool allMoved = true;
      for (int k = 0; k < 3; k++) {
        const CanvasOp* o = canvas_record_op(idx[k]);
        if (!o) { allMoved = false; continue; }
        if (abs(o->x - s_fbRef[idx[k]].x) <= 1 && abs(o->y - s_fbRef[idx[k]].y) <= 1)
          allMoved = false;
      }
      CHECK(allMoved);
    }
  }

  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_cfg.menuanim = savedAnim;
  g_cfg.uiscale = savedScale;
  g_cfg.debuglog = savedDbg;
  menu_stub_set_open(true);
  menu_set_tab(savedTab);
  menu_set_now_ms(0);
  // Canvas left as found: menu_draw leaves the transform standing and this block's last draw was a mid-open Glass
  // frame, so clear it rather than trap a later block that calls a primitive directly.
  canvas_set_transform(0, 0, 1.0, 0, 0);
}
