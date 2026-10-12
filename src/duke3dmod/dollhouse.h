#pragma once
// Cutaway view: paints the map from outside by depth instead of the renderer's column claims. hooked_domost paints each
// surface as its own polygon through drawpoly and claims nothing; hooked_drawpoly lets only the sky's own paints through.

const float DOLLHOUSE_SOLID_LINE = -10000.0f;

struct DollhouseState {
  float sx0, sx1;        // the wall span the floor claim opened
  float cy0, cy1;        // the span's ceiling claim line, in floor order
  float fy0, fy1;        // the span's floor line, when a record match sets it
  int floorSkipped, ceilSkipped, ceilCalls;
  int rec;               // the wall record this span matched, -1 for none
  // Under a parallax ceiling the engine stops a wall at the sky texture's top edge, carried as a flat line; the largest
  // such y on this span bounds the wall and top step.
  int skyTopSet; float skyTop;
};
inline DollhouseState dollhouse_state_reset() {
  DollhouseState s = { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, -1, 0, 0.0f };
  return s;
}
inline void dollhouse_note_sky_top(DollhouseState* s, float x0, float y0, float x1, float y1) {
  if (!s || x0 <= x1 || y0 != y1) return;
  if (x1 > s->sx1 || x0 < s->sx0) return;
  if (!s->skyTopSet || y0 > s->skyTop) { s->skyTop = y0; s->skyTopSet = 1; }
}
inline double dollhouse_sky_clamped(double cy, const DollhouseState* s) {
  return (s && s->skyTopSet && (double)s->skyTop > cy) ? (double)s->skyTop : cy;
}
// The cutaway claims nothing (closing columns would hide the rooms beyond) and lets the GL depth buffer sort. drawpoly (VA
// 0x00475190) silently returns on the wrong winding (tests at VA 0x0047526D, 0x00475307), so a failing polygon is reversed.
inline int dollhouse_poly_accepted(const double* px, const double* py, int n) {
  if (!px || !py || n < 3) return 0;
  if (n == 3)
    return (px[2] - px[1]) * (py[0] - py[1]) > (px[0] - px[1]) * (py[2] - py[1]) ? 1 : 0;
  double f = 0.0;
  for (int i = n - 2, j = n - 1, k = 0; k < n; i = j, j = k, k++)
    f += (px[i] - px[k]) * py[j];
  return f > 0.0 ? 1 : 0;
}
inline int dollhouse_poly_fix_winding(double* px, double* py, int n) {
  if (dollhouse_poly_accepted(px, py, n)) return 1;
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    const double tx = px[i]; px[i] = px[j]; px[j] = tx;
    const double ty = py[i]; py[i] = py[j]; py[j] = ty;
  }
  return dollhouse_poly_accepted(px, py, n);
}

// A polygon on its way to drawpoly; world xy rides with each vertex so a slope's z can be evaluated at a clipped vertex.
const int DOLLHOUSE_POLY_MAX = 8;   // drawpoly's arrays hold 16; a triangle through all three clips reaches 7
struct DollhouseVert { double wx, wy, vx, depth, sx, sy; };
struct DollhousePoly { int n; DollhouseVert v[DOLLHOUSE_POLY_MAX]; };

inline DollhouseVert dollhouse_vert_lerp(const DollhouseVert* a, const DollhouseVert* b, double t) {
  DollhouseVert r;
  r.wx = a->wx + (b->wx - a->wx) * t;
  r.wy = a->wy + (b->wy - a->wy) * t;
  r.vx = a->vx + (b->vx - a->vx) * t;
  r.depth = a->depth + (b->depth - a->depth) * t;
  r.sx = a->sx + (b->sx - a->sx) * t;
  r.sy = a->sy + (b->sy - a->sy) * t;
  return r;
}

inline void dollhouse_view_xform(DollhouseVert* v, double dx, double dy,
                                 double gcosang, double gsinang,
                                 double gcosang2, double gsinang2) {
  v->vx = dy * gcosang - dx * gsinang;
  v->depth = dy * gsinang2 + dx * gcosang2;
}
inline void dollhouse_project_vert(DollhouseVert* v, double dz,
                                   double ghalfx, double ghoriz, double gyxscale) {
  v->sx = ghalfx * v->vx / v->depth + ghalfx;
  v->sy = dz * gyxscale / v->depth + ghoriz;
}

