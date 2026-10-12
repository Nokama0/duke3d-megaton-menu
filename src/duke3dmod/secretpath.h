#pragma once

#include "common.h"

// Path to a secret: breadth-first over the sector graph to the nearest lotag-32767 sector. Pure; secretpath.cpp is
// DLL-only. A HINT, NOT A GUARANTEE: geometry only, so it cannot know a door needs a keycard or a drop is one way.

// rva::MAX_SECTORS is 4096; a route through over a quarter of that is no hint, so the search refuses.
const int SPATH_MAX_SECTORS = 1024;
// The search looks no deeper: a farther goal reads as "no path".
const int SPATH_MAX_HOPS    = 64;
// 6144: a climbable step (512 units/metre).
const int SPATH_STEP_UP     = 0x1800;
// 10240: a crouching player's height.
const int SPATH_MIN_GAP     = 0x2800;

// A teleport edge is half of a paired sector effector 7 (teleporter, water surface or underwater side).
enum { SPATH_EDGE_WALK = 0, SPATH_EDGE_TELEPORT = 1 };

// Build z grows DOWNWARD: a higher floor is a SMALLER floorz.
struct SPathSector { int floorz, ceilingz, lotag; };

// x/y/z is where the polyline turns.
struct SPathEdge { int from, to, kind, x, y, z; };

// Duke's door and lift sector tags (sector.c ST_*): 20-23, 25-31. Not 24, nor the 15-19 platforms/elevators (judged on step).
inline int spath_lotag_is_door(int lotag) {
  return ((lotag >= 20 && lotag <= 23) || (lotag >= 25 && lotag <= 31)) ? 1 : 0;
}

// A door or lift passes any gap (it opens) but not any STEP. long long: live map heights.
inline int spath_edge_passable(const SPathSector& a, const SPathSector& b, int bIsDoor) {
  const long long climb = (long long)a.floorz - (long long)b.floorz;   // > 0: b's floor is higher
  if (climb > (long long)SPATH_STEP_UP) return 0;
  if (bIsDoor) return 1;
  const long long gap = (long long)b.floorz - (long long)b.ceilingz;
  return (gap >= (long long)SPATH_MIN_GAP) ? 1 : 0;
}

// Returns the hop count (0 for none) and fills `out` with at most `outCap` edges. Every pointer is required.
inline int spath_search(const SPathEdge* edges, int edgeCount, const SPathSector* sectors,
                        int sectorCount, const unsigned char* isDoor,
                        const unsigned char* isGoal, int start,
                        SPathEdge* out, int outCap) {
  if (!edges || !sectors || !isDoor || !isGoal || !out) return 0;
  if (edgeCount < 0 || outCap <= 0) return 0;
  if (sectorCount <= 0 || sectorCount > SPATH_MAX_SECTORS) return 0;
  if (start < 0 || start >= sectorCount) return 0;
  // Already in the secret: nothing to point at.
  if (isGoal[start]) return 0;

  unsigned char seen[SPATH_MAX_SECTORS];
  short depth[SPATH_MAX_SECTORS];
  int   via[SPATH_MAX_SECTORS];
  short queue[SPATH_MAX_SECTORS];
  for (int i = 0; i < sectorCount; i++) { seen[i] = 0; depth[i] = 0; via[i] = -1; }

  int head = 0, tail = 0, goal = -1;
  seen[start] = 1;
  queue[tail++] = (short)start;
  while (head < tail && goal < 0) {
    const int s = queue[head++];
    if (depth[s] >= SPATH_MAX_HOPS) continue;
    for (int e = 0; e < edgeCount; e++) {
      const SPathEdge& ed = edges[e];
      if (ed.from != s) continue;
      const int to = ed.to;
      if (to < 0 || to >= sectorCount || seen[to]) continue;
      // A teleport edge has no step and no gap.
      if (ed.kind == SPATH_EDGE_WALK &&
          !spath_edge_passable(sectors[s], sectors[to], isDoor[to] != 0)) continue;
      seen[to] = 1;
      via[to] = e;
      depth[to] = (short)(depth[s] + 1);
      queue[tail++] = (short)to;
      if (isGoal[to]) { goal = to; break; }
    }
  }
  if (goal < 0) return 0;

  // Keep the FIRST outCap edges, so a truncated path is the near end.
  SPathEdge rev[SPATH_MAX_HOPS];
  int at = goal, n = 0;
  while (at != start && n < SPATH_MAX_HOPS) {
    const int e = via[at];
    if (e < 0 || e >= edgeCount) break;
    rev[n++] = edges[e];
    at = edges[e].from;
  }
  const int keep = (n < outCap) ? n : outCap;
  for (int i = 0; i < keep; i++) out[i] = rev[n - 1 - i];
  return depth[goal];
}

// Rescan on a new level, a room change, or the period elapsing (unsigned subtraction survives the clock wrap).
inline int spath_rescan_due(int scanned, unsigned sig, unsigned lastSig,
                            int sect, int lastSect,
                            unsigned nowMs, unsigned lastMs, unsigned periodMs) {
  if (!scanned) return 1;
  if (sig != lastSig) return 1;
  if (sect != lastSect) return 1;
  return ((unsigned)(nowMs - lastMs) >= periodMs) ? 1 : 0;
}

struct SPathPoint { int x, y, z, kind; };

// `seq` is a seqlock (target.h's snap_*). `hops` can exceed `count`, which is what the point array had room for.
struct SPathSnapshot {
  unsigned seq;
  int hops;
  int teleports;
  int count;         // stored points, 0..SPATH_MAX_HOPS
  int fromSect, toSect;
  SPathPoint p[SPATH_MAX_HOPS];
};

inline int spath_count_clamped(int n) { return clamp_int(n, 0, SPATH_MAX_HOPS); }

inline const char* spath_room_word(int hops) { return hops == 1 ? "room" : "rooms"; }

// secretpath.cpp: tic-clock scan (hooked_processinput's tail, after trigger_tic) and render-clock draw. No-ops unless
// "Secret areas" and "Path to a secret" are on.
void secretpath_tic();
void secretpath_draw(int screenW, int screenH);
bool secretpath_snapshot(SPathSnapshot* out);
