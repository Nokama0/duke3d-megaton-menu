#include "dollhouse.h"
#include "common.h"
#include "config.h"
#include "engine.h"
#include "freecam.h"   // camera_render_armed: the frame draws the camera's view
#include "hooks.h"
#include "log.h"
#include "trigger.h"   // trigger_map_signature: the footprint cache's key
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

// The domost thunk hook. Runs 45 times per wall of every bunch, so it only reads one global and the rule while the
// camera's view is drawn.
typedef void (__cdecl* DomostFn)(float, float, float, float);
typedef void (__cdecl* DrawallsFn)(long);
typedef long (__cdecl* ZofSlopeFn)(short, long, long);
typedef void (__cdecl* DrawpolyFn)(double*, double*, long, long);
static DomostFn s_orig = 0;
static DrawallsFn s_origDrawalls = 0, s_origDrawallsMirror = 0;
static ZofSlopeFn s_florz = 0, s_ceilz = 0;   // set only when both fingerprints match
static DrawpolyFn s_drawpoly = 0;             // set only when the body fingerprint matches
static DrawpolyFn s_drawpolyOrig = 0;         // the thunk's original target, for hooked_drawpoly
static int s_seedSkipped = 0;                 // this pass's seed scan was dropped (mirror)
static int s_curSector = -1;                  // the sector of the bunch drawalls is drawing
static DollhouseState s_state;
static unsigned s_drawn = 0, s_sky = 0;
static DWORD s_lastLog = 0;
static unsigned char s_floorDone[rva::MAX_SECTORS / 8];
static unsigned char s_ceilDone[rva::MAX_SECTORS / 8];

const int DH_WALLREC_MAX = 256;
static DollhouseWallRec s_rec[DH_WALLREC_MAX];
static int s_recCount = 0;
static int s_recCapLogged = 0;

static DollhouseFrameStats s_cur, s_last;
static int s_prevMirror = 0;

// Footprint cache: the cutaway paints floors and ceilings as their own polygons, so it needs each sector's footprint as
// triangles. The GLU tessellator does that on the CPU without a GL context, so the whole map is tessellated once on the
// render clock at first sight (sectors are concave, hold holes and loops; none of it changes until the map does).
const int DH_TRI_MAX = 200000;     // triangles over the whole map
const int DH_LOOP_VERTS = 2048;    // vertex slots for one sector's loops
const int DH_COMBINE_VERTS = 256;  // extra slots for vertices the tessellator invents

struct DhSectorTris { int off, count; };
static double* s_tri = 0;          // 6 doubles a triangle: world x,y per vertex
static int s_triCount = 0, s_triCap = 0;
static DhSectorTris s_sect[rva::MAX_SECTORS];
static unsigned s_triSig = 0;
static int s_triReady = 0, s_triFull = 0;

static double s_vpool[DH_LOOP_VERTS + DH_COMBINE_VERTS][3];
static int s_vpoolUsed = 0;
static short s_point2[DH_LOOP_VERTS];
static short s_loop[DH_LOOP_VERTS];
static double s_triTmp[3][2];
static int s_triTmpN = 0;
static int s_tessFailed = 0;

static int dh_tri_room(int need) {
  if (s_triCount + need <= s_triCap) return 1;
  int cap = s_triCap > 0 ? s_triCap * 2 : 4096;
  while (cap < s_triCount + need) cap *= 2;
  if (cap > DH_TRI_MAX) cap = DH_TRI_MAX;
  if (s_triCount + need > cap) return 0;
  double* p = (double*)realloc(s_tri, (size_t)cap * 6 * sizeof(double));
  if (!p) return 0;
  s_tri = p; s_triCap = cap;
  return 1;
}

