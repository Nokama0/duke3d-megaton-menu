#include <windows.h>
#include <GL/gl.h>
#include "world.h"
#include "esp_tint.h"   // first producer
#include "trigger.h"    // second, secret_volume_collect
#include "freecam.h"    // third, camera_draw_teleport_marker
#include "engine.h"
#include "common.h"
#include "hooks.h"   // g_base
#include "nocull.h"  // nocull_available: the capture call site this layer rides on
#include "config.h"
#include "log.h"

// In-world drawing layer's DLL half. Decides nothing (decisions are pure functions in world.h); it reads four live engine
// doubles and two GL matrices, so it cannot link into test_host. No hook of its own: nocull.cpp's scansector redirect calls
// world_capture(), shot.cpp's hooked_displaymasks calls world_draw_frame().

static WorldFrame s_frame;
static WorldQueue s_queue;
static int  s_lastDrawn = 0;
static bool s_installed = false;

static bool diag_throttle_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!s_first && (now - s_lastMs) < 1000) return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

// Shared by the drawn and the empty frame. `collected` says whether the producers ran: the refused frame reports before the
// producer slot, so the tint counters still hold the last collecting frame. face/wall/tex sum to tint=.
static void diag_line(int queued, bool overflowed, bool collected) {
  if (g_cfg.debuglog == 0 || !diag_throttle_due()) return;
  int tint = 0, face = 0, wall = 0, tex = 0;
  if (collected) {
    tint = esp_tint_last_count();
    esp_tint_kind_counts(&face, &wall, &tex);
  }
  LOGI("world: valid=%d queued=%d drawn=%d overflow=%d tint=%d face=%d wall=%d tex=%d",
       (int)s_frame.valid, queued, s_lastDrawn, (int)overflowed,
       tint, face, wall, tex);
}

// Capture, from hooked_polymost_scansector before the original. s_frame.valid is cleared first on every path so a mirror frame or unverified engine cannot draw on stale matrices.
void world_capture() {
  s_frame.valid = false;
  if (!s_installed || !engine_verified() || engine_in_prepare_mirror()) return;
  glGetFloatv(GL_PROJECTION_MATRIX, s_frame.proj);
  glGetFloatv(GL_MODELVIEW_MATRIX,  s_frame.modelview);
  s_frame.ghalfx        = *(const double*)(g_base + rva::GHALFX);
  s_frame.ghoriz        = *(const double*)(g_base + rva::GHORIZ);
  s_frame.grhalfxdown10 = *(const double*)(g_base + rva::GRHALFXDOWN10);
  s_frame.gviewxrange   = *(const double*)(g_base + rva::GVIEWXRANGE);
  s_frame.valid = s_frame.ghalfx > 0.0 && s_frame.gviewxrange > 0.0 && s_frame.grhalfxdown10 > 0.0;
}

bool world_push_quad(const WorldQuad& q) { return world_quad_ok(q) && world_queue_push(&s_queue, q); }
bool world_frame_valid() { return s_frame.valid; }

void world_skip_frame() {
  world_queue_reset(&s_queue);
  s_lastDrawn = 0;
}

// From hooked_displaymasks after the original (masks done, HUD not drawn). The queue empties at the top so an early return
// leaves no quads. glPushAttrib does not save the matrices, so both stacks are pushed/popped; depth func/range are set, never
// inherited.
void world_draw_frame() {
  world_queue_reset(&s_queue);
  s_lastDrawn = 0;
  if (!s_installed || !s_frame.valid || !engine_verified()) { diag_line(0, false, false); return; }
  // Producer slot, after the reset: elsewhere quads would be dropped or accumulated.
  esp_tint_collect();
  secret_volume_collect();
  camera_draw_teleport_marker();           // last, so it sits on top
  const int  queued     = s_queue.n;
  const bool overflowed = s_queue.overflow;
  if (queued == 0) { diag_line(queued, overflowed, true); return; }
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadMatrixf(s_frame.proj);
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(s_frame.modelview);
  glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); glDisable(GL_FOG); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthRange(WORLD_DEPTH_NEAR, WORLD_DEPTH_FAR); glDepthMask(GL_FALSE);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(WORLD_OFFSET_FACTOR, WORLD_OFFSET_UNITS);
  glBegin(GL_QUADS);
  for (int i = 0; i < queued; i++) {
    const WorldQuad& q = s_queue.q[i];
    WorldVertex v[4]; bool ok = true;
    for (int k = 0; k < 4 && ok; k++) ok = world_vertex(q.p[k], s_frame, &v[k]);
    if (!ok) continue;
    glColor4ub((GLubyte)((q.rgba >> 16) & 0xFF), (GLubyte)((q.rgba >> 8) & 0xFF),
               (GLubyte)(q.rgba & 0xFF),         (GLubyte)((q.rgba >> 24) & 0xFF));
    for (int k = 0; k < 4; k++) glVertex3d(v[k].x, v[k].y, v[k].z);
    s_lastDrawn++;
  }
  glEnd();
  glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
  glPopAttrib();
  diag_line(queued, overflowed, true);
}

// The capture site is nocull.cpp's scansector redirect, which can fail its fingerprint; the flag folds in nocull_available().
bool world_install() {
  s_installed = nocull_available();
  if (s_installed) LOGI("world: in-world layer ready (no hook of its own)");
  else LOGE("world: the capture call site did not install; the in-world layer stays off this session");
  return s_installed;
}
