#include "secretpath.h"
#include "trigger.h"
#include "freecam.h"
#include "engine.h"
#include "esp_layout.h"
#include "esp_world.h"
#include "wallclock.h"
#include "project.h"
#include "canvas.h"
#include "common.h"
#include "hooks.h"
#include "config.h"
#include "font.h"
#include "log.h"
#include "target.h"
#include <stdio.h>

// Path to a secret: the tic-clock search and render-clock draw. Decides nothing; hands live map memory to spath_search
// (secretpath.h, pure). The clocks meet at s_pub through a seqlock: same thread, but a partial path must not be drawable.

// How many candidate edges one scan may hold. A refusal, not a clamp: past it the search runs on the edges staged, so a
// huge map gives a shorter answer rather than a wrong one. 8192 covers a 1024-sector map's two-sided walls.
const int SPATH_EDGE_MAX = 8192;

// Half the arm of the cross at a teleport hop, as in trigger.cpp: a full-size cross beside a secret marker reads as one smudge.
const int SPATH_CROSS_ARM = ESP_SECRET_ARM / 2;

// A blocking two-sided wall (cstat bit 0, the clip bit) is not a way through: a force field or railing sets it.
const unsigned SPATH_CSTAT_BLOCK = 1u;

// The published result and the scan's bookkeeping. Static, not stack: the scan runs inside the engine's update.
static SPathSnapshot s_pub = { 0u, 0, 0, 0, -1, -1, {} };
static SPathSector   s_sec[SPATH_MAX_SECTORS];
static unsigned char s_isDoor[SPATH_MAX_SECTORS];
static unsigned char s_isGoal[SPATH_MAX_SECTORS];
static SPathEdge     s_edges[SPATH_EDGE_MAX];
static SPathEdge     s_route[SPATH_MAX_HOPS];

static unsigned s_sig = 0;
static unsigned s_lastScanMs = 0;
static int      s_lastSect = -1;
static bool     s_scanned = false;
static int s_logFrom = -2, s_logTo = -2, s_logHops = -1, s_logTele = -1;

static unsigned path_seq_read() { return *(volatile unsigned*)&s_pub.seq; }
static void path_seq_write(unsigned v) { *(volatile unsigned*)&s_pub.seq = v; }

// Publishes an empty path: what the off state and every refusal leave behind, so nothing stays latched on screen.
static void path_publish_empty() {
  unsigned seq = path_seq_read();
  snap_begin(&seq);
  path_seq_write(seq);
  s_pub.hops = 0;
  s_pub.teleports = 0;
  s_pub.count = 0;
  s_pub.fromSect = -1;
  s_pub.toSect = -1;
  snap_end(&seq);
  path_seq_write(seq);
}

