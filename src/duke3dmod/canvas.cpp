#include "canvas.h"
#include "font.h"
#include "ui.h"
#include "anim.h"

// Outside the NO_GL guard: both builds scale, so a recorder never logs logical coordinates.
static int s_scale = UI_SCALE_DEFAULT;

void canvas_set_scale(int pct) { s_scale = ui_scale_clamped(pct); }
int  canvas_scale()            { return s_scale; }

// The panel transform (canvas.h), outside NO_GL for the same reason.
static int    s_txDx = 0, s_txDy = 0;
static int    s_txCx = 0, s_txCy = 0;
static double s_txScale = 1.0;
// Identity short-circuits dev_* so it is exact.
static bool   s_txIdentity = true;

void canvas_set_transform(int dx, int dy, double scale, int cx, int cy) {
  s_txDx = dx; s_txDy = dy; s_txCx = cx; s_txCy = cy;
  s_txScale = scale;
  s_txIdentity = (dx == 0 && dy == 0 && scale == 1.0);
}

// Half away from zero, saturating: an out-of-range or NaN (int) cast is UB on the render thread.
static int tx_round(double v) {
  return clamp_double_to_int(v < 0.0 ? v - 0.5 : v + 0.5);
}

static int tx_dev_x(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCx, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDx, s_scale));
}
static int tx_dev_y(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCy, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDy, s_scale));
}
// A length takes the scale, not the displacement: translation moves a shape, it does not resize it.
static int tx_dev_len(int d) {
  if (s_txIdentity) return d;
  return tx_round((double)d * s_txScale);
}

// Logical -> device: UI scale then transform; one trio for every primitive in both builds.
static int dev_x(int x)   { return tx_dev_x(ui_scale_px(x, s_scale)); }
static int dev_y(int y)   { return tx_dev_y(ui_scale_px(y, s_scale)); }
static int dev_len(int v) { return tx_dev_len(ui_scale_px(v, s_scale)); }

// /DNO_GL lets canvas_text_width be tested without OpenGL.
#ifndef NO_GL
#include <windows.h>
#include <GL/gl.h>
#include "engine.h"
#include "fontgen.h"

// gl.h declares only GL 1.1; defined locally rather than load an extension.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// For the backdrop capture: glCopyTexSubImage2D reads bottom-left, coordinates here are top-left.
static int s_devW = 0, s_devH = 0;

static unsigned s_fontTex = 0;
static int s_fontTexXdim = 0, s_fontTexYdim = 0;

// At any scale other than 100 the faces are rasterized at that scale (fontgen.cpp, GDI) into a second texture, since the
// baked atlas would blur under GL_LINEAR. A scale fontgen refused is remembered in s_runFailedPct.
static unsigned s_runTex = 0;
static int s_runTexXdim = 0, s_runTexYdim = 0;
static int s_runTexPct  = 0;
static FontAtlas s_run = { 0 };
static int s_runFailedPct = 0;   // 0 = none

static bool runtime_ready() {
  return s_scale != UI_SCALE_DEFAULT && s_run.pct == s_scale && s_run.pixels != nullptr;
}

// CPU/GDI only, so canvas_text_width can call it during layout.
static void runtime_prepare() {
  if (s_scale == UI_SCALE_DEFAULT) return;
  if (s_scale == s_runFailedPct)   return;
  if (runtime_ready())             return;
  fontgen_free(&s_run);
  if (!fontgen_build(s_scale, &s_run)) s_runFailedPct = s_scale;
}

static void set_colour(unsigned c) {
  glColor4ub((GLubyte)((c >> 16) & 0xFF), (GLubyte)((c >> 8) & 0xFF),
             (GLubyte)(c & 0xFF),         (GLubyte)((c >> 24) & 0xFF));
}