// Sutherland-Hodgman against the view-space near plane, depth >= 1; every field rides along.
inline DollhousePoly dollhouse_near_clip(const DollhousePoly* in) {
  DollhousePoly out; out.n = 0;
  if (!in || in->n < 3 || in->n > DOLLHOUSE_POLY_MAX) return out;
  for (int i = 0; i < in->n; i++) {
    const DollhouseVert* a = &in->v[i];
    const DollhouseVert* b = &in->v[(i + 1) % in->n];
    const double da = a->depth - 1.0, db = b->depth - 1.0;
    if (da >= 0.0) { if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; } out.v[out.n++] = *a; }
    if ((da < 0.0) != (db < 0.0)) {
      if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; }
      out.v[out.n++] = dollhouse_vert_lerp(a, b, da / (da - db));
    }
  }
  if (out.n < 3) out.n = 0;
  return out;
}

// drawpoly does not clip against the tilted near plane in GL mode (depth is ghalfx*gchang + (sy - ghoriz)*gshang; under 1
// draws garbage): the same clip, in screen space.
inline double dollhouse_screen_depth(double sy, double ghalfx, double ghoriz,
                                     double gchang, double gshang) {
  return ghalfx * gchang + (sy - ghoriz) * gshang;
}
inline DollhousePoly dollhouse_tilt_clip(const DollhousePoly* in, double ghalfx, double ghoriz,
                                         double gchang, double gshang) {
  DollhousePoly out; out.n = 0;
  if (!in || in->n < 3 || in->n > DOLLHOUSE_POLY_MAX) return out;
  for (int i = 0; i < in->n; i++) {
    const DollhouseVert* a = &in->v[i];
    const DollhouseVert* b = &in->v[(i + 1) % in->n];
    const double da = dollhouse_screen_depth(a->sy, ghalfx, ghoriz, gchang, gshang) - 1.0;
    const double db = dollhouse_screen_depth(b->sy, ghalfx, ghoriz, gchang, gshang) - 1.0;
    if (da >= 0.0) { if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; } out.v[out.n++] = *a; }
    if ((da < 0.0) != (db < 0.0)) {
      if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; }
      out.v[out.n++] = dollhouse_vert_lerp(a, b, da / (da - db));
    }
  }
  if (out.n < 3) out.n = 0;
  return out;
}

// What one domost call stands for while the cutaway paints. A quad carries its two lines in floor order (x ascending).
enum { DH_KIND_NONE = 0, DH_KIND_FLOOR = 1, DH_KIND_CEIL = 2,
       DH_KIND_WALL = 3, DH_KIND_TOPSTEP = 4, DH_KIND_BOTSTEP = 5 };
struct DollhouseDepthCall { int kind; float ay0, ay1, by0, by1; };

inline DollhouseDepthCall dollhouse_depth_call(DollhouseState* s, float x0, float y0,
                                               float x1, float y1) {
  DollhouseDepthCall r = { DH_KIND_NONE, 0.0f, 0.0f, 0.0f, 0.0f };
  if (!s) return r;
  const int floorSide = x0 < x1;
  // A solid wall's call is its own span: under a parallax floor the sky's sub-span calls have clobbered the span.
  if (floorSide && y0 == DOLLHOUSE_SOLID_LINE && y1 == DOLLHOUSE_SOLID_LINE) {
    if (x0 != s->sx0 || x1 != s->sx1) { s->sx0 = x0; s->sx1 = x1; s->rec = -1; s->ceilCalls = 0; }
    r.kind = DH_KIND_WALL; r.ay0 = s->cy0; r.ay1 = s->cy1; r.by0 = s->fy0; r.by1 = s->fy1;
    return r;
  }
  if (floorSide && (x0 != s->sx0 || x1 != s->sx1)) {
    s->sx0 = x0; s->sx1 = x1;
    s->fy0 = y0; s->fy1 = y1;
    s->cy0 = 0.0f; s->cy1 = 0.0f;
    s->floorSkipped = 0; s->ceilSkipped = 0; s->ceilCalls = 0;
    s->rec = -1;
    r.kind = DH_KIND_FLOOR; r.ay0 = y0; r.ay1 = y1;
    return r;
  }
  if (floorSide ? (x0 != s->sx0 || x1 != s->sx1) : (x1 != s->sx0 || x0 != s->sx1)) return r;
  if (!floorSide) {
    s->ceilCalls++;
    if (s->ceilCalls == 1) {
      s->cy0 = y1; s->cy1 = y0;
      r.kind = DH_KIND_CEIL; r.ay0 = y1; r.ay1 = y0;
      return r;
    }
    r.kind = DH_KIND_TOPSTEP; r.ay0 = s->cy0; r.ay1 = s->cy1; r.by0 = y1; r.by1 = y0;
    return r;
  }
  r.kind = DH_KIND_BOTSTEP; r.ay0 = y0; r.ay1 = y1; r.by0 = s->fy0; r.by1 = s->fy1;
  return r;
}
// Screen y grows downward, so a quad's lower line must be below its upper at both ends; crossed, it paints nothing.
inline int dollhouse_quad_empty(double ay0, double ay1, double by0, double by1) {
  return (by0 <= ay0 || by1 <= ay1) ? 1 : 0;
}