bool secretpath_snapshot(SPathSnapshot* out) {
  if (!out) return false;
  const unsigned before = path_seq_read();
  SPathSnapshot tmp = s_pub;
  const unsigned after = path_seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

// Sub-switch of "Secret areas"; gating it this way keeps the feature out of esp_any_on (espsecrets already opens the GL
// bracket the draw needs).
static bool path_on() { return g_cfg.espsecrets != 0 && g_cfg.espsecretpath != 0; }

// Stages every two-sided wall of sectors 0..nsec-1 as a walk edge at the wall's midpoint and the near sector's floor.
static int path_stage_walls(int nsec, int at, bool* overflow) {
  const int nwalls = engine_numwalls();
  for (int s = 0; s < nsec; s++) {
    int wallptr = 0, wallnum = 0;
    if (!engine_sector_wall_range(s, &wallptr, &wallnum)) continue;
    const int florz = engine_sector_field_l(s, SEC_FLOORZ);
    for (int w = wallptr; w < wallptr + wallnum; w++) {
      const int nx = engine_wall_field_s(w, rva::WAL_NEXTSECTOR);
      if (nx < 0 || nx >= nsec) continue;
      if (((unsigned)engine_wall_field_s(w, rva::WAL_CSTAT) & SPATH_CSTAT_BLOCK) != 0) continue;
      if (at >= SPATH_EDGE_MAX) { if (overflow) *overflow = true; return at; }
      // point2 is a second index nothing upstream vouched for: bounds-checked against live numwalls.
      const int point2 = engine_wall_field_s(w, rva::WAL_POINT2);
      if (point2 < 0 || point2 >= nwalls) continue;
      SPathEdge& e = s_edges[at++];
      e.from = s;
      e.to = nx;
      e.kind = SPATH_EDGE_WALK;
      e.x = esp_wall_mid(engine_wall_field_l(w, WAL_X), engine_wall_field_l(point2, WAL_X));
      e.y = esp_wall_mid(engine_wall_field_l(w, WAL_Y), engine_wall_field_l(point2, WAL_Y));
      e.z = florz;
    }
  }
  return at;
}

// Stages each paired sector effector 7 as a teleport edge: statnum 9, paired through SPR_OWNER, the effector's SECTOR's
// lotag giving the kind (0 teleporter, 1 water surface, 2 underwater), as movetransports reads it (freecam.h).
static int path_stage_transports(int nsec, int at, bool* overflow) {
  for (int i = engine_first_sprite_of_stat(FREECAM_TRANSPORT_STAT); i >= 0;
       i = engine_next_sprite(i)) {
    const int owner = engine_sprite_field_s(i, SPR_OWNER);
    if (owner < 0 || owner == i) continue;
    const int from = engine_sprite_field_s(i, SPR_SECTNUM);
    const int to   = engine_sprite_field_s(owner, SPR_SECTNUM);
    if (from < 0 || from >= nsec || to < 0 || to >= nsec || from == to) continue;
    const int lotag = engine_sector_lotag(from);
    if (lotag != 0 && lotag != 1 && lotag != 2) continue;
    if (at >= SPATH_EDGE_MAX) { if (overflow) *overflow = true; return at; }
    SPathEdge& e = s_edges[at++];
    e.from = from;
    e.to = to;
    e.kind = SPATH_EDGE_TELEPORT;
    e.x = engine_sprite_field_l(i, SPR_X);
    e.y = engine_sprite_field_l(i, SPR_Y);
    e.z = engine_sprite_field_l(i, SPR_Z);
  }
  return at;
}

void secretpath_tic() {
  // Off means no scan and no stale path: the snapshot is emptied and s_scanned cleared so turning it on rebuilds next tic.
  if (!path_on() || !engine_verified()) {
    if (s_scanned || s_pub.count != 0) path_publish_empty();
    s_scanned = false;
    s_lastSect = -1;
    return;
  }

  const PlayerPos pp = engine_player_pos();
  const int nsectors = engine_numsectors();
  // Bounded by the live count, the static array and the search's cap: a corrupt numsectors must not widen the window.
  const int nsec = (nsectors < SPATH_MAX_SECTORS) ? nsectors : SPATH_MAX_SECTORS;
  if (!pp.valid || pp.cursectnum < 0 || pp.cursectnum >= nsec) {
    if (s_pub.count != 0) path_publish_empty();
    s_scanned = false;
    s_lastSect = -1;
    return;
  }

  // Third signature term is 0 deliberately: the standables count is trigger.cpp's own; this scan triggers on the
  // player's room and a one-second timer that picks up a secret just found.
  const unsigned sig = trigger_map_signature(nsectors, engine_numwalls(), 0);
  const unsigned now = wallclock_ms();
  if (!spath_rescan_due(s_scanned ? 1 : 0, sig, s_sig, pp.cursectnum, s_lastSect,
                        now, s_lastScanMs, TRIG_SCAN_PERIOD_MS))
    return;

  for (int s = 0; s < nsec; s++) {
    const int lotag = engine_sector_lotag(s);
    s_sec[s].floorz = engine_sector_field_l(s, SEC_FLOORZ);
    s_sec[s].ceilingz = engine_sector_field_l(s, SEC_CEILINGZ);
    s_sec[s].lotag = lotag;
    s_isDoor[s] = (unsigned char)spath_lotag_is_door(lotag);
    // The goal is the secret ESP's own question: the engine clears a secret's lotag when found.
    s_isGoal[s] = engine_sector_is_secret(s) ? (unsigned char)1 : (unsigned char)0;
  }

  // Walls first: engine_first_sprite_of_stat's cycle counter is single-slot, so the sector sweep must finish before it.
  bool overflow = false;
  int edgeCount = path_stage_walls(nsec, 0, &overflow);
  edgeCount = path_stage_transports(nsec, edgeCount, &overflow);

  const int hops = spath_search(s_edges, edgeCount, s_sec, nsec, s_isDoor, s_isGoal,
                                pp.cursectnum, s_route, SPATH_MAX_HOPS);
  const int count = (hops < SPATH_MAX_HOPS) ? hops : SPATH_MAX_HOPS;
  int teleports = 0;
  for (int k = 0; k < count; k++)
    if (s_route[k].kind == SPATH_EDGE_TELEPORT) teleports++;

  s_sig = sig;
  s_lastScanMs = now;
  s_lastSect = pp.cursectnum;
  s_scanned = true;

  if (hops <= 0) {
    path_publish_empty();
    return;
  }

  // Publish under the bracket, count LAST: the render clock reads s_pub.p under that count.
  const int toSect = s_route[count - 1].to;
  unsigned seq = path_seq_read();
  snap_begin(&seq);
  path_seq_write(seq);
  s_pub.hops = hops;
  s_pub.teleports = teleports;
  s_pub.fromSect = pp.cursectnum;
  s_pub.toSect = toSect;
  for (int k = 0; k < count; k++) {
    s_pub.p[k].x = s_route[k].x;
    s_pub.p[k].y = s_route[k].y;
    s_pub.p[k].z = s_route[k].z;
    s_pub.p[k].kind = s_route[k].kind;
  }
  s_pub.count = count;
  snap_end(&seq);
  path_seq_write(seq);

  if (g_cfg.debuglog != 0 &&
      (pp.cursectnum != s_logFrom || toSect != s_logTo ||
       hops != s_logHops || teleports != s_logTele)) {
    s_logFrom = pp.cursectnum; s_logTo = toSect;
    s_logHops = hops; s_logTele = teleports;
    LOGI("spath: from=%d to=%d hops=%d teleports=%d", pp.cursectnum, toSect, hops, teleports);
    if (overflow) LOGI("spath: edges=%d overflow=1 sectors=%d", edgeCount, nsec);
  }
}

// One polyline segment, with trigger.cpp's near-plane rule: one refused end walks toward the other until accepted, on
// `visible`, never depth alone (project_world also refuses on the pitched view plane).
static void path_draw_segment(const CamState& cam, const GProj& g, const short* sintable,
                              int ax, int ay, int az, int bx, int by, int bz, unsigned colour) {
  ProjectDebug aDbg, bDbg;
  const ScreenPt a = project_world(cam, g, sintable, ax, ay, az, &aDbg);
  const ScreenPt b = project_world(cam, g, sintable, bx, by, bz, &bDbg);
  ScreenPt e0 = a, e1 = b;
  bool haveLine = a.visible && b.visible;
  if (!haveLine && (a.visible || b.visible)) {
    const bool aRefused = !a.visible;
    const double uKeep = aRefused ? 1.0 : 0.0;
    const double uEnd  = aRefused ? 0.0 : 1.0;
    const double plane = PROJ_NEAR_CLIP + TRIG_CLIP_MARGIN;
    const double nearT = trigger_segment_clip_t(aDbg.gdepth, bDbg.gdepth, plane);
    const double refusedDepth = aRefused ? aDbg.gdepth : bDbg.gdepth;
    double u = (nearT > 0.0 && nearT < 1.0 && !(refusedDepth >= plane))
                 ? nearT : trigger_bisect_toward(uEnd, uKeep);
    for (int k = 0; k <= TRIG_CLIP_STEPS; k++) {
      if (k) u = trigger_bisect_toward(u, uKeep);
      int cx = 0, cy = 0, cz = 0;
      trigger_lerp_point(ax, ay, az, bx, by, bz, u, &cx, &cy, &cz);
      const ScreenPt cp = project_world(cam, g, sintable, cx, cy, cz);
      if (cp.visible) {
        if (aRefused) e0 = cp; else e1 = cp;
        haveLine = true;
        break;
      }
    }
  }
  if (haveLine) canvas_line(e0.x, e0.y, e1.x, e1.y, colour);
}

// The cross at a teleport hop. Widens before subtracting and clamps back: project_world saturates to the int range.
static void path_draw_cross(int cx, int cy, unsigned colour) {
  const int span = SPATH_CROSS_ARM * 2 + 1;
  canvas_rect(clamp_ll_to_int((long long)cx - SPATH_CROSS_ARM),
              clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2),
              span, ESP_SECRET_T, colour);
  canvas_rect(clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2),
              clamp_ll_to_int((long long)cy - SPATH_CROSS_ARM),
              ESP_SECRET_T, span, colour);
}