void canvas_begin(int w, int h) {
  s_devW = w;
  s_devH = h;
  // Cleared every frame: menu_draw leaves its transform standing (canvas.h), so the ESP must not inherit the panel's motion.
  canvas_set_transform(0, 0, 1.0, 0, 0);
  // Polymost is immediate-mode GL; leaking state corrupts the next frame.
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
  glOrtho(0, w, h, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_FOG);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void canvas_end() {
  glMatrixMode(GL_PROJECTION); glPopMatrix();
  glMatrixMode(GL_MODELVIEW);  glPopMatrix();
  glPopAttrib();
}

// A rect already in device pixels, so multi-part shapes scale once. Known limit: a right edge ui_scale_px(x)+ui_scale_px(w)
// can differ by one from ui_scale_px(x+w), so abutting fills may seam at a non-integer scale.
static void fill_device(int x, int y, int w, int h, unsigned rgba) {
  set_colour(rgba);
  glBegin(GL_QUADS);
    glVertex2i(x,     y);
    glVertex2i(x + w, y);
    glVertex2i(x + w, y + h);
    glVertex2i(x,     y + h);
  glEnd();
}

void canvas_rect(int x, int y, int w, int h, unsigned rgba) {
  fill_device(dev_x(x), dev_y(y), dev_len(w), dev_len(h), rgba);
}

void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  set_colour(rgba);
  glBegin(GL_LINES);
    glVertex2f(dx0 + 0.5f, dy0 + 0.5f);
    glVertex2f(dx1 + 0.5f, dy1 + 0.5f);
  glEnd();
}

void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo) {
  // Scaled once; routing edges back through canvas_rect would scale twice.
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  // Edge floored at one device pixel.
  int e = dev_len(1);
  if (e < 1) e = 1;
  fill_device(dx, dy, dw, dh, fill);
  fill_device(dx,          dy,          dw, e,          hi);
  fill_device(dx,          dy + dh - e, dw, e,          lo);
  fill_device(dx,          dy + e,      e,  dh - 2 * e, hi);
  fill_device(dx + dw - e, dy + e,      e,  dh - 2 * e, lo);
}

void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period) {
  // Period scaled too, else stripes bunch at 200% and spread at 75%.
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  const int dp = dev_len(period);
  // A non-positive period would hang the render thread; checked on device values.
  if (!canvas_hazard_params_ok(dw, dh, dp)) return;

  fill_device(dx, dy, dw, dh, ground);
  set_colour(stripe);
  // 45 degree bars clipped by a scissor box.
  glEnable(GL_SCISSOR_TEST);
  GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
  glScissor(dx, vp[3] - (dy + dh), dw, dh);
  glBegin(GL_QUADS);
  for (int i = -dh; i < dw + dh; i += dp * 2) {
    glVertex2i(dx + i,           dy + dh);
    glVertex2i(dx + i + dp,      dy + dh);
    glVertex2i(dx + i + dp + dh, dy);
    glVertex2i(dx + i + dh,      dy);
  }
  glEnd();
  glDisable(GL_SCISSOR_TEST);
}

// Glass primitives: gradient and glow are Glass-only; row_bind draws the round rect and outline in every style. No shader,
// FBO or stencil.

// One tessellation for the round rect and the backdrop, so the tint leaves no hairline at a corner. 4*seg points, each
// quarter arc edge-to-edge inclusive; at dr == 0 it collapses to the plain rect.
static const int ROUND_FAN_MAX = 4 * 12;   // anim_round_rect_segments' own ceiling
static int round_fan_points(int dx, int dy, int dw, int dh, int dr, int seg,
                            float* px, float* py) {
  if (seg < 3) seg = 3;
  if (seg > 12) seg = 12;
  // y down: corners walk clockwise from top-left; start angles 180, 270, 0, 90.
  const double pi = 3.14159265358979323846;
  const float cx[4] = { (float)(dx + dr),      (float)(dx + dw - dr),
                        (float)(dx + dw - dr), (float)(dx + dr) };
  const float cy[4] = { (float)(dy + dr),      (float)(dy + dr),
                        (float)(dy + dh - dr), (float)(dy + dh - dr) };
  int n = 0;
  for (int c = 0; c < 4; c++) {
    const double a0 = (180.0 + 90.0 * c) * pi / 180.0;
    for (int i = 0; i < seg; i++) {
      const double a = a0 + (pi / 2.0) * (double)i / (double)(seg - 1);
      px[n] = cx[c] + (float)(dr * cos(a));
      py[n] = cy[c] + (float)(dr * sin(a));
      n++;
    }
  }
  return n;
}

