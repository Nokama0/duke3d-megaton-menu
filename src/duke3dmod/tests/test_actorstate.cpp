#include "test_common.h"
#include "../actorstate.h"
#include <math.h>
#include <string.h>

// Actor-state instrument's pure core. actorstate.cpp is DLL-only (walks live sprite memory); what it decides lives in
// actorstate.h and is pinned here, the target.h/tbot.h split.

// One trooper's key, so each check varies exactly one field. Picnum 1680 is an Assault Trooper's own.
static ActorKey trooper_key() {
  ActorKey k = { 30, 0x0101, 1, 0x0BADF00D, 40, 0, 1680 };
  return k;
}

void tb_0110_actorstate_core() {
  // Must match target.h's SPR_CSTAT_SHOOT_MASK, or an actor merely flipped would spend a line each turn.
  CHECK(ACTORSTATE_CSTAT_MASK == 0x8101);
  CHECK(ACTORSTATE_CSTAT_MASK == SPR_CSTAT_SHOOT_MASK);
  CHECK(ACTORSTATE_MAX_TRACKED == 64 && ACTORSTATE_MAX_LINES == 400);

  {
    const ActorKey a = trooper_key();
    ActorKey b = trooper_key();
    CHECK(!actorstate_changed(a, b));
    CHECK(!actorstate_changed(b, a));

    b = trooper_key(); b.extra = 29;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    b = trooper_key(); b.cstatBits = 0x0001;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    b = trooper_key(); b.statnum = 2;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    b = trooper_key(); b.t4 = 0x0BADF00E;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    b = trooper_key(); b.xrepeat = 41;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    b = trooper_key(); b.pal = 21;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
    // A slot reused inside one tic by a different actor type, every other field equal, must still register: picnum
    // alone suffices.
    b = trooper_key(); b.picnum = 1681;
    CHECK(actorstate_changed(a, b) && actorstate_changed(b, a));
  }

  {
    ActorTrack t[ACTORSTATE_MAX_TRACKED];
    memset(t, 0, sizeof(t));

    const int s0 = actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 700);
    CHECK(s0 == 0);
    CHECK(t[0].used && t[0].index == 700 && !t[0].seen);
    t[0].seen = true;
    CHECK(actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 700) == 0);
    CHECK(t[0].seen);           // existing slot's `seen` left alone
    CHECK(actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 701) == 1);
    CHECK(t[1].used && t[1].index == 701 && !t[1].seen);

    // A hole is reused rather than skipped.
    t[0].used = false;
    CHECK(actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 702) == 0);
    CHECK(t[0].index == 702 && !t[0].seen);

    for (int k = 0; k < ACTORSTATE_MAX_TRACKED; k++) {
      t[k].used = true; t[k].index = 1000 + k; t[k].seen = true;
    }
    CHECK(actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 9999) == -1);
    CHECK(t[0].index == 1000 && t[ACTORSTATE_MAX_TRACKED - 1].index ==
          1000 + ACTORSTATE_MAX_TRACKED - 1);
    CHECK(actorstate_slot_for(t, ACTORSTATE_MAX_TRACKED, 1042) == 42);

    CHECK(actorstate_slot_for(0, ACTORSTATE_MAX_TRACKED, 700) == -1);
    CHECK(actorstate_slot_for(t, 0, 700) == -1);
  }

  CHECK(actorstate_budget_ok(0, ACTORSTATE_MAX_LINES));
  CHECK(actorstate_budget_ok(ACTORSTATE_MAX_LINES - 1, ACTORSTATE_MAX_LINES));
  CHECK(!actorstate_budget_ok(ACTORSTATE_MAX_LINES, ACTORSTATE_MAX_LINES));
  CHECK(!actorstate_budget_ok(ACTORSTATE_MAX_LINES + 1, ACTORSTATE_MAX_LINES));

  // A level change zeroes the counter; anything else leaves it alone.
  CHECK(budget_after_level_change(true, 0) == 0);
  CHECK(budget_after_level_change(true, 1) == 0);
  CHECK(budget_after_level_change(true, ACTORSTATE_MAX_LINES) == 0);
  CHECK(budget_after_level_change(false, 0) == 0);
  CHECK(budget_after_level_change(false, 1) == 1);
  CHECK(budget_after_level_change(false, ACTORSTATE_MAX_LINES) == ACTORSTATE_MAX_LINES);

}

