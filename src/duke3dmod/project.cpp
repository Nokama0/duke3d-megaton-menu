#include "project.h"
#include "common.h"
#include <math.h>

// Why this projection READS the engine's constants rather than recomputing: three recomputed revisions were
// algebraically identical yet wrong at every FOV, since `viewingrange` at buffer swap is not always what polymost_drawrooms
// used. Tilt is not reproduced. VAs: docs/re/esp-sprites.md.

// Pitched-view-plane clip on drawpoly's `oz2`. The engine does the same compare at 0x00475527-0x00475534 but skips the
// bail unless rendmode == 3; we keep it always, since a near-zero divide paints a cross at a garbage coordinate.
static const double NEAR_OZ2 = 1.0;

GProj proj_gproj_recomputed(const CamState& cam, const short* sintable) {
  GProj g = {};
  if (!cam.valid || !sintable || cam.halfxdimen <= 0 || cam.ydimen <= 0) return g;

  const int sinang = sintable[cam.ang & 2047];
  const int cosang = sintable[(cam.ang + 512) & 2047];
  g.cosang  = (double)cosang / 262144.0;
  g.sinang  = (double)sinang / 262144.0;
  g.cosang2 = (double)cam.viewingrange * g.cosang / 65536.0;
  g.sinang2 = (double)cam.viewingrange * g.sinang / 65536.0;
  g.halfx   = (double)cam.halfxdimen;
  g.yxscale = (double)cam.xdimenscale / 131072.0;

  // gshang/gchang, then the ghoriz overwrite: the engine writes ghoriz twice (globalhoriz at 0x0047a58f only to build
  // dh, then ydimen>>1, what sprites draw with), so only the second is stored. dhi is widened before subtracting:
  // cam.horiz is an unchecked live read.
  const int       halfy  = cam.ydimen >> 1;
  const long long dhi    = (long long)halfy - (long long)cam.horiz;
  const double    dh     = (double)dhi;
  const double    radius = sqrt(g.halfx * g.halfx + dh * dh);
  g.shang = dh / radius;
  g.chang = g.halfx / radius;
  g.horiz = (double)halfy;
  g.valid = true;
  return g;
}

// Plausibility bound for one engine double: closed comparisons so NaN fails both, and finite is not enough (an enormous
// value would overflow the products below into inf then NaN).
static bool proj_g_in_range(double v, double lo, double hi) {
  return v >= lo && v <= hi;
}

static bool proj_g_usable(const GProj& g) {
  return g.valid
      && proj_g_in_range(g.cosang,  -1.0,     1.0)
      && proj_g_in_range(g.sinang,  -1.0,     1.0)
      && proj_g_in_range(g.cosang2, -1.0e4,   1.0e4)
      && proj_g_in_range(g.sinang2, -1.0e4,   1.0e4)
      && proj_g_in_range(g.halfx,    1.0,     1.0e6)
      && proj_g_in_range(g.horiz,   -1.0e6,   1.0e6)
      && proj_g_in_range(g.shang,   -1.0,     1.0)
      && proj_g_in_range(g.chang,    0.0,     1.0)
      && proj_g_in_range(g.yxscale,  0.0,     1.0e6);
}

// The projection from glat/gdepth onward, shared by project_world (integer) and project_world_d (fractional): a second
// transcription of this calibrated arithmetic is the divergence that failed three times. Callers make the rejections
// tied to their operand types.
static ScreenPt project_core(const CamState& cam, const GProj& g,
                             double dx, double dy, double dz,
                             long long fwd, long long lat, ProjectDebug* dbg) {
  ScreenPt p = { 0, 0, 0, 0, false };

  // polymost_drawsprite 0x00482d02-0x00482d30. The lateral term takes the unscaled pair, the depth term the
  // viewingrange-scaled one; dropping that factor matched only at a neutral viewingrange.
  const double glat   = dy * g.cosang  - dx * g.sinang;
  const double gdepth = dx * g.cosang2 + dy * g.sinang2;

  if (dbg) {
    dbg->fwd = fwd; dbg->lat = lat;
    dbg->viewingrange = cam.viewingrange;
    dbg->dh = clamp_ll_to_int((long long)(cam.ydimen >> 1) - (long long)cam.horiz);
    dbg->glat = glat;
    dbg->gdepth = gdepth;
  }

  if (!(gdepth >= PROJ_NEAR_CLIP)) {
    if (dbg) dbg->reject = PROJ_REJECT_NEAR;
    return p;
  }

  // dpx/dpy minus the view centre, so ghalfx/ghoriz cancel exactly; then drawpoly's rotation (0x004754a7-0x004754de).
  const double ox  = glat * g.halfx / gdepth;
  const double oy  = dz * g.yxscale / gdepth;
  const double oz  = g.halfx;
  const double oy2 = oy * g.chang - oz * g.shang;
  const double oz2 = oy * g.shang + oz * g.chang;
  if (dbg) dbg->oz2 = oz2;
  if (!(oz2 >= NEAR_OZ2)) {
    if (dbg) dbg->reject = PROJ_REJECT_PITCH;
    return p;
  }
  const double f = g.halfx / oz2;


  // Committed only now the point projects: every ScreenPt field stays zero on the rejection paths. p.depth is in BUILD
  // units (g-space depth scaled back by 16).
  const double depth16 = gdepth * 16.0;
  p.depth = (depth16 > 2147483647.0) ? 0x7FFFFFFF : (int)depth16;

  const double dist2dD = sqrt((double)fwd * (double)fwd + (double)lat * (double)lat);
  p.dist2d = (dist2dD > 2147483647.0) ? 0x7FFFFFFF : (int)dist2dD;

  // drawpoly's final divide plus the viewport origin: resizeglcheck (0x00474fc0) places the viewport at (windowx1, yres -
  // (windowy2 + 1)); GL's origin is bottom-left. Clamped, not bare (int) casts: NaN is UB and this runs every frame.
  p.x = clamp_double_to_int((double)cam.windowx1 + g.halfx + ox * f);
  p.y = clamp_double_to_int((double)cam.windowy1 + g.horiz + oy2 * f);
  p.visible = true;
  return p;
}