// Rounded rect in device pixels: a triangle fan from the centre, closed by repeating the first point.
static void round_rect_device(int dx, int dy, int dw, int dh, int dr, int seg,
                              unsigned rgba) {
  float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
  set_colour(rgba);
  glBegin(GL_TRIANGLE_FAN);
    glVertex2f(dx + dw * 0.5f, dy + dh * 0.5f);
    for (int i = 0; i < n; i++) glVertex2f(px[i], py[i]);
    glVertex2f(px[0], py[0]);
  glEnd();
}

// The corner radius a rectangle can carry: at most half its shorter side, or the arcs would cross and fold the fan.
static int corner_radius_device(int dw, int dh, int r) {
  int dr = dev_len(r > 0 ? r : 0);
  const int half = (dw < dh ? dw : dh) / 2;
  if (dr > half) dr = half;
  return dr < 0 ? 0 : dr;
}

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Segments from the device radius: a 200% panel would otherwise look more faceted.
  const int dr = corner_radius_device(dw, dh, r);
  round_rect_device(dx, dy, dw, dh, dr, anim_round_rect_segments(dr), rgba);
}

// A real ring (strip between outer and inner outlines), not a filled shape under the slab, which would wash the face in the
// rim colour. Outlines share a corner centre so thickness holds at corners while dr >= t.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Floored at one device pixel like canvas_bevel.
  int t = dev_len(thickness > 0 ? thickness : 0);
  if (t < 1) t = 1;
  const int dr  = corner_radius_device(dw, dh, r);
  // Device radius, as in canvas_round_rect.
  const int seg = anim_round_rect_segments(dr);
  // Thinner than two rims the outlines would cross: degenerate to the filled shape.
  if (dw <= 2 * t || dh <= 2 * t) {
    round_rect_device(dx, dy, dw, dh, dr, seg, rgba);
    return;
  }
  float ox[ROUND_FAN_MAX], oy[ROUND_FAN_MAX];
  float ix[ROUND_FAN_MAX], iy[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, ox, oy);
  const int ir = dr - t < 0 ? 0 : dr - t;
  // Same seg, so n matches and the lists pair.
  round_fan_points(dx + t, dy + t, dw - 2 * t, dh - 2 * t, ir, seg, ix, iy);
  set_colour(rgba);
  glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= n; i++) {
      const int k = (i < n) ? i : 0;
      glVertex2f(ox[k], oy[k]);
      glVertex2f(ix[k], iy[k]);
    }
  glEnd();
}

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Per-vertex colour, interpolated by fixed-function GL.
  glBegin(GL_QUADS);
    set_colour(top);    glVertex2i(dx,      dy);
                        glVertex2i(dx + dw, dy);
    set_colour(bottom); glVertex2i(dx + dw, dy + dh);
                        glVertex2i(dx,      dy + dh);
  glEnd();
}

void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  // Additive blend, put back after so no later primitive inherits it.
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  fill_device(dx, dy, dw, dh, rgba);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Owned like the runtime atlas: the engine recreates the GL context on a resolution change, leaving s_backTex a
// dead-context name glIsTexture cannot tell from a reused one. Delete only with same-context evidence (xdim/ydim), else
// abandon it.
static unsigned s_backTex  = 0;
static int s_backTexW = 0, s_backTexH = 0;
static int s_backXdim = 0, s_backYdim = 0;
static int s_backX = 0, s_backY = 0;         // the captured rect, device space
static int s_backW = 0, s_backH = 0;
static bool s_backReady = false;             // captured THIS frame

// Next power of two at or above v, capped at 4096; GL 1.1 has no NPOT guarantee, so over the cap refuses.
static int next_pow2_capped(int v) {
  int p = 1;
  while (p < v && p < 4096) p <<= 1;
  return p;
}