// A wall's lines come from the wall, not the call: under a parallax ceiling the calls over its span carry the sky's edge
// line and split points, so each bunch wall is projected up front and calls are matched to it. One record per wall,
// floor order; the n-lines are the next sector's floor/ceiling, bounding the steps.
struct DollhouseWallRec {
  float sx0, sx1;
  double fy0, fy1, cy0, cy1;
  double nfy0, nfy1, ncy0, ncy1;  // the next sector's, when twoSided
  int twoSided;
};

// The call's span against every record: nearest wins; a miss of a whole pixel or more is no match.
inline int dollhouse_match_record(const DollhouseWallRec* recs, int n, float x0, float x1) {
  if (!recs || n <= 0) return -1;
  int best = -1;
  double bestd = 0.0;
  for (int i = 0; i < n; i++) {
    const double a = (double)recs[i].sx0 - (double)x0;
    const double b = (double)recs[i].sx1 - (double)x1;
    const double d = (a < 0.0 ? -a : a) + (b < 0.0 ? -b : b);
    if (best < 0 || d < bestd) { best = i; bestd = d; }
  }
  return bestd < 1.0 ? best : -1;
}

struct DollhouseQuad { int ok; double ay0, ay1, by0, by1; };
inline DollhouseQuad dollhouse_record_quad(const DollhouseWallRec* r, int kind) {
  DollhouseQuad q = { 0, 0.0, 0.0, 0.0, 0.0 };
  if (!r) return q;
  if (kind == DH_KIND_WALL) {
    q.ay0 = r->cy0; q.ay1 = r->cy1; q.by0 = r->fy0; q.by1 = r->fy1;
  } else if (kind == DH_KIND_TOPSTEP) {
    if (!r->twoSided) return q;
    q.ay0 = r->cy0; q.ay1 = r->cy1; q.by0 = r->ncy0; q.by1 = r->ncy1;
  } else if (kind == DH_KIND_BOTSTEP) {
    if (!r->twoSided) return q;
    q.ay0 = r->nfy0; q.ay1 = r->nfy1; q.by0 = r->fy0; q.by1 = r->fy1;
  } else {
    return q;
  }
  q.ok = 1;
  return q;
}

// One wall segment against the view-space near plane, world xy interpolated so a clipped end's slope z is askable.
inline int dollhouse_clip_seg(DollhouseVert* a, DollhouseVert* b) {
  if (!a || !b) return 0;
  const double da = a->depth - 1.0, db = b->depth - 1.0;
  if (da < 0.0 && db < 0.0) return 0;
  if (da < 0.0) *a = dollhouse_vert_lerp(a, b, da / (da - db));
  else if (db < 0.0) *b = dollhouse_vert_lerp(b, a, db / (db - da));
  return 1;
}

