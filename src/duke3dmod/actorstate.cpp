#include <windows.h>
#include "actorstate.h"
#include "engine.h"
#include "trigger.h"
#include "esp_names.h"
#include "wallclock.h"
#include "config.h"
#include "log.h"
#include "aim.h"
#include "dmgnum.h"

// One tic's actor-state scan. Decides nothing (actorstate.h does); walks live sprite memory, writes nothing, every line
// behind g_cfg.debuglog. Never nested in another walk (the cycle counter is single-slot), so it runs from the tail of
// hooked_processinput after the other tic-clock walks.

static ActorTrack s_track[ACTORSTATE_MAX_TRACKED];
static int s_lines = 0;

// The world the tracked set belongs to, dropped when replaced (the same triple as aim.cpp's lost-enemy instrument), or a
// new level's first tic would report changes that never happened.
static int s_prevPlayerSprite = -1;
static int s_prevNumsectors   = -1;
static unsigned s_prevMs      = 0;
static bool s_haveWorld       = false;

// esp_enemy_name returns null for an unnamed picnum; "?" keeps the log column aligned.
static const char* actor_name(int picnum) {
  const char* n = esp_enemy_name(picnum);
  return n ? n : "?";
}

static ActorStateCache s_stateCache = { 0, {0}, {0} };

void actor_state_generation_begin(unsigned gen) { actor_cache_begin(&s_stateCache, gen); }

// Engine fields for sprite i, record questions only when a rule can use them, one entry per generation. Records read
// through engine_action_read_guarded's SEH frame, fail-closed.
ActorState actor_state_cached(int i) {
  ActorState s = AS_NONE;
  if (actor_cache_get(&s_stateCache, i, &s)) return s;
  if (!engine_verified() || i < 0 || i >= rva::MAX_SPRITES) return AS_NONE;
  const int  statnum = engine_sprite_field_s(i, SPR_STATNUM);
  const bool badguy  = engine_is_badguy(i);
  const int  xrepeat = engine_sprite_field_u8(i, SPR_XREPEAT);
  const int  cstat   = engine_sprite_field_s(i, SPR_CSTAT) & 0xFFFF;
  const int  extra   = engine_sprite_field_s(i, SPR_EXTRA);
  bool choking = false, playDead = false;
  bool dyingRecord = false;
  const bool hittable = (cstat & AS_CSTAT_HITTABLE) != 0;
  // Record asked for only where a rule can act on it: a hittable enemy at or above zero (choking?) and an unhittable one at
  // zero (playing dead?). Known gap: an unhittable sprite above zero reads AS_CLOAKED where the record would say
  // AS_PLAYDEAD; every consumer treats them alike.
  if (extra >= 0 && (hittable || extra == 0)) {
    int t4 = 0;
    if (engine_actor_temp_data(i, 4, &t4) && t4 != 0) {
      ActorAction self = {};
      engine_action_read_guarded((unsigned)t4, &self);
      if (hittable) {
        choking = actor_record_is_suffering(self);
        dyingRecord = engine_sprite_field_s(i, SPR_PICNUM) == ACTOR_PICNUM_COMMANDER
                   && actor_record_is_commander_dying(self);
      } else if (action_record_is(self, 54, 0, 0, 0, 0)) {
        // Only a "down" record can be play-dead, so the neighbour reads (20 bytes either side) wait.
        ActorAction before = {}, after = {};
        engine_action_read_guarded((unsigned)t4 - 20u, &before);
        engine_action_read_guarded((unsigned)t4 + 20u, &after);
        playDead = actor_record_is_playdead(self, before, after);
      }
    }
  }
  s = actor_state(statnum, badguy, xrepeat, cstat, extra, choking, playDead, dyingRecord);
  actor_cache_put(&s_stateCache, i, s);
  return s;
}

// One "gone" line for a slot the walk no longer reaches, then freed. Prints the key's cstat (mask bits) and last state.
static void actorstate_release(int k) {
  if (actorstate_budget_ok(s_lines, ACTORSTATE_MAX_LINES)) {
    s_lines++;
    LOGI("actor: i=%d pic=%d %s gone (last extra=%d cstat=0x%04X stat=%d) ms=%u",
         s_track[k].index, s_track[k].picnum, actor_name(s_track[k].picnum),
         s_track[k].key.extra, (unsigned)(s_track[k].key.cstatBits & 0xFFFF),
         s_track[k].key.statnum, wallclock_ms());
  }
  s_track[k].used = false;
  s_track[k].seen = false;
}