void canvas_backdrop_release() {
  const bool sameContext = (engine_xdim() == s_backXdim && engine_ydim() == s_backYdim);
  if (s_backTex != 0 && sameContext && glIsTexture(s_backTex)) glDeleteTextures(1, &s_backTex);
  s_backTex = 0;
  s_backTexW = 0; s_backTexH = 0;
  s_backXdim = 0; s_backYdim = 0;
  s_backReady = false;
}

bool canvas_backdrop_capture(int x, int y, int w, int h) {
  s_backReady = false;
  // The overlay draws from the SDL_GL_SwapWindow hook after the game's frame, so a back-buffer copy is the capture.
  if (s_devW <= 0 || s_devH <= 0) return false;
  int dx = dev_x(x), dy = dev_y(y);
  int dw = dev_len(w), dh = dev_len(h);
  // Clipped: glCopyTexSubImage2D leaves out-of-bounds texels undefined.
  if (dx < 0) { dw += dx; dx = 0; }
  if (dy < 0) { dh += dy; dy = 0; }
  if (dx + dw > s_devW) dw = s_devW - dx;
  if (dy + dh > s_devH) dh = s_devH - dy;
  if (dw <= 0 || dh <= 0) return false;

  const int texW = next_pow2_capped(dw), texH = next_pow2_capped(dh);
  if (texW < dw || texH < dh) return false;

  const int xdim = engine_xdim(), ydim = engine_ydim();
  const bool stillValid = s_backTex != 0 && glIsTexture(s_backTex) &&
                          xdim == s_backXdim && ydim == s_backYdim &&
                          texW == s_backTexW && texH == s_backTexH;
  if (stillValid) {
    glBindTexture(GL_TEXTURE_2D, s_backTex);
  } else {
    canvas_backdrop_release();
    glGenTextures(1, &s_backTex);
    if (s_backTex == 0) return false;
    glBindTexture(GL_TEXTURE_2D, s_backTex);
    // Allocated empty and filled by the copy below.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s_backTexW = texW; s_backTexH = texH;
    s_backXdim = xdim; s_backYdim = ydim;
    // Fail closed.
    if (!glIsTexture(s_backTex)) {
      s_backTex = 0; s_backTexW = 0; s_backTexH = 0;
      s_backXdim = 0; s_backYdim = 0;
      return false;
    }
  }
  // The one y flip in this file: glOrtho is top-left, the copy reads GL's bottom-left, so texture row 0 holds the panel's
  // bottom row. The read buffer is asserted GL_BACK; it is GL_PIXEL_MODE_BIT state, saved by canvas_begin/canvas_end.
  glReadBuffer(GL_BACK);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dx, s_devH - (dy + dh), dw, dh);
  s_backX = dx; s_backY = dy; s_backW = dw; s_backH = dh;
  s_backReady = true;
  return true;
}

