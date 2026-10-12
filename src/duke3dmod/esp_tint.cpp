#include "esp_tint.h"
#include "world.h"
#include "esp_world.h"
#include "esp_switch.h"
#include "esp.h"          // esp_switch_sprite_walk: the statnum-0 switch walk shared with the marker pass
#include "esp_layout.h"   // esp_within_draw_distance
#include "trigger.h"      // trigger_world_dist2d
#include "engine.h"
#include "project.h"
#include "canvas.h"
#include "common.h"
#include "hooks.h"    // g_base: the sintable is read at a fixed RVA off it
#include "config.h"

// The combination switch tint, the in-world layer's first producer. Decides nothing: it walks live sprite memory and projects
// through the frame's camera (rules: tint_* in esp_tint.h, switch_classify, project.h), so it cannot link into test_host. Runs
// only from world_draw_frame's producer slot, which resets the queue first; gated by g_cfg.espswitches like the marker.

static int s_lastCount = 0;   // quads the layer ACCEPTED on the last collect, so tint= agrees with queued=

enum { TINT_KIND_FACE = 0, TINT_KIND_WALL = 1, TINT_KIND_TEX = 2, TINT_KIND_N = 3 };
static int s_kindCount[TINT_KIND_N] = { 0, 0, 0 };

int esp_tint_last_count() { return s_lastCount; }

void esp_tint_kind_counts(int* face, int* wall, int* tex) {
  if (face) *face = s_kindCount[TINT_KIND_FACE];
  if (wall) *wall = s_kindCount[TINT_KIND_WALL];
  if (tex)  *tex  = s_kindCount[TINT_KIND_TEX];
}

// A world point for the in-world layer: projected through the frame's camera, converted to the VIEWPORT coordinates the
// vertex formula uses. False when it does not project; the caller skips the whole quad, never clips it.
static bool tint_point(const CamState& cam, const GProj& g, const short* sintable,
                       int x, int y, int z, WorldPoint* out) {
  return world_view_point(cam, g, sintable, x, y, z, out);
}

// For a point NOT on the integer lattice: wall-sprite corners are fractional and rounding would restore the error
// project_world_d removes.
static bool tint_point_d(const CamState& cam, const GProj& g, const short* sintable,
                         double x, double y, double z, WorldPoint* out) {
  ProjectDebug dbg;
  const ScreenPt p = project_world_d(cam, g, sintable, x, y, z, &dbg);
  if (!p.visible) return false;
  *out = world_view_point(p, cam, dbg.gdepth, dbg.oz2);
  return true;
}

static void tint_push(const WorldPoint p[4], unsigned colour, int kind) {
  WorldQuad q;
  for (int k = 0; k < 4; k++) q.p[k] = p[k];
  q.rgba = colour;
  if (!world_push_quad(q)) return;
  s_lastCount++;
  if (kind >= 0 && kind < TINT_KIND_N) s_kindCount[kind]++;
}

// A FACE sprite is a screen-space box: its four corners from project_sprite_corners, all four or nothing. Each corner
// carries its OWN pitch term (drawpoly rotates every vertex separately); the centre's on all four z-fought.
static void tint_face_sprite(int i, const CamState& cam, const GProj& g,
                             const short* sintable, int pic, int cstat,
                             int wx, int wy, int wz, unsigned colour) {
  const SpriteExtents ext = proj_sprite_extents(wz, cstat,
                                                engine_tilesizx(pic), engine_tilesizy(pic),
                                                engine_sprite_field_u8(i, SPR_XREPEAT),
                                                engine_sprite_field_u8(i, SPR_YREPEAT));
  ScreenPt pts[4];
  ProjectDebug dbgs[4];
  const int n = project_sprite_corners(cam, g, sintable, wx, wy, ext, pts, dbgs);
  if (!proj_box_corners_ok(n)) return;
  WorldPoint p[4];
  for (int k = 0; k < 4; k++) p[k] = world_view_point(pts[k], cam, dbgs[k].gdepth, dbgs[k].oz2);
  tint_push(p, colour, TINT_KIND_FACE);
}