typedef void (CALLBACK* GluCb)();
static void CALLBACK dh_tess_begin(GLenum type) {
  s_triTmpN = 0;
  if (type != GL_TRIANGLES) s_tessFailed = 1;   // the edge-flag callback should have forced these
}
static void CALLBACK dh_tess_end(void) { s_triTmpN = 0; }
static void CALLBACK dh_tess_edge_flag(GLboolean) {}
static void CALLBACK dh_tess_error(GLenum) { s_tessFailed = 1; }
static void CALLBACK dh_tess_vertex(void* data) {
  const double* p = (const double*)data;
  if (!p || s_tessFailed || s_triTmpN < 0 || s_triTmpN > 2) return;
  s_triTmp[s_triTmpN][0] = p[0];
  s_triTmp[s_triTmpN][1] = p[1];
  if (++s_triTmpN < 3) return;
  s_triTmpN = 0;
  if (s_triCount >= DH_TRI_MAX || !dh_tri_room(1)) { s_tessFailed = 1; s_triFull = 1; return; }
  double* d = s_tri + (size_t)s_triCount * 6;
  for (int k = 0; k < 3; k++) { d[k * 2] = s_triTmp[k][0]; d[k * 2 + 1] = s_triTmp[k][1]; }
  s_triCount++;
}
static void CALLBACK dh_tess_combine(GLdouble coords[3], void*[4], GLfloat[4], void** out) {
  if (!out) return;
  if (s_vpoolUsed >= DH_LOOP_VERTS + DH_COMBINE_VERTS) { s_tessFailed = 1; *out = s_vpool[0]; return; }
  double* v = s_vpool[s_vpoolUsed++];
  v[0] = coords[0]; v[1] = coords[1]; v[2] = coords[2];
  *out = v;
}

static void tess_sector(GLUtesselator* t, int sect) {
  s_sect[sect].off = 0; s_sect[sect].count = 0;
  int wallptr = 0, wallnum = 0;
  if (!engine_sector_wall_range(sect, &wallptr, &wallnum)) return;
  if (wallnum < 3 || wallnum > DH_LOOP_VERTS) return;
  for (int i = 0; i < wallnum; i++) {
    s_point2[i] = (short)engine_wall_field_s(wallptr + i, rva::WAL_POINT2);
    s_vpool[i][0] = (double)engine_wall_field_l(wallptr + i, WAL_X);
    s_vpool[i][1] = (double)engine_wall_field_l(wallptr + i, WAL_Y);
    s_vpool[i][2] = 0.0;
  }
  s_vpoolUsed = wallnum;
  const int start = s_triCount;
  s_tessFailed = 0; s_triTmpN = 0;
  gluTessBeginPolygon(t, 0);
  for (int i = 0; i < wallnum; i++) {
    if (!dollhouse_loop_start(s_point2, wallptr, wallnum, i)) continue;
    const int n = dollhouse_loop_walls(s_point2, wallptr, wallnum, i, s_loop, DH_LOOP_VERTS);
    if (n < 3) continue;
    gluTessBeginContour(t);
    for (int k = 0; k < n; k++) {
      const int local = s_loop[k] - wallptr;
      if (local < 0 || local >= wallnum) continue;
      gluTessVertex(t, s_vpool[local], s_vpool[local]);
    }
    gluTessEndContour(t);
  }
  gluTessEndPolygon(t);
  if (s_tessFailed) { s_triCount = start; return; }
  s_sect[sect].off = start;
  s_sect[sect].count = s_triCount - start;
}