void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const int dr  = corner_radius_device(dw, dh, r);
  // Device radius; the tint shares this tessellation.
  const int seg = anim_round_rect_segments(dr);

  if (s_backReady && s_backTex != 0 && s_backTexW > 0 && s_backTexH > 0) {
    // The blur is offset taps of the same texture (no FBOs), one extra quad per tap.
    const int taps = blurTaps <= 1 ? 1 : blurTaps <= 5 ? 5 : 9;
    // First entry the centre, first five the plus, all nine the 3x3 grid.
    static const int TAP_X[9] = { 0, -1,  1,  0,  0, -1, -1,  1, 1 };
    static const int TAP_Y[9] = { 0,  0,  0, -1,  1, -1,  1, -1, 1 };
    const int rad = dev_len(blurRadius > 0 ? blurRadius : 0);
    // A logical length spent as a device offset, so it takes the UI and panel scale; via thousandths so the decaying
    // amplitude is not rounded.
    const double ampDev = wobbleAmp * (double)ui_scale_px(1000, s_scale) / 1000.0 * s_txScale;
    // Texture coordinates only: growing the geometry would spill the blurred ground past the tint and rim.
    const double wob = anim_wobble(wobblePhase, ampDev);

    float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
    const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
    const float cx = dx + dw * 0.5f, cy = dy + dh * 0.5f;
    const float iw = 1.0f / (float)s_backTexW, ih = 1.0f / (float)s_backTexH;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_backTex);
    // MODULATE rather than trusting Polymost's state.
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // White at this tap's share of the tint alpha; canvas_tap_alpha makes the shares sum exactly.
    const int aTint = (int)((tint >> 24) & 0xFFu);
    for (int t = 0; t < taps; t++) {
      const int aPass = canvas_tap_alpha(aTint, taps, t);
      if (aPass <= 0) continue;
      set_colour(((unsigned)aPass << 24) | 0x00FFFFFFu);
      // Every tap draws the SAME fan and moves only texture coordinates: a true average at every pixel (offset geometry
      // would under-sample the edges).
      const float ox = (float)(TAP_X[t] * rad) + (float)wob;
      const float oy = (float)(TAP_Y[t] * rad) + (float)wob;
      glBegin(GL_TRIANGLE_FAN);
      for (int i = -1; i <= n; i++) {
        const float vx = (i < 0) ? cx : px[i < n ? i : 0];
        const float vy = (i < 0) ? cy : py[i < n ? i : 0];
        // Texture coordinate from device position through the captured rect; v counts up from its bottom.
        glTexCoord2f((vx + ox - (float)s_backX) * iw,
                     ((float)(s_backY + s_backH) - (vy + oy)) * ih);
        glVertex2f(vx, vy);
      }
      glEnd();
    }
    glDisable(GL_TEXTURE_2D);
  }
  // The tint over the ground, the same fan.
  round_rect_device(dx, dy, dw, dh, dr, seg, tint);
}

// Lazy, but a non-zero s_fontTex is not trusted: the engine recreates the GL context on a resolution change. glIsTexture
// confirms the name in the current context; xdim/ydim catches a driver that recycled it.
static void ensure_font_texture() {
  const int xdim = engine_xdim();
  const int ydim = engine_ydim();
  const bool stillValid = s_fontTex != 0 && glIsTexture(s_fontTex) &&
                           xdim == s_fontTexXdim && ydim == s_fontTexYdim;
  if (stillValid) return;

  glGenTextures(1, &s_fontTex);
  glBindTexture(GL_TEXTURE_2D, s_fontTex);
  // Unpack alignment 1 for the 8-bit atlas is client state glPushAttrib does not save.
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_W, FONT_ATLAS_H, 0,
               GL_ALPHA, GL_UNSIGNED_BYTE, font_atlas);
  glPopClientAttrib();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  s_fontTexXdim = xdim;
  s_fontTexYdim = ydim;
}

// Same context-loss rule as the baked one, plus its pct. Delete only in the creating context: glIsTexture could vouch for a
// dead-context name in the NEW one (Polymost regenerates from low names) and deleting would strip one of the game's own.
static void ensure_runtime_texture() {
  const int xdim = engine_xdim();
  const int ydim = engine_ydim();
  const bool stillValid = s_runTex != 0 && glIsTexture(s_runTex) &&
                          xdim == s_runTexXdim && ydim == s_runTexYdim &&
                          s_runTexPct == s_run.pct;
  if (stillValid) return;

  const bool sameContext = (xdim == s_runTexXdim && ydim == s_runTexYdim);
  if (s_runTex != 0 && sameContext && glIsTexture(s_runTex)) glDeleteTextures(1, &s_runTex);
  s_runTex = 0;
  // Also releases the backdrop texture, which this invalidation (context loss or scale change) would strand.
  canvas_backdrop_release();
  glGenTextures(1, &s_runTex);
  glBindTexture(GL_TEXTURE_2D, s_runTex);
  glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, s_run.w, s_run.h, 0,
               GL_ALPHA, GL_UNSIGNED_BYTE, s_run.pixels);
  glPopClientAttrib();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  s_runTexXdim = xdim;
  s_runTexYdim = ydim;
  s_runTexPct  = s_run.pct;
}