// A WALL sprite (cstat kind 1): a real world quad from polymost_drawsprite's corner rule (esp_tint.h); xoff/yoff are negated
// by the flip bits. picanm bytes are NOT added: error is at most that tile's offset byte per axis, UNVERIFIED, not assumed zero.
static void tint_wall_sprite(int i, const CamState& cam, const GProj& g,
                             const short* sintable, int pic, int cstat,
                             int wx, int wy, int wz, unsigned colour) {
  const int xrep = engine_sprite_field_u8(i, SPR_XREPEAT);
  const int yrep = engine_sprite_field_u8(i, SPR_YREPEAT);
  const int tsx  = engine_tilesizx(pic);
  const int tsy  = engine_tilesizy(pic);
  // Signed chars read through the unsigned reader: restore the sign or the span shifts a tile sideways.
  int xoff = (int)(signed char)(unsigned char)engine_sprite_field_u8(i, SPR_XOFFSET);
  int yoff = (int)(signed char)(unsigned char)engine_sprite_field_u8(i, SPR_YOFFSET);
  if (cstat & 4) xoff = -xoff;
  if (cstat & 8) yoff = -yoff;

  WorldXYd a, b;
  tint_wall_sprite_endpoints(wx, wy, engine_sprite_field_s(i, SPR_ANG) & 2047,
                             xrep, tsx, xoff, sintable, &a, &b);
  int top = 0, bottom = 0;
  tint_wall_sprite_zrange(wz, yrep, tsy, yoff, (cstat & SPR_CSTAT_CENTRED) != 0,
                          &top, &bottom);

  // The sector's FLAT ceilingz/floorz, which the engine's wall-sprite branch compares against, not the slope height. Sector
  // index checked HERE: the reader returns 0 for an unreadable sector, an ordinary z, so an untrusted one clamps NOTHING.
  const int sect = engine_sprite_field_s(i, SPR_SECTNUM);
  if (sect >= 0 && sect < engine_numsectors()) {
    tint_clamp_to_sector(
        engine_sector_field_l(sect, SEC_CEILINGZ),
        (engine_sector_field_s(sect, SEC_CEILINGSTAT) & SEC_STAT_PARALLAX) != 0,
        engine_sector_field_l(sect, SEC_FLOORZ),
        (engine_sector_field_s(sect, SEC_FLOORSTAT) & SEC_STAT_PARALLAX) != 0,
        &top, &bottom);
  }

  // Top-left, top-right, bottom-right, bottom-left. Through the double entry: a and b are fractional and tint_point would
  // round them onto the lattice.
  WorldPoint p[4];
  if (!tint_point_d(cam, g, sintable, a.x, a.y, (double)top,    &p[0])) return;
  if (!tint_point_d(cam, g, sintable, b.x, b.y, (double)top,    &p[1])) return;
  if (!tint_point_d(cam, g, sintable, b.x, b.y, (double)bottom, &p[2])) return;
  if (!tint_point_d(cam, g, sintable, a.x, a.y, (double)bottom, &p[3])) return;
  tint_push(p, colour, TINT_KIND_WALL);
}

// A wall SWITCH (ESP_WKIND_SWITCH mark) tints the WHOLE WALL FACE: both endpoints, floor to ceiling through the slope
// functions, so a sloped room gets a tilted panel. The sector comes from the MARK (nextsector is the far side). Not a
// tile-sized patch: that needs the wall's repeat/panning and two-sided split, and a wrong guess would tint non-switch wall.
static void tint_wall_switch(const EspWallMark& wm, const CamState& cam, const GProj& g,
                             const short* sintable, unsigned colour) {
  const int p2 = engine_wall_field_s(wm.wallnum, rva::WAL_POINT2);
  if (p2 < 0 || p2 >= engine_numwalls()) return;
  const int x0 = engine_wall_field_l(wm.wallnum, WAL_X);
  const int y0 = engine_wall_field_l(wm.wallnum, WAL_Y);
  const int x1 = engine_wall_field_l(p2, WAL_X);
  const int y1 = engine_wall_field_l(p2, WAL_Y);

  int c0 = 0, c1 = 0, f0 = 0, f1 = 0;
  if (!engine_ceilz(wm.sectnum, x0, y0, &c0)) return;
  if (!engine_ceilz(wm.sectnum, x1, y1, &c1)) return;
  if (!engine_florz(wm.sectnum, x0, y0, &f0)) return;
  if (!engine_florz(wm.sectnum, x1, y1, &f1)) return;

  WorldPoint p[4];
  if (!tint_point(cam, g, sintable, x0, y0, c0, &p[0])) return;
  if (!tint_point(cam, g, sintable, x1, y1, c1, &p[1])) return;
  if (!tint_point(cam, g, sintable, x1, y1, f1, &p[2])) return;
  if (!tint_point(cam, g, sintable, x0, y0, f0, &p[3])) return;
  tint_push(p, colour, TINT_KIND_TEX);
}

