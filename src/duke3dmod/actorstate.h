#pragma once
#include "target.h"
#include "common.h"

// Actor-state instrument: logs an enemy's state (g_cfg.debuglog, 64 actors / 400 lines a session, writes nothing) to
// build classifier rules on ground truth. actorstate.cpp cannot link into test_host (the target.h/aim.cpp split).

const int ACTORSTATE_MAX_TRACKED = 64;
const int ACTORSTATE_MAX_LINES   = 400;
// Bits a change can be about (blocking, hittable, invisible); x/y-flip animation spends no line.
const int ACTORSTATE_CSTAT_MASK  = 0x0001 | 0x0100 | 0x8000;

// t4 (CON action pointer) discriminates; pal marks tougher variants; picnum catches a recycled slot within one tic.
struct ActorKey   { int extra; int cstatBits; int statnum; int t4; int xrepeat; int pal; int picnum; };
struct ActorTrack { int index; int picnum; ActorKey key; bool used; bool seen; };

// A plain disjunction, not memcmp: a field missing here is a visible omission.
inline bool actorstate_changed(const ActorKey& a, const ActorKey& b) {
  return a.extra != b.extra || a.cstatBits != b.cstatBits ||
         a.statnum != b.statnum || a.t4 != b.t4 ||
         a.xrepeat != b.xrepeat || a.pal != b.pal ||
         a.picnum != b.picnum;
}

// The existing-slot search runs in full first, so a hole before the wanted index cannot be claimed twice.
inline int actorstate_slot_for(ActorTrack* t, int n, int index) {
  if (!t || n <= 0) return -1;
  for (int k = 0; k < n; k++)
    if (t[k].used && t[k].index == index) return k;
  for (int k = 0; k < n; k++) {
    if (t[k].used) continue;
    t[k].used = true;
    t[k].index = index;
    t[k].seen = false;
    return k;
  }
  return -1;
}

inline bool actorstate_budget_ok(int linesSoFar, int cap) { return linesSoFar < cap; }

// A level change zeroes the counter, nothing else does. actorstate.cpp calls aim.cpp's and dmgnum.cpp's resets, so each
// counter stays file-static.
inline int budget_after_level_change(bool levelChanged, int linesSoFar) {
  return levelChanged ? 0 : linesSoFar;
}

// Dying enemies leave the ESP and aimbot, never alive ones (tb_0113). The engine's alive test is `extra < 0`
// (troopers walk and shoot at exactly 0), so zero is alive here too.

enum ActorState { AS_ALIVE = 0, AS_CHOKING, AS_PLAYDEAD, AS_CLOAKED, AS_DYING, AS_DEAD, AS_NONE };
const int AS_CSTAT_HITTABLE  = 0x0100;   // cstat bit 8
const int AS_CSTAT_INVISIBLE = 0x8000;   // cstat bit 15 (a teleported captain)

inline bool action_record_is(const ActorAction& a, int sf, int nf, int vt, int iv, int dl) {
  return a.readable && a.startframe == sf && a.numframes == nf && a.viewtype == vt &&
         a.incval == iv && a.delay == dl;
}
// ATROOPSUFFERING (59, 2, 1, 1, 21): choking trooper, extra 0, hittable (0x016310DC).
inline bool actor_record_is_suffering(const ActorAction& self) { return action_record_is(self, 59, 2, 1, 1, 21); }
// ATROOPPLAYDEAD has ATROOPDEAD's record (54, 0, 0, 0, 0), 20 bytes after it; SUFFERDEAD (58, 2, 1, -4, 24) follows.
// A "down" record is playing dead iff its predecessor is also down and its successor is SUFFERDEAD (DEAD's predecessor
// is DYING). Addresses 0x016310A0 / 0x016310B4 / 0x016310C8.
inline bool actor_record_is_playdead(const ActorAction& self, const ActorAction& before, const ActorAction& after) {
  return action_record_is(self, 54, 0, 0, 0, 0) && action_record_is(before, 54, 0, 0, 0, 0) &&
         action_record_is(after, 58, 2, 1, -4, 24);
}