void actorstate_tic() {
  // The generation turn is at hooked_processinput's head; this walk only reads the cache. With the log off it costs one test.
  if (g_cfg.debuglog == 0 || !engine_verified()) return;
  const PlayerAim p = engine_player_aim();
  if (!p.valid) return;

  // World check first: a new map frees every slot through the "gone" line rather than rebinding an old key to a new actor.
  const unsigned nowMs = (unsigned)GetTickCount();
  const int nsec = engine_numsectors();
  if (s_haveWorld &&
      aim_presence_reset(s_prevPlayerSprite, s_prevNumsectors,
                         p.spriteIndex, nsec, nowMs, s_prevMs,
                         AIM_PRESENCE_MAX_GAP_MS)) {
    for (int k = 0; k < ACTORSTATE_MAX_TRACKED; k++)
      if (s_track[k].used) actorstate_release(k);

    // The three session budgets are per-process file-statics; a level change resets all three.
    s_lines = budget_after_level_change(true, s_lines);
    aim_shot_budget_reset();
    dmgnum_hit_budget_reset();
    LOGI("instruments: budgets reset (level change)");
  }
  s_prevPlayerSprite = p.spriteIndex;
  s_prevNumsectors = nsec;
  s_prevMs = nowMs;
  s_haveWorld = true;

  for (int k = 0; k < ACTORSTATE_MAX_TRACKED; k++)
    if (s_track[k].used) s_track[k].seen = false;

  // Two sequential walks, never nested (shared cycle counter).
  const int stats[2] = { STAT_ACTOR, STAT_ZOMBIEACTOR };
  for (int s = 0; s < 2; s++) {
    for (int i = engine_first_sprite_of_stat(stats[s]); i >= 0; i = engine_next_sprite(i)) {
      if (i >= rva::MAX_SPRITES) continue;
      if (!engine_is_badguy(i)) continue;

      const int x = engine_sprite_field_l(i, SPR_X);
      const int y = engine_sprite_field_l(i, SPR_Y);
      if (!aim_within_range(trigger_world_dist2d(p.eyex, p.eyey, x, y), g_cfg.aimrange))
        continue;

      // Seven key fields. t4 (CON action pointer) is 0 when unreadable; "cannot tell" and "no action" share it.
      int t4 = 0;
      if (!engine_actor_temp_data(i, 4, &t4)) t4 = 0;
      const int cstat = engine_sprite_field_s(i, SPR_CSTAT) & 0xFFFF;
      // Read with the other key fields so a slot reused in one tic by a different actor type is caught.
      const int picnum = engine_sprite_field_s(i, SPR_PICNUM);
      ActorKey key;
      key.extra     = engine_sprite_field_s(i, SPR_EXTRA);
      key.cstatBits = cstat & ACTORSTATE_CSTAT_MASK;
      key.statnum   = engine_sprite_field_s(i, SPR_STATNUM);
      key.t4        = t4;
      key.xrepeat   = engine_sprite_field_u8(i, SPR_XREPEAT);
      key.pal       = engine_sprite_field_u8(i, SPR_PAL);
      key.picnum    = picnum;

      // Asked before the slot is claimed: actorstate_slot_for allocates on a miss, and a first sighting must log.
      bool fresh = true;
      for (int k = 0; k < ACTORSTATE_MAX_TRACKED; k++)
        if (s_track[k].used && s_track[k].index == i) { fresh = false; break; }

      const int slot = actorstate_slot_for(s_track, ACTORSTATE_MAX_TRACKED, i);
      if (slot < 0) continue;
      const bool changed = fresh || actorstate_changed(s_track[slot].key, key);

      // The remaining reads happen only for a line actually written: the CON action query is a VirtualQuery plus an SEH frame.
      if (changed && actorstate_budget_ok(s_lines, ACTORSTATE_MAX_LINES)) {
        s_lines++;
        int disp = 0;
        const bool haveDisp = engine_actor_dispicnum(i, &disp);
        ActorAction act = {};
        const bool haveAct = engine_actor_action(i, &act);
        const bool actionOk = haveAct && act.readable && act.plausible;
        // `hittable` is the raw cstat bit every state rule turns on, kept so a surprising state shows its cause.
        const bool hittable = target_hittable_rule(key.xrepeat, cstat);
        const ActorState st = actor_state_cached(i);
        LOGI("actor: i=%d pic=%d %s extra=%d cstat=0x%04X stat=%d t4=%08X "
             "act=(%d,%d,%d,%d,%d) ok=%d disp=%d xrep=%d pal=%d z=%d | "
             "hittable=%d state=%s shown=%d cand=%d ms=%u",
             i, picnum, actor_name(picnum), key.extra, (unsigned)cstat,
             key.statnum, (unsigned)key.t4,
             act.startframe, act.numframes, act.viewtype, act.incval, act.delay,
             actionOk ? 1 : 0, haveDisp ? disp : -1,
             key.xrepeat, key.pal, engine_sprite_field_l(i, SPR_Z),
             hittable ? 1 : 0, actor_state_name(st),
             actor_state_shown(st) ? 1 : 0, actor_state_targetable(st) ? 1 : 0,
             wallclock_ms());
      }

      s_track[slot].picnum = picnum;
      s_track[slot].key = key;
      s_track[slot].seen = true;
    }
  }

  // Anything the walk did not reach is gone (out of range, off both lists, no longer a badguy, or retired); which, is not knowable.

  for (int k = 0; k < ACTORSTATE_MAX_TRACKED; k++)
    if (s_track[k].used && !s_track[k].seen) actorstate_release(k);
}