static void tess_build_map() {
  memset(s_sect, 0, sizeof s_sect);
  s_triCount = 0; s_triFull = 0;
  const int nsect = engine_numsectors();
  if (nsect <= 0) return;
  GLUtesselator* t = gluNewTess();
  if (!t) { LOGE("dollhouse: gluNewTess failed; the cutaway paints no planes"); return; }
  gluTessCallback(t, GLU_TESS_BEGIN, (GluCb)dh_tess_begin);
  gluTessCallback(t, GLU_TESS_VERTEX, (GluCb)dh_tess_vertex);
  gluTessCallback(t, GLU_TESS_END, (GluCb)dh_tess_end);
  gluTessCallback(t, GLU_TESS_ERROR, (GluCb)dh_tess_error);
  gluTessCallback(t, GLU_TESS_EDGE_FLAG, (GluCb)dh_tess_edge_flag);   // forces GL_TRIANGLES only
  gluTessCallback(t, GLU_TESS_COMBINE, (GluCb)dh_tess_combine);
  gluTessProperty(t, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
  gluTessNormal(t, 0.0, 0.0, 1.0);   // the data is flat xy; do not let GLU guess a plane
  int sectors = 0;
  for (int s = 0; s < nsect && s < rva::MAX_SECTORS; s++) {
    if (s_triFull || s_triCount >= DH_TRI_MAX) { s_triFull = 1; break; }
    tess_sector(t, s);
    if (s_sect[s].count > 0) sectors++;
  }
  gluDeleteTess(t);
  LOGI("dollhouse: footprint cache %d triangles over %d/%d sectors%s",
       s_triCount, sectors, nsect, s_triFull ? " -- CAPPED" : "");
}

static void tess_ensure() {
  const unsigned sig = trigger_map_signature(engine_numsectors(), engine_numwalls(), 0);
  if (s_triReady && sig == s_triSig) return;
  s_triSig = sig; s_triReady = 1;
  tess_build_map();
}

void dollhouse_frame_begin() {
  memset(s_floorDone, 0, sizeof s_floorDone);
  memset(s_ceilDone, 0, sizeof s_ceilDone);
  s_state = dollhouse_state_reset();   // a span carried across frames reads as a step
  s_curSector = -1;
  s_recCount = 0;
  s_seedSkipped = 0;
  if (s_prevMirror == 0) s_last = s_cur;
  s_prevMirror = engine_in_prepare_mirror() ? 1 : 0;
  memset(&s_cur, 0, sizeof(s_cur));
  // The cutaway's polygons sort by depth, so the walls pass must depth test: polymost_drawrooms sets GL_ALWAYS above
  // this hook and GL_LEQUAL only after the walls; depth writes are already on.
  if (g_cfg.freecamcutaway != 0 && camera_render_armed()
      && !engine_in_prepare_mirror() && s_drawpoly) {
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LEQUAL);
    tess_ensure();
  }
}
DollhouseFrameStats dollhouse_frame_stats() { return s_last; }

// Cutaway painting. The texture globals (gdx..gvo, globalpicnum, globalshade, pow2xsplit) are set by polymost_drawalls
// just before each domost call and hold for that call only, so a surface is painted inside the hook, at its own call.
struct DhView {
  double gcosang, gsinang, gcosang2, gsinang2, ghalfx, ghoriz, gyxscale, gshang, gchang;
  int posx, posy, posz;
};
static void dh_read_view(DhView* v) {
  v->gcosang  = *(const double*)(g_base + rva::GCOSANG);
  v->gsinang  = *(const double*)(g_base + rva::GSINANG);
  v->gcosang2 = *(const double*)(g_base + rva::GCOSANG2);
  v->gsinang2 = *(const double*)(g_base + rva::GSINANG2);
  v->ghalfx   = *(const double*)(g_base + rva::GHALFX);
  v->ghoriz   = *(const double*)(g_base + rva::GHORIZ);
  v->gyxscale = *(const double*)(g_base + rva::GYXSCALE);
  v->gshang   = *(const double*)(g_base + rva::GSHANG);
  v->gchang   = *(const double*)(g_base + rva::GCHANG);
  v->posx = *(const int*)(g_base + rva::GLOBALPOSX);
  v->posy = *(const int*)(g_base + rva::GLOBALPOSY);
  v->posz = *(const int*)(g_base + rva::GLOBALPOSZ);
}

static void dh_draw(const DollhousePoly* p, int method) {
  if (!s_drawpoly || !p || p->n < 3 || p->n > DOLLHOUSE_POLY_MAX) return;
  double px[DOLLHOUSE_POLY_MAX], py[DOLLHOUSE_POLY_MAX];
  for (int i = 0; i < p->n; i++) { px[i] = p->v[i].sx; py[i] = p->v[i].sy; }
  if (!dollhouse_poly_fix_winding(px, py, p->n)) return;
  s_drawpoly(px, py, (long)p->n, method | 8);   // bit 8 is the detail layer, as domost passes it
  s_drawn++;
  s_cur.drawn++;
}