// The int entry's `>>14` on a non-integer product: an arithmetic right shift is a floor, so this is the same truncation.
// Saturated; a NaN takes the low saturation.
static long long proj_floor_shift14(double v) {
  const double s = floor(v / 16384.0);
  const double lim = 9.0e15;
  if (!(s >= -lim)) return (long long)-lim;
  if (!(s <=  lim)) return (long long) lim;
  return (long long)s;
}

ScreenPt project_world(const CamState& cam, const GProj& g, const short* sintable,
                       int wx, int wy, int wz, ProjectDebug* dbg) {
  ScreenPt p = { 0, 0, 0, 0, false };
  if (dbg) *dbg = ProjectDebug{};

  if (!cam.valid || !sintable || !proj_g_usable(g)) {
    if (dbg) dbg->reject = PROJ_REJECT_INVALID;
    return p;
  }

  const int dx = wx - cam.posx;
  const int dy = wy - cam.posy;

  const int sinang = sintable[cam.ang & 2047];
  const int cosang = sintable[(cam.ang + 512) & 2047];
  const long long fwd = ((long long)dx * cosang + (long long)dy * sinang) >> 14;
  const long long lat = ((long long)dy * cosang - (long long)dx * sinang) >> 14;
  if (dbg) { dbg->dx = dx; dbg->dy = dy; dbg->sinang = sinang; dbg->cosang = cosang; }

  return project_core(cam, g, (double)dx, (double)dy,
                      (double)((long long)wz - cam.posz), fwd, lat, dbg);
}

// Same projection for a point off the integer lattice. A wall sprite's endpoints are `x - xv * f` with xv fractional;
// rounding moves each end up to half a unit, thousands of 24-bit depth units at switch-pressing distance
// (docs/re/esp-sprites.md A3), so the tint passes unrounded endpoints. fwd/lat floor instead of shift.
ScreenPt project_world_d(const CamState& cam, const GProj& g, const short* sintable,
                         double wx, double wy, double wz, ProjectDebug* dbg) {
  ScreenPt p = { 0, 0, 0, 0, false };
  if (dbg) *dbg = ProjectDebug{};

  if (!cam.valid || !sintable || !proj_g_usable(g)) {
    if (dbg) dbg->reject = PROJ_REJECT_INVALID;
    return p;
  }

  const double dx = wx - (double)cam.posx;
  const double dy = wy - (double)cam.posy;
  const double dz = wz - (double)cam.posz;

  const int sinang = sintable[cam.ang & 2047];
  const int cosang = sintable[(cam.ang + 512) & 2047];
  const long long fwd = proj_floor_shift14(dx * (double)cosang + dy * (double)sinang);
  const long long lat = proj_floor_shift14(dy * (double)cosang - dx * (double)sinang);
  if (dbg) {
    dbg->dx = clamp_double_to_int(dx); dbg->dy = clamp_double_to_int(dy);
    dbg->sinang = sinang; dbg->cosang = cosang;
  }

  return project_core(cam, g, dx, dy, dz, fwd, lat, dbg);
}