// Assault Commander's death record (30, 8, 1, 1, 12): `extra` flickers between negative and 0 while hittable.
const int ACTOR_PICNUM_COMMANDER = 1920;
inline bool actor_record_is_commander_dying(const ActorAction& self) { return action_record_is(self, 30, 8, 1, 1, 12); }

// A known death record (`dyingRecord`) outranks extra >= 0.
inline ActorState actor_state(int statnum, bool badguy, int xrepeat, int cstat, int extra,
                              bool choking, bool playDead, bool dyingRecord) {
  const bool onList = (statnum == TARGET_STAT_ACTOR || statnum == TARGET_STAT_ZOMBIEACTOR);
  if (!onList || !badguy || xrepeat == 0) return AS_NONE;
  const bool hittable = (cstat & AS_CSTAT_HITTABLE) != 0;
  if (extra < 0) return hittable ? AS_DYING : AS_DEAD;
  if (hittable) return dyingRecord ? AS_DYING : (choking ? AS_CHOKING : AS_ALIVE);
  if (playDead) return AS_PLAYDEAD;
  if (extra > 0) return AS_CLOAKED;   // a corpse is reset to 0: above zero and unhittable is withdrawn
  return AS_DEAD;
}
inline bool actor_state_shown(ActorState s)      { return s == AS_ALIVE || s == AS_CHOKING || s == AS_PLAYDEAD || s == AS_CLOAKED; }
inline bool actor_state_targetable(ActorState s) { return s == AS_ALIVE || s == AS_CHOKING; }
inline const char* actor_state_label(ActorState s) {
  switch (s) { case AS_CHOKING: return "Choking"; case AS_PLAYDEAD: return "Playing dead"; case AS_CLOAKED: return "Cloaked"; default: return nullptr; }
}
inline const char* actor_state_name(ActorState s) {
  switch (s) { case AS_ALIVE: return "alive"; case AS_CHOKING: return "choking"; case AS_PLAYDEAD: return "playdead";
               case AS_CLOAKED: return "cloaked"; case AS_DYING: return "dying"; case AS_DEAD: return "dead"; default: return "none"; }
}

// One state per sprite per tic (record questions cost up to three page queries). Generation 0 is never real, so a
// fresh cache cannot read begin(0) as "same generation".
struct ActorStateCache { unsigned gen; unsigned char known[rva::MAX_SPRITES / 8]; unsigned char state[rva::MAX_SPRITES]; };
inline void actor_cache_begin(ActorStateCache* c, unsigned gen) {
  if (!c || c->gen == gen) return;
  c->gen = gen;
  for (int b = 0; b < rva::MAX_SPRITES / 8; b++) c->known[b] = 0;
}
inline bool actor_cache_get(const ActorStateCache* c, int i, ActorState* out) {
  if (!c || !out || i < 0 || i >= rva::MAX_SPRITES) return false;
  if (!(c->known[(unsigned)i >> 3] & (unsigned char)(1u << ((unsigned)i & 7u)))) return false;
  *out = (ActorState)c->state[i];
  return true;
}
inline void actor_cache_put(ActorStateCache* c, int i, ActorState s) {
  if (!c || i < 0 || i >= rva::MAX_SPRITES) return;
  c->state[i] = (unsigned char)s;
  c->known[(unsigned)i >> 3] |= (unsigned char)(1u << ((unsigned)i & 7u));
}

// actorstate.cpp (DLL only): one tic's walk from hooked_processinput's tail, after esp_world_tic().
void actorstate_tic();
// Cached state of sprite i. The generation turns at hooked_processinput's head so silent aim and the shoot-time
// instrument see a just-killed target; the triggerbot asks one hook earlier and recomputes.
ActorState actor_state_cached(int i);
void actor_state_generation_begin(unsigned gen);