// The draw-distance cull, as the ESP marker over the same switch obeys (else a distant lock showed tinted faces with no
// marker). Measured in the WORLD (no visibility term); no on-screen cull, since the layer clips these depth-tested quads.
static bool tint_in_range(const CamState& cam, int wx, int wy) {
  return esp_within_draw_distance_or_unlimited(trigger_world_dist2d(cam.posx, cam.posy, wx, wy),
                                              g_cfg.espdrawdist, g_cfg.espnolimit);
}

// cstat bits 4-5 pick the kind (0 face, 1 wall); 2 is a floor sprite, which no switch in this game is.
struct TintSwitchCtx { const CamState* cam; const GProj* g; const short* sintable; };
static void tint_switch_sprite(const EspSwitchSprite& s, void* vctx) {
  const TintSwitchCtx& c = *(const TintSwitchCtx*)vctx;
  const int verdict = tint_verdict(s.si.family, s.si.line);
  if (verdict == TINT_NONE) return;
  const unsigned colour = tint_colour(verdict, C_TINT_FLIP, C_TINT_LEAVE);
  const int wx = engine_sprite_field_l(s.i, SPR_X);
  const int wy = engine_sprite_field_l(s.i, SPR_Y);
  const int wz = engine_sprite_field_l(s.i, SPR_Z);
  if (!tint_in_range(*c.cam, wx, wy)) return;
  const int kind = (s.cstat >> 4) & 3;
  if (kind == 0)      tint_face_sprite(s.i, *c.cam, *c.g, c.sintable, s.pic, s.cstat, wx, wy, wz, colour);
  else if (kind == 1) tint_wall_sprite(s.i, *c.cam, *c.g, c.sintable, s.pic, s.cstat, wx, wy, wz, colour);
}

void esp_tint_collect() {
  s_lastCount = 0;
  for (int k = 0; k < TINT_KIND_N; k++) s_kindCount[k] = 0;
  if (g_cfg.espswitches == 0 || !engine_verified() || !world_frame_valid()) return;
  const CamState cam = engine_camera();
  if (!cam.valid) return;
  const GProj g = engine_gproj();
  const short* sintable = (const short*)(g_base + rva::SINTABLE);
  const int access = engine_player_got_access();

  // Switch SPRITES use the shared walk that esp.cpp's marker pass also drives, so tint and marker agree. This runs from the
  // displaymasks hook, esp_draw's from overlay_frame: never nested (engine.cpp's per-walk cycle counter is single-slot).
  TintSwitchCtx tctx;
  tctx.cam = &cam; tctx.g = &g; tctx.sintable = sintable;
  esp_switch_sprite_walk(access, tint_switch_sprite, &tctx);

  // The tic-clock scan's snapshot, through the same seqlock as esp_draw; no clean copy means no tints for one frame.
  EspWallSnapshot snap;
  if (!esp_wall_snapshot(&snap)) return;
  const int n = esp_wall_count_clamped(snap.count);
  for (int k = 0; k < n; k++) {
    const EspWallMark& wm = snap.m[k];
    if (wm.kind != ESP_WKIND_SWITCH) continue;
    // Classified HERE on the render clock: wall fields do not change between tics, lock state does.
    const SwitchInfo si = switch_classify(wm.picnum, wm.pal, wm.hitag, wm.lotag, access);
    const int verdict = tint_verdict(si.family, si.line);
    if (verdict == TINT_NONE) continue;
    if (!tint_in_range(cam, wm.x, wm.y)) continue;
    tint_wall_switch(wm, cam, g, sintable, tint_colour(verdict, C_TINT_FLIP, C_TINT_LEAVE));
  }
}