static void dh_draw_quad(const DhView* v, double x0, double x1,
                         double ay0, double ay1, double by0, double by1, int method) {
  if (dollhouse_quad_empty(ay0, ay1, by0, by1)) return;
  DollhousePoly p;
  memset(&p, 0, sizeof p);
  p.n = 4;   // domost's own order: upper line left to right, then lower line back
  p.v[0].sx = x0; p.v[0].sy = ay0;
  p.v[1].sx = x1; p.v[1].sy = ay1;
  p.v[2].sx = x1; p.v[2].sy = by1;
  p.v[3].sx = x0; p.v[3].sy = by0;
  const DollhousePoly c = dollhouse_tilt_clip(&p, v->ghalfx, v->ghoriz, v->gchang, v->gshang);
  dh_draw(&c, method);
}

static void dh_paint_footprint(const DhView* v, int sect, int isFloor, int method,
                               int clipX, double xlo, double xhi) {
  if (sect < 0 || sect >= rva::MAX_SECTORS || !s_tri) return;
  const DhSectorTris t = s_sect[sect];
  if (t.count <= 0 || t.off < 0 || t.off + t.count > s_triCount) return;
  const ZofSlopeFn zf = isFloor ? s_florz : s_ceilz;
  const int flat = engine_sector_field_l(sect, isFloor ? SEC_FLOORZ : SEC_CEILINGZ);
  const int sloped = dollhouse_plane_sloped(engine_sector_field_s(sect,
                         isFloor ? SEC_FLOORSTAT : SEC_CEILINGSTAT));
  for (int i = 0; i < t.count; i++) {
    const double* d = s_tri + (size_t)(t.off + i) * 6;
    DollhousePoly p;
    memset(&p, 0, sizeof p);
    p.n = 3;
    for (int k = 0; k < 3; k++) {
      p.v[k].wx = d[k * 2];
      p.v[k].wy = d[k * 2 + 1];
      dollhouse_view_xform(&p.v[k], p.v[k].wx - v->posx, p.v[k].wy - v->posy,
                           v->gcosang, v->gsinang, v->gcosang2, v->gsinang2);
    }
    DollhousePoly n = dollhouse_near_clip(&p);
    if (n.n < 3) continue;
    for (int k = 0; k < n.n; k++) {
      const int z = (sloped && zf) ? (int)zf((short)sect, (long)n.v[k].wx, (long)n.v[k].wy) : flat;
      dollhouse_project_vert(&n.v[k], (double)(z - v->posz), v->ghalfx, v->ghoriz, v->gyxscale);
    }
    DollhousePoly c = dollhouse_tilt_clip(&n, v->ghalfx, v->ghoriz, v->gchang, v->gshang);
    if (clipX) c = dollhouse_xspan_clip(&c, xlo, xhi);
    dh_draw(&c, method);
  }
}

static void dh_draw_plane(const DhView* v, int sect, int isFloor, int method) {
  if (sect < 0 || sect >= rva::MAX_SECTORS) return;
  unsigned char* done = isFloor ? s_floorDone : s_ceilDone;
  if (done[sect >> 3] & (1 << (sect & 7))) return;
  done[sect >> 3] |= (unsigned char)(1 << (sect & 7));
  dh_paint_footprint(v, sect, isFloor, method, 0, 0.0, 0.0);
}

// The engine's sky is a fake plane 32 m above the camera and one piece is a floor-side call that paints down into
// everything still open (the whole screen: black sky and ground bands). So a sky call paints this sector's own
// footprint over the call's columns. No done bitmap: each band owns different columns.
static void dh_draw_sky(const DhView* v, int sect, int isFloor, float x0, float x1, int method) {
  if (sect < 0 || sect >= rva::MAX_SECTORS) return;
  const ZofSlopeFn zf = isFloor ? s_florz : s_ceilz;
  const int zc = zf ? (int)zf((short)sect, (long)v->posx, (long)v->posy)
                    : engine_sector_field_l(sect, isFloor ? SEC_FLOORZ : SEC_CEILINGZ);
  if (isFloor ? v->posz >= zc : v->posz <= zc) return;
  const double lo = x0 < x1 ? (double)x0 : (double)x1;
  const double hi = x0 < x1 ? (double)x1 : (double)x0;
  s_sky++; s_cur.sky++;
  dh_paint_footprint(v, sect, isFloor, method, 1, lo, hi);
}

