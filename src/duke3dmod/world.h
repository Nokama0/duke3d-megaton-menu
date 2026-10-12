#pragma once
#include <math.h>
#include "project.h"

// A world quad corner: project_world position, gdepth and oz2. px/py are viewport coordinates (screen minus window origin).

struct WorldPoint  { int px, py; double gdepth, oz2; };
struct WorldVertex { double x, y, z; };
struct WorldFrame  { float proj[16], modelview[16]; double ghalfx, ghoriz, grhalfxdown10, gviewxrange; bool valid; };
const int WORLD_QUAD_MAX = 512;
struct WorldQuad  { WorldPoint p[4]; unsigned rgba; };
struct WorldQueue { WorldQuad q[WORLD_QUAD_MAX]; int n; bool overflow; };
const double WORLD_DEPTH_NEAR = 0.0, WORLD_DEPTH_FAR = 0.99999;
const float WORLD_OFFSET_FACTOR = -1.0f, WORLD_OFFSET_UNITS = -2.0f;
// Proportional depth bias: each vertex moves this fraction toward the camera along its view ray. A fixed polygon offset
// can't cover a residual growing as d^2 (float32 endpoint error); 1/512 is 0.125 units at d=64, 16 at the draw cap.
const double WORLD_DEPTH_BIAS = 1.0 / 512.0;
// drawpoly's vertex for a face sprite: r = gdepth / gviewxrange, times Polymost's pitch factor s = oz2 / ghalfx. The point
// arrives already pitched, so s scales all three components; oz2 <= 0 is a refusal.
inline bool world_vertex(const WorldPoint& p, const WorldFrame& f, WorldVertex* out) {
  if (!out || !f.valid || !(p.gdepth > 0.0) || !(f.gviewxrange > 0.0)) return false;
  if (!(p.oz2 > 0.0) || !(f.ghalfx > 0.0)) return false;
  const double r = p.gdepth / f.gviewxrange;
  const double s = p.oz2 / f.ghalfx;
  const double bias = 1.0 - WORLD_DEPTH_BIAS;
  out->x = ((double)p.px - f.ghalfx) * r * f.grhalfxdown10 * s * bias;
  out->y = (f.ghoriz - (double)p.py) * r * f.grhalfxdown10 * s * bias;
  out->z = r * s * (1.0 / 1024.0) * bias;
  return true;
}
inline WorldPoint world_view_point(int screenX, int screenY, int windowx1, int windowy1,
                                   double gdepth, double oz2) {
  WorldPoint p;
  p.px = clamp_ll_to_int((long long)screenX - (long long)windowx1);
  p.py = clamp_ll_to_int((long long)screenY - (long long)windowy1);
  p.gdepth = gdepth;
  p.oz2 = oz2;
  return p;
}
inline WorldPoint world_view_point(const ScreenPt& p, const CamState& cam,
                                   double gdepth, double oz2) {
  return world_view_point(p.x, p.y, cam.windowx1, cam.windowy1, gdepth, oz2);
}
inline bool world_view_point(const CamState& cam, const GProj& g, const short* sintable,
                             int x, int y, int z, WorldPoint* out) {
  if (!out) return false;
  ProjectDebug dbg;
  const ScreenPt p = project_world(cam, g, sintable, x, y, z, &dbg);
  if (!p.visible) return false;
  *out = world_view_point(p, cam, dbg.gdepth, dbg.oz2);
  return true;
}
inline void world_queue_reset(WorldQueue* q) { if (q) { q->n = 0; q->overflow = false; } }
inline bool world_queue_push(WorldQueue* q, const WorldQuad& wq) {
  if (!q) return false;
  if (q->n >= WORLD_QUAD_MAX) { q->overflow = true; return false; }
  q->q[q->n++] = wq; return true;
}
inline bool world_quad_ok(const WorldQuad& wq) { for (int i = 0; i < 4; i++) if (!(wq.p[i].gdepth > 0.0)) return false; return true; }

void world_capture();
// A producer's one quad, refused if a corner is behind the camera. Call only from the producer slot in world_draw_frame:
// the queue resets at its top, and in screenshot mode a collector elsewhere accumulates until overflow latches.
bool world_push_quad(const WorldQuad& q);
void world_draw_frame();

void world_skip_frame();
bool world_frame_valid();
bool world_install();
