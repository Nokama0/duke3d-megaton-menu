#pragma once
#include "engine.h"
#include "common.h"

// The one world-units-per-metre constant.
const int BUILD_UNITS_PER_METRE = 512;

struct ScreenPt {
  int  x, y;      // overlay pixels
  int  depth;     // viewingrange-scaled: what the projection divides by; not true distance off-centre
  int  dist2d;    // true 2D distance, unscaled; display only
  bool visible;   // false (all fields zeroed) behind the camera or pitched view plane, or camera invalid
};

// ProjectDebug::reject codes; PROJ_OK is 0 so a zeroed struct reads "no rejection".
const int PROJ_OK             = 0;
const int PROJ_REJECT_INVALID = 1;
const int PROJ_REJECT_NEAR    = 2;
const int PROJ_REJECT_PITCH   = 3;

// ENGINE depth (gcosang2/gsinang2): the engine bails at VA 0x00482d36 against the double 1.0 (VA 0x00599e30); 4.0 is a
// 4x margin. trigger.cpp clips against this same const.
const double PROJ_NEAR_CLIP = 4.0;

// Optional debug intermediates from project_world so esp.cpp logs the EXACT values used; filled before the NEAR_CLIP
// return.
struct ProjectDebug {
  int dx, dy;
  int sinang, cosang;
  long long fwd, lat;     // after rotation and >>14, BEFORE viewingrange scaling (lat is never scaled)
  int viewingrange;       // 65536 == 1.0x at 90 degrees
  int dh;                 // (ydimen>>1) - horiz: the pitch in viewport pixels
  double glat, gdepth;    // ENGINE-sourced (gcosang/gsinang, gcosang2/gsinang2): what the divide uses
  double oz2;             // pitch-rotated depth of the final divide; 0 if never reached
  int reject;
};

// Driven by Polymost's OWN per-frame constants (GProj, engine_gproj()), not recomputed ones: calibration was correct at
// every FOV where three recomputed formulas were not.
ScreenPt project_world(const CamState& cam, const GProj& g, const short* sintable,
                       int wx, int wy, int wz, ProjectDebug* dbg = nullptr);

// For a point off the integer lattice (a wall sprite's endpoints, docs/re/polymost-depth.md C): rounding moved a tint's
// plane enough to z-fight (A3). Shares project_core.
ScreenPt project_world_d(const CamState& cam, const GProj& g, const short* sintable,
                         double wx, double wy, double wz, ProjectDebug* dbg = nullptr);

// DIAGNOSTIC ONLY: rebuilds a GProj as polymost_drawrooms would, for esp.cpp's log and the suite's fixtures. NEVER feed
// it to project_world.
GProj proj_gproj_recomputed(const CamState& cam, const short* sintable);

// A marker's ink extends past its projected point, so an exact-edge cull would clip a border enemy's label.
const int PROJ_CULL_MARGIN = 64;

inline bool proj_on_screen(int x, int y, int screenW, int screenH) {
  return x >= -PROJ_CULL_MARGIN && x <= screenW + PROJ_CULL_MARGIN
      && y >= -PROJ_CULL_MARGIN && y <= screenH + PROJ_CULL_MARGIN;
}

// The sprite's world-space extents projected as four corners; constants from polymost_drawsprite's face branch.

struct SpriteExtents {
  int halfWidth;   // x/y world units
  int topZ;        // smaller z is higher
  int bottomZ;
};

// cstat bit 7 centres the sprite on its z; clear (normal Duke enemy), z is the BOTTOM. Face branch VA 0x00482d02..
// 0x00482d9a: `bottom=cy+h/2, top=cy-h/2` set, `bottom=cy, top=cy-h` clear.
const int SPR_CSTAT_CENTRED = 0x80;

// Ceilings so a garbage read cannot overflow an extent (tilesizx/y are `short[9500]`).
const int PROJ_MAX_TILE_DIM = 4096;
const int PROJ_MAX_REPEAT   = 255;

// Equating polymost_drawsprite's face-branch gains (VA 0x00482d02-0x00482d9a) with project_world's gives height =
// tilesizy * yrepeat * 4, half-width = tilesizx * xrepeat / 10. Wall sprites are wider (/8, clipmove).
const int PROJ_SPRITE_ZSCALE = 4;
const int PROJ_SPRITE_XDIV   = 10;

// Minimums keep a distant enemy legible; the maximum and COORD_LIMIT stop `x + w` overflowing in canvas_rect.
const int PROJ_BOX_MIN_W       = 4;
const int PROJ_BOX_MIN_H       = 6;
const int PROJ_BOX_MAX         = 8192;
const int PROJ_BOX_COORD_LIMIT = 65536;

struct ScreenBox {
  int  x, y, w, h;
  bool valid;   // false unless ALL FOUR corners projected; other fields are 0
  int  corners; // corners accepted (0..4); diagnostic only
};

// Every argument is clamped (live memory); a zero or negative size gives a degenerate but well-formed extent.
SpriteExtents proj_sprite_extents(int wz, int cstat, int tilesizx, int tilesizy,
                                  int xrepeat, int yrepeat);

// Reachable counts are 0, 2 and 4.
inline bool proj_box_corners_ok(int corners) { return corners == 4; }

// Fallback when project_sprite_box refuses: a minimum-size box off the anchor; a POSITION and no size.
inline ScreenBox proj_anchor_box(int ax, int ay) {
  ScreenBox b = { 0, 0, 0, 0, false, 0 };
  b.w = PROJ_BOX_MIN_W;
  b.h = PROJ_BOX_MIN_H;
  // Widened before the subtraction, clamped after (project_world saturates).
  b.x = clamp_int(clamp_ll_to_int((long long)ax - PROJ_BOX_MIN_W / 2),
                  -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.y = clamp_int(clamp_ll_to_int((long long)ay - PROJ_BOX_MIN_H),
                  -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.valid = true;
  b.corners = 0;
  return b;
}

// Laterally by the ENGINE's yaw (g.cosang/g.sinang), not sintable on cam.ang. ALL FOUR corners must project: a partial
// set is a box of UNKNOWN size (it once gave a screen-wide streak).
ScreenBox project_sprite_box(const CamState& cam, const GProj& g, const short* sintable,
                             int wx, int wy, const SpriteExtents& ext);

// The four corners as POINTS, each with its own ProjectDebug; returns how many projected. A QUAD needs each corner's
// own depth: the four `oz2` differ across a pitched sprite (docs/re/polymost-depth.md A2). ORDER IS FIXED: (c0,r0)
// (c1,r0) (c1,r1) (c0,r1), a ring. `pts` and optional `dbgs` are written for all four (refused: visible == false).
int project_sprite_corners(const CamState& cam, const GProj& g, const short* sintable,
                           int wx, int wy, const SpriteExtents& ext,
                           ScreenPt pts[4], ProjectDebug dbgs[4] = nullptr);