// A sky band as a screen strip over the call's columns, cut to where the band's mapping has positive depth and pinned
// to the far end of the depth range so any surface beats it (the sky is a backdrop, not a plane; a footprint left black
// above every roof). One domost call claims nothing: the surface it stood for is painted instead.
static int s_lastKind = DH_KIND_NONE;
static void depth_call(int pm, float x0, float y0, float x1, float y1) {
  const int recNow = (x0 < x1) ? dollhouse_match_record(s_rec, s_recCount, x0, x1) : -1;
  const DollhouseDepthCall c = dollhouse_depth_call_rec(&s_state, recNow, x0, y0, x1, y1);
  s_lastKind = c.kind;   // hooked_domost reads it to classify the engine's own paints
  if (pm == -1) return;   // a back-facing plane paints nothing, here as in the engine
  DhView v;
  dh_read_view(&v);
  if (v.ghalfx <= 0.0) return;
  const int isFloorSide = x0 < x1 ? 1 : 0;
  const int parallax = (*(const int*)(g_base + rva::GLOBALORIENTATION) & 1) != 0;
  // A call off every span is a parallax band or sub-span split. The engine paints those (hooked_drawpoly lets sky
  // through); the band's flat line is still noted, since it bounds a wall under a sky ceiling.
  if (c.kind == DH_KIND_NONE) {
    if (parallax) dollhouse_note_sky_top(&s_state, x0, y0, x1, y1);
    return;
  }
  if (parallax && (c.kind == DH_KIND_FLOOR || c.kind == DH_KIND_CEIL)) {
    dollhouse_note_sky_top(&s_state, x0, y0, x1, y1);
    return;   // the engine's own sky, not a footprint of mine
  }
  if (c.kind == DH_KIND_FLOOR || c.kind == DH_KIND_CEIL) {
    dh_draw_plane(&v, s_curSector, isFloorSide, pm);
    return;
  }
  if (s_state.rec < 0 || s_state.rec >= s_recCount) return;
  const DollhouseWallRec* r = &s_rec[s_state.rec];
  DollhouseQuad q = dollhouse_record_quad(r, c.kind);
  if (!q.ok) return;
  if (c.kind == DH_KIND_WALL || c.kind == DH_KIND_TOPSTEP) {
    q.ay0 = dollhouse_sky_clamped(q.ay0, &s_state);
    q.ay1 = dollhouse_sky_clamped(q.ay1, &s_state);
  }
  if (c.kind == DH_KIND_WALL) s_cur.solid++; else if (c.kind == DH_KIND_TOPSTEP) s_cur.top++; else s_cur.bottom++;
  dh_draw_quad(&v, r->sx0, r->sx1, q.ay0, q.ay1, q.by0, q.by1, pm);
}

bool dollhouse_skip_seed(bool inMirror) {
  const bool skip = inMirror && g_cfg.freecamcutaway != 0 && camera_render_armed();
  if (skip) s_seedSkipped = 1;
  return skip;
}

// The mirror path's drawalls(0): nothing to draw once its seed was dropped, and bunchfirst[0] would be stale.
static void __cdecl hooked_polymost_drawalls_mirror(long bunch) {
  s_curSector = -1; s_recCount = 0;   // never the main pass's records
  if (s_seedSkipped) return;
  if (s_origDrawallsMirror) s_origDrawallsMirror(bunch);
}

static int bunch_sector(long bunch) {
  if (!engine_verified() || bunch < 0 || bunch >= rva::MAXWALLSB) return -1;
  const int z = *(const short*)(g_base + rva::BUNCHFIRST + (unsigned)bunch * 2);
  if (z < 0 || z >= rva::MAXWALLSB) return -1;
  const int sect = *(const short*)(g_base + rva::THESECTOR + (unsigned)z * 2);
  if (sect < 0 || sect >= engine_numsectors() || sect >= rva::MAX_SECTORS) return -1;
  return sect;
}

static double dh_wall_line(const DhView* v, ZofSlopeFn zf, int sect,
                           const DollhouseVert* p, unsigned flatOff) {
  const int z = zf ? (int)zf((short)sect, (long)p->wx, (long)p->wy)
                   : engine_sector_field_l(sect, flatOff);
  return (double)(z - v->posz) * v->gyxscale / p->depth + v->ghoriz;
}