// Which metrics the active atlas carries; one definition across both builds so the DLL's measurement and the suite's cannot
// drift.
static const Glyph* metrics_for(int font) {
  runtime_prepare();
  return runtime_ready() ? s_run.glyphs[font] : font_glyphs[font];
}
static bool metrics_are_device() {
  runtime_prepare();
  return runtime_ready();
}

// Baked (logical metrics, quads scaled on the way out) or runtime (device metrics, 1:1).
struct AtlasView { const Glyph* glyphs; unsigned tex; int w, h; bool device; };

int canvas_text(int x, int y, const char* s, unsigned rgba, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return x;
  runtime_prepare();
  AtlasView v;
  if (runtime_ready()) {
    ensure_runtime_texture();
    v.glyphs = s_run.glyphs[font];  v.tex = s_runTex;
    v.w = s_run.w;                  v.h = s_run.h;
    v.device = true;
  } else {
    ensure_font_texture();
    v.glyphs = font_glyphs[font];   v.tex = s_fontTex;
    v.w = FONT_ATLAS_W;             v.h = FONT_ATLAS_H;
    v.device = false;
  }
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, v.tex);
  // MODULATE: REPLACE would not multiply fragment alpha by vertex alpha, which translucent text needs.
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  set_colour(rgba);
  const float iw = 1.0f / v.w, ih = 1.0f / v.h;
  // Pen: device and zero-based on the runtime atlas; logical on the baked one, scaling the accumulated pen (per-advance
  // scaling piles up error). The panel transform applies per glyph quad.
  const int ox = v.device ? ui_scale_px(x, s_scale) : 0;
  const int oy = v.device ? ui_scale_px(y, s_scale) : 0;
  int pen = v.device ? 0 : x;
  glBegin(GL_QUADS);
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    const Glyph& g = v.glyphs[*p - 0x20];
    if (g.w > 0) {
      int gx, gy, gw, gh;
      if (v.device) {
        gx = tx_dev_x(ox + pen + g.bx);        gy = tx_dev_y(oy + g.by);
        gw = tx_dev_len(g.w);                  gh = tx_dev_len(g.h);
      } else {
        gx = dev_x(pen + g.bx);                gy = dev_y(y + g.by);
        gw = dev_len(g.w);                     gh = dev_len(g.h);
      }
      const float u0 = g.x * iw, v0 = g.y * ih;
      const float u1 = (g.x + g.w) * iw, v1 = (g.y + g.h) * ih;
      glTexCoord2f(u0, v0); glVertex2i(gx,      gy);
      glTexCoord2f(u1, v0); glVertex2i(gx + gw, gy);
      glTexCoord2f(u1, v1); glVertex2i(gx + gw, gy + gh);
      glTexCoord2f(u0, v1); glVertex2i(gx,      gy + gh);
    }
    pen += g.adv;
  }
  glEnd();
  glDisable(GL_TEXTURE_2D);
  // Logical, like the x it was seeded from.
  return v.device ? x + ui_unscale_px(pen, s_scale) : pen;
}
#else  // NO_GL: recording bodies, so menu.cpp links into test_host without OpenGL
#include <string.h>

static CanvasOp s_ops[CANVAS_RECORD_MAX];
static int  s_opCount = 0;
static bool s_recording = false;
static bool s_overflowed = false;

void canvas_record_begin() { s_opCount = 0; s_overflowed = false; s_recording = true; }
void canvas_record_end()   { s_recording = false; }
int  canvas_record_count() { return s_opCount; }
bool canvas_record_overflowed() { return s_overflowed; }
const CanvasOp* canvas_record_op(int i) {
  return (i >= 0 && i < s_opCount) ? &s_ops[i] : nullptr;
}
const CanvasOp* canvas_record_find_text(const char* s) {
  if (!s) return nullptr;
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == CANVAS_OP_TEXT && strcmp(s_ops[i].text, s) == 0) return &s_ops[i];
  return nullptr;
}
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour) {
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == kind && s_ops[i].colour == colour) return &s_ops[i];
  return nullptr;
}