// The shot ray decomposed: shot_lateral, shot_forward, shot_vertical.
void tb_0111_shot_geometry() {
  short sintable[2048];
  for (int i = 0; i < 2048; i++)
    sintable[i] = (short)floor(16384.0 * sin(i * 3.14159265358979323846 / 1024.0) + 0.5);
  CHECK(sintable[0] == 0 && sintable[512] == 16384);

  // ang 0 faces +x. A point at (+100, +50) is 100 units along the ray and 50 to the shooter's RIGHT (Build's y points
  // down the map), which this formula reports as -50: positive is LEFT.
  CHECK(shot_forward(0, 0, 0, 100, 50, sintable) == 100);
  CHECK(shot_lateral(0, 0, 0, 100, 50, sintable) == -50);
  CHECK(shot_lateral(0, 0, 0, 100, -50, sintable) == 50);
  CHECK(shot_lateral(0, 0, 0, 100, 0, sintable) == 0);
  CHECK(shot_forward(0, 0, 0, -100, 0, sintable) == -100);

  CHECK(shot_forward(0, 0, 512, 50, 100, sintable) == 100);
  CHECK(shot_lateral(0, 0, 512, 50, 100, sintable) == 50);

  CHECK(shot_forward(1000, 2000, 0, 1100, 2050, sintable) == 100);
  CHECK(shot_lateral(1000, 2000, 0, 1100, 2050, sintable) == -50);

  CHECK(shot_forward(0, 0, 0, 100, 50, 0) == 0);
  CHECK(shot_lateral(0, 0, 0, 100, 50, 0) == 0);

  // `ang` is a live read, so it is wrapped rather than trusted: 2048 is ang 0 and -1536 is ang 512.
  CHECK(shot_forward(0, 0, 2048, 100, 50, sintable) == 100);
  CHECK(shot_lateral(0, 0, 2048, 100, 50, sintable) == -50);
  CHECK(shot_forward(0, 0, -1536, 50, 100, sintable) == 100);

  // the vertical: a level ray (vz 0) passes the target by exactly the z difference.
  CHECK(shot_vertical(0, 0, 100, -2000) == -2000);
  CHECK(shot_vertical(500, 0, 100, -2000) == -2500);
  // vz 16384 is one z unit of drop per xy unit of travel (the table's scale), so at fwd 100 the ray has fallen 100 z
  // units.
  CHECK(shot_vertical(0, 16384, 100, -2000) == -2100);
  CHECK(shot_vertical(0, 16384, 100, 0) == -100);
  CHECK(shot_vertical(0, -16384, 100, 0) == 100);
  CHECK(shot_vertical(700, 32768, 0, 700) == 0);

  // Saturation rather than a wrapped coordinate, as every arithmetic helper in target.h: nothing may hand the log a
  // rolled int.
  CHECK(shot_forward(-2000000000, 0, 0, 2000000000, 0, sintable) == 2147483647);
  CHECK(shot_lateral(2000000000, 0, 512, -2000000000, 0, sintable) == -2147483647 - 1);
  CHECK(shot_vertical(-2000000000, -16384, 2000000000, 2000000000) == 2147483647);
  CHECK(shot_vertical(2000000000, 16384, 2000000000, -2000000000) == -2147483647 - 1);
}

// The engine's own hitscan half-width, shot_hitscan_halfw.
void tb_0112_shot_hitscan_halfw() {
  // A worked example: an Assault Trooper-sized tile (44 wide) at repeat 40.
  CHECK(shot_hitscan_halfw(44, 40) == 155);
  CHECK(shot_hitscan_halfw(0, 40) == 0);
  CHECK(shot_hitscan_halfw(44, 0) == 0);
  CHECK(shot_hitscan_halfw(0, 0) == 0);
  CHECK(shot_hitscan_halfw(128, 255) == (int)((long long)128 * 255 * 1000 / 11314));
  CHECK(shot_hitscan_halfw(2000000000, 2000000000) == 2147483647);
  CHECK(shot_hitscan_halfw(-2000000000, 2000000000) == -2147483647 - 1);
}