// Every wall of the bunch, projected as polymost_drawalls does, so a later call can be matched to its wall. Under a
// parallax ceiling a wall's calls carry the sky's texture-edge line and split points, never the wall's top, so those
// lines are unusable.
static void build_wall_recs(long bunch) {
  s_recCount = 0;
  if (s_curSector < 0 || !s_drawpoly || bunch < 0 || bunch >= rva::MAXWALLSB) return;
  const int numwalls = engine_numwalls();
  const int numsect = engine_numsectors();
  if (numwalls <= 0 || numsect <= 0) return;
  DhView v;
  dh_read_view(&v);
  if (v.ghalfx <= 0.0) return;
  int z = *(const short*)(g_base + rva::BUNCHFIRST + (unsigned)bunch * 2);
  for (int guard = 0; guard < rva::MAXWALLSB; guard++) {
    if (z < 0 || z >= rva::MAXWALLSB) break;
    const int w = *(const short*)(g_base + rva::THEWALL + (unsigned)z * 2);
    z = *(const short*)(g_base + rva::P2 + (unsigned)z * 2);
    if (w < 0 || w >= numwalls) continue;
    const int p2w = engine_wall_field_s(w, rva::WAL_POINT2);
    if (p2w < 0 || p2w >= numwalls) continue;
    DollhouseVert a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.wx = (double)engine_wall_field_l(w, WAL_X);
    a.wy = (double)engine_wall_field_l(w, WAL_Y);
    b.wx = (double)engine_wall_field_l(p2w, WAL_X);
    b.wy = (double)engine_wall_field_l(p2w, WAL_Y);
    dollhouse_view_xform(&a, a.wx - v.posx, a.wy - v.posy,
                         v.gcosang, v.gsinang, v.gcosang2, v.gsinang2);
    dollhouse_view_xform(&b, b.wx - v.posx, b.wy - v.posy,
                         v.gcosang, v.gsinang, v.gcosang2, v.gsinang2);
    if (!dollhouse_clip_seg(&a, &b)) continue;
    const double sxa = v.ghalfx * a.vx / a.depth + v.ghalfx;
    const double sxb = v.ghalfx * b.vx / b.depth + v.ghalfx;
    if (sxa >= sxb) continue;   // domost names a span in floor order only
    if (s_recCount >= DH_WALLREC_MAX) {
      if (!s_recCapLogged) {
        s_recCapLogged = 1;
        LOGE("dollhouse: a bunch holds over %d walls; the rest paint no pieces", DH_WALLREC_MAX);
      }
      break;
    }
    DollhouseWallRec* r = &s_rec[s_recCount++];
    r->sx0 = (float)sxa; r->sx1 = (float)sxb;
    r->fy0 = dh_wall_line(&v, s_florz, s_curSector, &a, SEC_FLOORZ);
    r->fy1 = dh_wall_line(&v, s_florz, s_curSector, &b, SEC_FLOORZ);
    r->cy0 = dh_wall_line(&v, s_ceilz, s_curSector, &a, SEC_CEILINGZ);
    r->cy1 = dh_wall_line(&v, s_ceilz, s_curSector, &b, SEC_CEILINGZ);
    const int ns = engine_wall_field_s(w, rva::WAL_NEXTSECTOR);
    const int cstat = engine_wall_field_s(w, rva::WAL_CSTAT);
    r->twoSided = (ns >= 0 && ns < numsect && ns < rva::MAX_SECTORS && (cstat & 32) == 0) ? 1 : 0;
    r->nfy0 = r->nfy1 = r->ncy0 = r->ncy1 = 0.0;
    if (r->twoSided) {
      r->nfy0 = dh_wall_line(&v, s_florz, ns, &a, SEC_FLOORZ);
      r->nfy1 = dh_wall_line(&v, s_florz, ns, &b, SEC_FLOORZ);
      r->ncy0 = dh_wall_line(&v, s_ceilz, ns, &a, SEC_CEILINGZ);
      r->ncy1 = dh_wall_line(&v, s_ceilz, ns, &b, SEC_CEILINGZ);
    }
  }
}