// Overflow sets a flag rather than wrapping, or a "never drawn" check would pass wrongly.
static CanvasOp* push_op(int kind, int x, int y, int w, int h, unsigned colour) {
  if (!s_recording) return nullptr;
  if (s_opCount >= CANVAS_RECORD_MAX) { s_overflowed = true; return nullptr; }
  CanvasOp& o = s_ops[s_opCount++];
  o.kind = kind; o.x = x; o.y = y; o.w = w; o.h = h;
  o.colour = colour; o.font = -1; o.text[0] = 0;
  return &o;
}

// Recorded coordinates are device space, as the GL bodies hand to OpenGL, so "the scale is applied" cannot pass while nothing
// moved. The transform is cleared here too: it belongs to the frame.
void canvas_begin(int, int) { canvas_set_transform(0, 0, 1.0, 0, 0); }
void canvas_end() {}
CanvasTransform canvas_record_transform() {
  CanvasTransform t;
  t.dx = s_txDx; t.dy = s_txDy; t.cx = s_txCx; t.cy = s_txCy; t.scale = s_txScale;
  return t;
}
void canvas_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_RECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
// Only the fill colour is recorded; bevel edges are always C_BEVELHI/C_BEVELLO.
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned, unsigned) {
  push_op(CANVAS_OP_BEVEL, dev_x(x), dev_y(y), dev_len(w), dev_len(h), fill);
}
void canvas_line(int x0, int y0, int x1, int y1, unsigned c) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  push_op(CANVAS_OP_LINE, dx0, dy0, dx1 - dx0, dy1 - dy0, c);
}
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned, int) {
  push_op(CANVAS_OP_HAZARD, dev_x(x), dev_y(y), dev_len(w), dev_len(h), stripe);
}
// Glass primitives, recording: one op each in device space; round rect and outline in every style (row_bind draws both).
void canvas_round_rect(int x, int y, int w, int h, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDRECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
// Own op kind so a sweep can tell a ring from a filled shape.
void canvas_round_rect_outline(int x, int y, int w, int h, int, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDOUTLINE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
// The top colour (the sheen); the bottom is the transparent end everywhere it is used.
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned) {
  push_op(CANVAS_OP_GRADIENT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), top);
}
void canvas_glow_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_GLOW, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
// Capture and draw are distinct events for a "drawn once a frame" sweep; true: no GL to fail.
bool canvas_backdrop_capture(int x, int y, int w, int h) {
  push_op(CANVAS_OP_CAPTURE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), 0u);
  return true;
}
// The tint is the recorded colour.
void canvas_backdrop_draw(int x, int y, int w, int h, int, unsigned tint,
                          double, double, int, int) {
  push_op(CANVAS_OP_BACKDROP, dev_x(x), dev_y(y), dev_len(w), dev_len(h), tint);
}
// Nothing to release; defined so callers need no preprocessor branch.
void canvas_backdrop_release() {}

// No runtime atlas here (fontgen.cpp is GDI, DLL-only).
static const Glyph* metrics_for(int font) { return font_glyphs[font]; }
static bool metrics_are_device() { return false; }

int canvas_text(int x, int y, const char* s, unsigned c, int font) {
  // Recorded width scales like the GL quads (tab_text_fits compares it to a device edge). ui_scale_px(sum of advances) can
  // differ from the GL per-glyph rounded edge by ~1px at a non-100 scale.
  CanvasOp* o = push_op(CANVAS_OP_TEXT, dev_x(x), dev_y(y),
                        dev_len(canvas_text_width(s, font)), 0, c);
  if (o) {
    o->font = font;
    if (s) strncpy_s(o->text, sizeof(o->text), s, _TRUNCATE);
  }
  return x;
}
#endif // NO_GL

// Logical for every caller, measured through the atlas that will be drawn (menu.cpp lays out in logical pixels; a width that
// moved with the scale would apply the factor twice). GDI hinting is not linear, so sum the live atlas and divide it out.
int canvas_text_width(const char* s, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return 0;
  const Glyph* glyphs = metrics_for(font);
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    w += glyphs[*p - 0x20].adv;
  }
  return metrics_are_device() ? ui_unscale_px(w, s_scale) : w;
}