// The enemy state, record predicates and the per-tic cache: the state function on every row of a recorded log.
static ActorAction rec(int sf, int nf, int vt, int iv, int dl) {
  ActorAction a = {}; a.startframe = sf; a.numframes = nf; a.viewtype = vt; a.incval = iv; a.delay = dl;
  a.readable = true; a.plausible = (nf > 0); return a;
}
void tb_0113_enemy_states() {
  const int H = AS_CSTAT_HITTABLE, B = 0x0001, I = AS_CSTAT_INVISIBLE;
  // Trooper alive at 30, at exactly 0, walking (the engine's own alive test is extra < 0).
  CHECK(actor_state(1, true, 40, B | H, 30, false, false, false) == AS_ALIVE);
  CHECK(actor_state(1, true, 40, B | H, 0,  false, false, false) == AS_ALIVE);
  CHECK(actor_state(2, true, 40, B | H, 30, false, false, false) == AS_ALIVE);       // asleep on list 2
  // Dying: negative, still hittable (the shot-absorbing window); dead: negative and not hittable.
  CHECK(actor_state(1, true, 40, B | H, -6,  false, false, false) == AS_DYING);
  CHECK(actor_state(1, true, 40, 0,     -90, false, false, false) == AS_DEAD);
  CHECK(actor_state(1, true, 40, 0x0004, -1, false, false, false) == AS_DEAD);       // a corpse flickering to -1
  CHECK(actor_state(1, true, 40, 0,      0, false, false, false) == AS_DEAD);
  CHECK(actor_state(2, true, 40, 0x0004, 0, false, false, false) == AS_DEAD);
  CHECK(actor_state(1, true, 40, B | H, 0, true, false, false) == AS_CHOKING);
  CHECK(actor_state(1, true, 40, B | H, 5, true, false, false) == AS_CHOKING);       // the flag decides, not the number
  // Assault Commander: its death record (30,8,1,1,12) runs with extra flickering between negative and 0 while still
  // hittable, so the record decides.
  CHECK(actor_state(1, true, 40, B | H, 0,  false, false, true) == AS_DYING);
  CHECK(actor_state(1, true, 40, B | H, -8, false, false, true) == AS_DYING);
  CHECK(actor_state(1, true, 40, B | H, 0,  true,  false, true) == AS_DYING);     // dying wins over choking
  CHECK(actor_state(1, true, 40, 0,     0,  false, false, true) == AS_DEAD);      // unhittable: the usual corpse rule
  { ActorAction a = rec(30, 8, 1, 1, 12); CHECK(actor_record_is_commander_dying(a)); }
  { ActorAction a = rec(30, 8, 1, 1, 13); CHECK(!actor_record_is_commander_dying(a)); }
  CHECK(ACTOR_PICNUM_COMMANDER == 1920);
  CHECK(actor_state(1, true, 40, 0, 0, false, true, false) == AS_PLAYDEAD);
  CHECK(actor_state(1, true, 40, 0, -1, false, true, false) == AS_DEAD);             // negative wins over the record
  // Cloaked: above 0, not hittable, with or without the invisible bit.
  CHECK(actor_state(1, true, 40, I, 30, false, false, false) == AS_CLOAKED);
  CHECK(actor_state(1, true, 40, I, 0,  false, false, false) == AS_DEAD);            // at 0 without hittable: a corpse
  CHECK(actor_state(1, true, 40, 0, 30, false, false, false) == AS_CLOAKED);         // unhittable above 0 without the bit: withdrawn from play, still shown (ruling P16-D2)
  // The patrol vehicle: no record ever; at 0 alive, at -13 dying.
  CHECK(actor_state(1, true, 48, B | H, 0,   false, false, false) == AS_ALIVE);
  CHECK(actor_state(1, true, 48, B | H, -13, false, false, false) == AS_DYING);
  CHECK(actor_state(1, false, 40, B | H, 30, false, false, false) == AS_NONE);
  CHECK(actor_state(0, true, 40, B | H, 30, false, false, false) == AS_NONE);
  CHECK(actor_state(1, true, 0, B | H, 30, false, false, false) == AS_NONE);
  // Promise, exhaustively: every hittable badguy on a list with extra >= 0 is shown.
  for (int extra = 0; extra <= 200; extra += 7)
    for (int c = 0; c < 4; c++)
      for (int k = 0; k < 4; k++) {
        const int cstat = H | (c & 1) | ((c & 2) ? I : 0);
        const ActorState s = actor_state(1 + (k & 1), true, 40, cstat, extra, (k & 2) != 0, false, false);
        CHECK(actor_state_shown(s));
        CHECK(actor_state_targetable(s));
      }
  const ActorAction dying = rec(50, 5, 1, 1, 16), dead = rec(54, 0, 0, 0, 0), playdead = rec(54, 0, 0, 0, 0);
  const ActorAction sufferdead = rec(58, 2, 1, -4, 24), suffering = rec(59, 2, 1, 1, 21);
  ActorAction unreadable = {};
  CHECK(actor_record_is_suffering(suffering) && !actor_record_is_suffering(dying) && !actor_record_is_suffering(unreadable));
  CHECK( actor_record_is_playdead(playdead, dead, sufferdead));   // PLAYDEAD sits between DEAD and SUFFERDEAD
  CHECK(!actor_record_is_playdead(dead, dying, playdead));        // DEAD's predecessor is DYING
  CHECK(!actor_record_is_playdead(playdead, unreadable, sufferdead));
  CHECK(!actor_record_is_playdead(playdead, dead, unreadable));
  CHECK(!actor_record_is_playdead(suffering, dead, sufferdead));
  CHECK(action_record_is(dying, 50, 5, 1, 1, 16) && !action_record_is(dying, 50, 5, 1, 1, 17) && !action_record_is(unreadable, 0, 0, 0, 0, 0));
  // Consumer table: one row per state, swept so a state added without a row is a compile-visible omission. `shown` is
  // what esp.cpp's enemy walk filters on; `targetable` is what the aimbot, silent aim, the triggerbot's crosshair test
  // and the damage numbers ask; `label` is the ESP's string in the health slot (null = number and bar).
  {
    struct ConsumerRow { ActorState state; bool shown; bool targetable; const char* label; };
    const ConsumerRow rows[] = {
      { AS_ALIVE,    true,  true,  nullptr        },
      { AS_CHOKING,  true,  true,  "Choking"      },
      { AS_PLAYDEAD, true,  false, "Playing dead" },
      { AS_CLOAKED,  true,  false, "Cloaked"      },
      { AS_DYING,    false, false, nullptr        },
      { AS_DEAD,     false, false, nullptr        },
      { AS_NONE,     false, false, nullptr        },
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    // Every enum value has a row, in the enum's own order, so a state added anywhere but the end is caught too.
    CHECK(n == (int)AS_NONE + 1);
    bool allShown = true, allTargetable = true, allLabels = true, orderOk = true;
    for (int r = 0; r < n; r++) {
      if ((int)rows[r].state != r) orderOk = false;
      if (actor_state_shown(rows[r].state) != rows[r].shown) allShown = false;
      if (actor_state_targetable(rows[r].state) != rows[r].targetable) allTargetable = false;
      const char* got = actor_state_label(rows[r].state);
      if (rows[r].label == nullptr) { if (got != nullptr) allLabels = false; }
      else if (got == nullptr || !streq(got, rows[r].label)) allLabels = false;
      if (rows[r].targetable && !rows[r].shown) allShown = false;
    }
    CHECK(orderOk);
    CHECK(allShown);
    CHECK(allTargetable);
    CHECK(allLabels);
  }
  // aim_candidate_ok's sixth term, `targetable` (aim.cpp): five terms true with the state term false must still reject.
  CHECK( aim_candidate_ok(true, true, true, true, true, true));
  CHECK(!aim_candidate_ok(true, true, true, true, true, false));
  // The three labels draw through the same numBuf as "Dying" (esp.cpp, char numBuf[32]): "fits the slot" means no
  // truncation in the real buffer, terminator counted.
  CHECK(strlen(actor_state_label(AS_CHOKING))  < 32);
  CHECK(strlen(actor_state_label(AS_PLAYDEAD)) < 32);
  CHECK(strlen(actor_state_label(AS_CLOAKED))  < 32);
  CHECK(streq(actor_state_name(AS_ALIVE), "alive") && streq(actor_state_name(AS_PLAYDEAD), "playdead") && streq(actor_state_name(AS_NONE), "none"));
  // Cache: one generation, get after put, cleared on a new generation, bounds.
  static ActorStateCache c;
  actor_cache_begin(&c, 1);
  ActorState out = AS_NONE;
  CHECK(!actor_cache_get(&c, 5, &out));
  actor_cache_put(&c, 5, AS_CHOKING);
  CHECK(actor_cache_get(&c, 5, &out) && out == AS_CHOKING);
  CHECK(!actor_cache_get(&c, 6, &out));
  actor_cache_begin(&c, 1);                                   // the same generation keeps it
  CHECK(actor_cache_get(&c, 5, &out) && out == AS_CHOKING);
  actor_cache_begin(&c, 2);                                   // a new one clears it
  CHECK(!actor_cache_get(&c, 5, &out));
  CHECK(!actor_cache_get(&c, -1, &out) && !actor_cache_get(&c, rva::MAX_SPRITES, &out));
  actor_cache_put(&c, -1, AS_ALIVE); actor_cache_put(&c, rva::MAX_SPRITES, AS_ALIVE);   // refused, never written
  CHECK(!actor_cache_get(nullptr, 5, &out));
}