static void __cdecl hooked_polymost_drawalls(long bunch) {
  s_curSector = bunch_sector(bunch);
  if (g_cfg.freecamcutaway != 0 && camera_render_armed()
      && !engine_in_prepare_mirror()) {
    s_state = dollhouse_state_reset();
    build_wall_recs(bunch);
    s_cur.recs += s_recCount;
  } else {
    s_recCount = 0;
  }
  if (s_origDrawalls) s_origDrawalls(bunch);
}

// The paint hook. The engine's claim machinery must run untouched (it computes the sky as in gameplay) while wall and
// plane painting becomes depth-sorted polygons: domost claims, drawpoly paints. So every domost call reaches the
// original and this hook drops its non-sky paints. s_inDomost keeps sprites, masked walls and rotatesprite out.
static int s_inDomost = 0;
static int s_skyCall = 0;    // the call now inside the original is a parallax plane's
static void __cdecl hooked_drawpoly(double* px, double* py, long n, long method) {
  if (!s_drawpolyOrig) return;
  if (s_inDomost) {
    // A paint is the sky by the call it came from: a plane call whose sector has the parallax bit. skyclamphack and
    // drawingskybox belong to the hightile skybox path alone (both 0 for the classic sky), so keying on them painted
    // nothing.
    const int sky = s_skyCall
                    || (*(const int*)(g_base + rva::SKYCLAMPHACK) != 0)
                    || (*(const int*)(g_base + rva::DRAWINGSKYBOX) != 0);
    if (!sky) return;
    s_sky++; s_cur.sky++;
    // The engine's own polygon, mapping and texture state, only pinned to the far plane so a backdrop cannot occlude
    // the cutaway's geometry. Nothing about the sky is computed here: a screen-sized quad of our own ran past the sky
    // band and ended in nothing.
    glDepthRange(1.0, 1.0);
    s_drawpolyOrig(px, py, n, method);
    glDepthRange(1e-5, 1.0);   // polymost_drawrooms' own range for the walls pass
    return;
  }
  s_drawpolyOrig(px, py, n, method);
}

static void __cdecl hooked_domost(float x0, float y0, float x1, float y1) {
  if (!s_orig) return;
  if (g_cfg.freecamcutaway != 0 && camera_render_armed() && !engine_in_prepare_mirror()) {
    const int pm = *(const int*)(g_base + rva::DOMOSTPOLYMETHOD);
    s_cur.calls++;
    if (g_cfg.debuglog != 0) {
      const DWORD now = GetTickCount();
      if (now - s_lastLog >= 1000) {
        s_lastLog = now;
        LOGI("dollhouse: drawn=%u sky=%u", s_drawn, s_sky);
      }
    }
    // Claims nothing: the original never runs for this paint. The engine's fake sky plane would paint down into
    // everything still open, i.e. the whole screen (black sky, black ground bands).
    s_lastKind = DH_KIND_NONE;
    depth_call(pm, x0, y0, x1, y1);
    // Then the original, so claims evolve as in gameplay and the engine draws its sky; hooked_drawpoly drops the rest.
    // A plane call under a parallax ceiling or floor is the sky's own.
    const int planeCall = (s_lastKind == DH_KIND_NONE || s_lastKind == DH_KIND_FLOOR
                           || s_lastKind == DH_KIND_CEIL);
    s_skyCall = planeCall && g_cfg.freecamcutawaysky != 0
                && (*(const int*)(g_base + rva::GLOBALORIENTATION) & 1) != 0;
    // Only the sky's own calls reach the engine: nothing else claims a column, so the sky is bounded by its own bands,
    // as in gameplay, not by wall and floor claims (which describe occlusion from inside a room and leave slivers from
    // outside). Its paints are pinned to the far plane. A reopen per call cost frame rate, so it was rejected.
    if (s_skyCall) {
      s_inDomost = 1;
      s_orig(x0, y0, x1, y1);
      s_inDomost = 0;
      s_skyCall = 0;
    }
    return;
  }
  s_orig(x0, y0, x1, y1);
}