// Called from esp_draw inside the GL bracket, before the marker pass. Range is per POINT (esp_marker_in_range), but a
// segment needs both points in range, since half a segment to a culled point would point nowhere.
void secretpath_draw(int screenW, int screenH) {
  if (!path_on() || !engine_verified()) return;

  SPathSnapshot sp;
  if (!secretpath_snapshot(&sp)) return;
  const int n = spath_count_clamped(sp.count);
  if (n <= 0) return;

  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  const unsigned colour = canvas_esp_secret_colour();

  int prevX = 0, prevY = 0, prevZ = 0;
  bool prevOk = false;
  for (int k = 0; k < n; k++) {
    const SPathPoint& pt = sp.p[k];
    const ScreenPt p = project_world(cam, g, sintable, pt.x, pt.y, pt.z);
    // Distance is project_world's world-space dist2d; a refused projection zeroes it, so range is asked of projected points only.
    const bool inRange = p.visible &&
        esp_marker_in_range(p, g_cfg.espdrawdist, g_cfg.espnolimit);

    if (prevOk && inRange)
      path_draw_segment(cam, g, sintable, prevX, prevY, prevZ, pt.x, pt.y, pt.z, colour);

    if (inRange && proj_on_screen(p.x, p.y, screenW, screenH)) {
      if (pt.kind == SPATH_EDGE_TELEPORT) path_draw_cross(p.x, p.y, colour);

      if (k == 0) {
        char lab[32];
        _snprintf_s(lab, sizeof(lab), _TRUNCATE, "Secret: %d %s",
                    sp.hops, spath_room_word(sp.hops));
        canvas_text(esp_label_x(p.x, 0, canvas_text_width(lab, FONT_SMALL)),
                    esp_secret_dist_label_y(p.y), lab, colour, FONT_SMALL);
      }
    }
    prevX = pt.x; prevY = pt.y; prevZ = pt.z; prevOk = inRange;
  }
}