// A sky band or split owns only part of a span, so its footprint is cut to the band.
inline DollhousePoly dollhouse_xclip_half(const DollhousePoly* in, double bound, int keepAbove) {
  DollhousePoly out; out.n = 0;
  if (!in || in->n < 3 || in->n > DOLLHOUSE_POLY_MAX) return out;
  for (int i = 0; i < in->n; i++) {
    const DollhouseVert* p = &in->v[i];
    const DollhouseVert* q = &in->v[(i + 1) % in->n];
    const double dp = keepAbove ? p->sx - bound : bound - p->sx;
    const double dq = keepAbove ? q->sx - bound : bound - q->sx;
    if (dp >= 0.0) { if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; } out.v[out.n++] = *p; }
    if ((dp < 0.0) != (dq < 0.0)) {
      if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; }
      out.v[out.n++] = dollhouse_vert_lerp(p, q, dp / (dp - dq));
    }
  }
  if (out.n < 3) out.n = 0;
  return out;
}
inline DollhousePoly dollhouse_line_clip(const DollhousePoly* in, double a, double b, double c) {
  DollhousePoly out; out.n = 0;
  if (!in || in->n < 3 || in->n > DOLLHOUSE_POLY_MAX) return out;
  for (int i = 0; i < in->n; i++) {
    const DollhouseVert* p = &in->v[i];
    const DollhouseVert* q = &in->v[(i + 1) % in->n];
    const double dp = a * p->sx + b * p->sy + c;
    const double dq = a * q->sx + b * q->sy + c;
    if (dp >= 0.0) { if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; } out.v[out.n++] = *p; }
    if ((dp < 0.0) != (dq < 0.0)) {
      if (out.n >= DOLLHOUSE_POLY_MAX) { out.n = 0; return out; }
      out.v[out.n++] = dollhouse_vert_lerp(p, q, dp / (dp - dq));
    }
  }
  if (out.n < 3) out.n = 0;
  return out;
}
// The span authority is the wall record, not the call: a floor-side call matching a record opens that wall's span; any
// other off-span call is a sky band, split or stray and never moves it. `rec` is the floor-side match, else -1.
inline DollhouseDepthCall dollhouse_depth_call_rec(DollhouseState* s, int rec, float x0, float y0,
                                                   float x1, float y1) {
  DollhouseDepthCall r = { DH_KIND_NONE, 0.0f, 0.0f, 0.0f, 0.0f };
  if (!s) return r;
  const int floorSide = x0 < x1;
  const int same = floorSide ? (x0 == s->sx0 && x1 == s->sx1) : (x1 == s->sx0 && x0 == s->sx1);
  if (floorSide && y0 == DOLLHOUSE_SOLID_LINE && y1 == DOLLHOUSE_SOLID_LINE) {
    if (!same) { s->sx0 = x0; s->sx1 = x1; s->rec = rec; s->ceilCalls = 0; s->skyTopSet = 0; }
    r.kind = DH_KIND_WALL;
    return r;
  }
  if (floorSide && rec >= 0 && !same) {
    s->sx0 = x0; s->sx1 = x1; s->fy0 = y0; s->fy1 = y1; s->cy0 = 0.0f; s->cy1 = 0.0f;
    s->floorSkipped = 0; s->ceilSkipped = 0; s->ceilCalls = 0; s->rec = rec;
    s->skyTopSet = 0; s->skyTop = 0.0f;
    r.kind = DH_KIND_FLOOR; r.ay0 = y0; r.ay1 = y1;
    return r;
  }
  if (!same) return r;
  if (!floorSide) {
    s->ceilCalls++;
    if (s->ceilCalls == 1) { s->cy0 = y1; s->cy1 = y0; r.kind = DH_KIND_CEIL; r.ay0 = y1; r.ay1 = y0; return r; }
    r.kind = DH_KIND_TOPSTEP; r.ay0 = s->cy0; r.ay1 = s->cy1; r.by0 = y1; r.by1 = y0;
    return r;
  }
  r.kind = DH_KIND_BOTSTEP; r.ay0 = y0; r.ay1 = y1; r.by0 = s->fy0; r.by1 = s->fy1;
  return r;
}
// Only a sloped plane (floorstat/ceilingstat bit 1) varies in height across the footprint; flat saves three slope calls.
inline int dollhouse_plane_sloped(int stat) { return (stat & 2) != 0 ? 1 : 0; }

inline DollhousePoly dollhouse_xspan_clip(const DollhousePoly* in, double xlo, double xhi) {
  const DollhousePoly a = dollhouse_xclip_half(in, xlo, 1);
  if (a.n < 3) { DollhousePoly e; e.n = 0; return e; }
  return dollhouse_xclip_half(&a, xhi, 0);
}

// Sector loops as polymost_scansector walks them: a loop starts at wallptr or wherever wall[w-1].point2 does not point at
// w; vertices follow point2. One contour per loop for the footprint tessellation.
inline int dollhouse_loop_start(const short* point2, int wallptr, int wallnum, int i) {
  if (!point2 || i < 0 || i >= wallnum) return 0;
  if (i == 0) return 1;
  return point2[i - 1] != wallptr + i ? 1 : 0;
}
inline int dollhouse_loop_walls(const short* point2, int wallptr, int wallnum, int i,
                                short* out, int outMax) {
  if (!point2 || !out || outMax <= 0 || i < 0 || i >= wallnum) return 0;
  int w = wallptr + i, count = 0;
  for (;;) {
    const int local = w - wallptr;
    if (local < 0 || local >= wallnum) return 0;
    if (count >= outMax || count > wallnum) return 0;
    out[count++] = (short)w;
    w = point2[local];
    if (w == wallptr + i) break;
  }
  return count >= 3 ? count : 0;
}

#ifndef NO_GL
bool dollhouse_install();
void dollhouse_frame_begin();  // forgets which planes the cutaway painted this frame
// Whether the seed scan should be dropped: the mirror pass in cutaway mode. A mirror makes the game draw the reflected
// world first; with claims dropped the main pass no longer covers it, so the two blend.
bool dollhouse_skip_seed(bool inMirror);

struct DollhouseFrameStats { int calls, drawn, sky, solid, top, bottom, recs; };
DollhouseFrameStats dollhouse_frame_stats();
#endif