bool dollhouse_install() {
  s_state = dollhouse_state_reset();
  void* orig = 0;
  const bool ok = hook_jmp_thunk(rva::THUNK_DOMOST_RVA, rva::THUNK_DOMOST_BYTES,
                                 (void*)&hooked_domost, &orig);
  if (ok) {
    s_orig = (DomostFn)orig;
    const unsigned origRva = (unsigned)((unsigned char*)orig - g_base);
    LOGI("dollhouse: domost thunk hooked; recovered body rva=%08X (expect %08X)%s",
         origRva, rva::DOMOST_BODY_RVA,
         origRva == rva::DOMOST_BODY_RVA ? "" : " -- MISMATCH");
  } else {
    LOGE("dollhouse: domost thunk did not fingerprint-match; freecamcutaway stays inert");
  }
  void* origDraw = 0;
  if (ok && hook_call_site(rva::DRAWALLS_CALL_SITE_RVA, rva::DRAWALLS_CALL_SITE_BYTES,
                           (void*)&hooked_polymost_drawalls, &origDraw)) {
    s_origDrawalls = (DrawallsFn)origDraw;
    const unsigned origRva = (unsigned)((unsigned char*)origDraw - g_base);
    LOGI("dollhouse: drawalls call site redirected; recovered body rva=%08X (expect %08X)%s",
         origRva, rva::POLYMOST_DRAWALLS_BODY_RVA,
         origRva == rva::POLYMOST_DRAWALLS_BODY_RVA ? "" : " -- MISMATCH");
  } else if (ok) {
    LOGE("dollhouse: drawalls call site did not fingerprint-match; the cutaway cannot name a bunch's sector");
  }
  void* origMirror = 0;
  if (ok && hook_call_site(rva::DRAWALLS_MIRROR_CALL_SITE_RVA, rva::DRAWALLS_MIRROR_CALL_SITE_BYTES,
                           (void*)&hooked_polymost_drawalls_mirror, &origMirror)) {
    s_origDrawallsMirror = (DrawallsFn)origMirror;
    LOGI("dollhouse: mirror drawalls call site redirected; recovered body rva=%08X",
         (unsigned)((unsigned char*)origMirror - g_base));
  } else if (ok) {
    LOGE("dollhouse: mirror drawalls call site did not fingerprint-match; the mirror pass keeps its stale bunch");
  }
  void* origPoly = 0;
  if (ok && hook_jmp_thunk(rva::THUNK_DRAWPOLY_RVA, rva::THUNK_DRAWPOLY_BYTES,
                           (void*)&hooked_drawpoly, &origPoly)) {
    s_drawpolyOrig = (DrawpolyFn)origPoly;
    LOGI("dollhouse: drawpoly thunk hooked; recovered body rva=%08X (expect %08X)",
         (unsigned)((unsigned char*)origPoly - g_base), rva::DRAWPOLY_BODY_RVA);
  } else if (ok) {
    LOGE("dollhouse: drawpoly thunk did not fingerprint-match; the engine keeps painting its own walls");
  }
  if (ok && engine_verified()
      && memcmp(g_base + rva::GETFLORZOFSLOPE_BODY_RVA, rva::ZOFSLOPE_BODY_BYTES, sizeof rva::ZOFSLOPE_BODY_BYTES) == 0
      && memcmp(g_base + rva::GETCEILZOFSLOPE_BODY_RVA, rva::ZOFSLOPE_BODY_BYTES, sizeof rva::ZOFSLOPE_BODY_BYTES) == 0) {
    s_florz = (ZofSlopeFn)(g_base + rva::GETFLORZOFSLOPE_BODY_RVA);
    s_ceilz = (ZofSlopeFn)(g_base + rva::GETCEILZOFSLOPE_BODY_RVA);
    LOGI("dollhouse: slope functions fingerprint ok");
  } else if (ok) {
    LOGE("dollhouse: slope functions fingerprint mismatch; the cutaway treats every plane as flat");
  }
  if (ok && memcmp(g_base + rva::DRAWPOLY_BODY_RVA, rva::DRAWPOLY_BODY_BYTES,
                   sizeof rva::DRAWPOLY_BODY_BYTES) == 0) {
    s_drawpoly = (DrawpolyFn)(g_base + rva::DRAWPOLY_BODY_RVA);
    LOGI("dollhouse: drawpoly fingerprint ok");
  } else if (ok) {
    LOGE("dollhouse: drawpoly fingerprint mismatch; the cutaway paints nothing");
  }
  return ok;
}