// The sprite's world-space extents. Live inputs are clamped into the engine field's real range BEFORE multiplying: a
// negative size collapses to zero, an absurd one saturates.
SpriteExtents proj_sprite_extents(int wz, int cstat, int tilesizx, int tilesizy,
                                  int xrepeat, int yrepeat) {
  SpriteExtents e = { 0, wz, wz };

  const long long tsx  = clamp_int(tilesizx, 0, PROJ_MAX_TILE_DIM);
  const long long tsy  = clamp_int(tilesizy, 0, PROJ_MAX_TILE_DIM);
  const long long xrep = clamp_int(xrepeat,  0, PROJ_MAX_REPEAT);
  const long long yrep = clamp_int(yrepeat,  0, PROJ_MAX_REPEAT);

  e.halfWidth = clamp_ll_to_int(tsx * xrep / PROJ_SPRITE_XDIV);

  // polymost_drawsprite's face branch 0x00482d02-0x00482d9a: cstat bit 7 clear hangs the sprite above spr->z; set centres it.
  const long long h = tsy * yrep * PROJ_SPRITE_ZSCALE;
  if (cstat & SPR_CSTAT_CENTRED) {
    e.topZ    = clamp_ll_to_int((long long)wz - h / 2);
    e.bottomZ = clamp_ll_to_int((long long)wz + (h - h / 2));
  } else {
    e.topZ    = clamp_ll_to_int((long long)wz - h);
    e.bottomZ = wz;
  }
  return e;
}

// The four corners, each projected alone and exposed so a QUAD caller gets all four: drawpoly rotates every corner
// separately, so they carry different oz2 (docs/re/esp-sprites.md A2). Ring order (c0,r0) (c1,r0) (c1,r1) (c0,r1). The
// side offset uses g.cosang/g.sinang, not sintable[cam.ang]: exactly perpendicular to the depth axis.
int project_sprite_corners(const CamState& cam, const GProj& g, const short* sintable,
                           int wx, int wy, const SpriteExtents& ext,
                           ScreenPt pts[4], ProjectDebug dbgs[4]) {
  if (!pts) return 0;

  const double halfW = (double)ext.halfWidth;
  const int offx = clamp_double_to_int(-16.0 * g.sinang * halfW);
  const int offy = clamp_double_to_int( 16.0 * g.cosang * halfW);

  const int xs[2] = { clamp_ll_to_int((long long)wx - offx),
                      clamp_ll_to_int((long long)wx + offx) };
  const int ys[2] = { clamp_ll_to_int((long long)wy - offy),
                      clamp_ll_to_int((long long)wy + offy) };
  const int zs[2] = { ext.topZ, ext.bottomZ };

  static const int cc[4] = { 0, 1, 1, 0 };
  static const int rr[4] = { 0, 0, 1, 1 };

  int got = 0;
  for (int k = 0; k < 4; k++) {
    pts[k] = project_world(cam, g, sintable, xs[cc[k]], ys[cc[k]], zs[rr[k]],
                           dbgs ? &dbgs[k] : nullptr);
    if (pts[k].visible) got++;
  }
  return got;
}

ScreenBox project_sprite_box(const CamState& cam, const GProj& g, const short* sintable,
                             int wx, int wy, const SpriteExtents& ext) {
  ScreenBox b = { 0, 0, 0, 0, false, 0 };

  // Four corners through the one calibrated projection, so the box cannot drift from the marker. Horizontal extent is a
  // union over both rows, since drawpoly's pitch rotation makes x depend on vertical position too.
  ScreenPt pts[4];
  const int got = project_sprite_corners(cam, g, sintable, wx, wy, ext, pts, nullptr);
  long long minX = 0, maxX = 0, minY = 0, maxY = 0;
  bool first = true;
  for (int k = 0; k < 4; k++) {
    const ScreenPt& p = pts[k];
    if (!p.visible) continue;
    if (first) { minX = maxX = p.x; minY = maxY = p.y; first = false; continue; }
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }

  // All four corners or nothing: the top pair can be rejected alone by the pitched clip, and min/max over survivors once
  // gave full width with zero height (a six-pixel streak at the feet). A partial set draws proj_anchor_box instead.
  b.corners = got;
  if (!proj_box_corners_ok(got)) { b.x = b.y = b.w = b.h = 0; b.valid = false; return b; }

  long long w = maxX - minX;
  long long h = maxY - minY;
  if (w < PROJ_BOX_MIN_W) w = PROJ_BOX_MIN_W;
  if (w > PROJ_BOX_MAX)   w = PROJ_BOX_MAX;
  if (h < PROJ_BOX_MIN_H) h = PROJ_BOX_MIN_H;
  if (h > PROJ_BOX_MAX)   h = PROJ_BOX_MAX;

  const long long cx = (minX + maxX) / 2;
  b.x = clamp_int(clamp_ll_to_int(cx - w / 2), -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.y = clamp_int(clamp_ll_to_int(maxY - h),   -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.w = (int)w;
  b.h = (int)h;
  b.valid = true;
  return b;
}
