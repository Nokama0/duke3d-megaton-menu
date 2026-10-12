#pragma once
#include "common.h"

// Which sector to seed the renderer from when the camera is outside every one: displayrooms clamps z into the seed's span,
// so a camera in rock must report some sector or the screen goes black. Nearest walls in 2D win, ties by z in the span.
struct NearSectWall   { int x, y, point2; };
struct NearSectSector { int wallptr, wallnum, ceilingz, floorz; };

// Squared distance from a point to a segment (clamped t). Doubles: cross^2 is a 2^110 intermediate no 64-bit type holds.
inline double nearsect_point_seg_dist2(int px, int py, int ax, int ay, int bx, int by) {
  const double vx = (double)bx - (double)ax, vy = (double)by - (double)ay;
  const double wx = (double)px - (double)ax, wy = (double)py - (double)ay;
  const double len2 = vx * vx + vy * vy;
  if (len2 <= 0.0) return wx * wx + wy * wy;
  double t = (vx * wx + vy * wy) / len2;
  if (t < 0.0) t = 0.0;
  else if (t > 1.0) t = 1.0;
  const double cx = wx - t * vx, cy = wy - t * vy;
  return cx * cx + cy * cy;
}

inline bool nearsect_z_inside(int z, int ceilingz, int floorz) {
  return z >= ceilingz && z <= floorz;
}

// Sector whose walls are nearest to (x, y); ties go to a sector whose span contains z, then the lower index. -1: keep the
// held sector, never sector 0. O(numwalls), run only once updatesectorz found none. point2 is bounded to its loop.
inline int nearest_sector(int x, int y, int z,
                          const NearSectWall* walls, const NearSectSector* sectors,
                          int numwalls, int numsectors) {
  if (!walls || !sectors) return -1;
  if (numwalls <= 0 || numsectors <= 0) return -1;
  if (numwalls > rva::MAX_WALLS || numsectors > rva::MAX_SECTORS) return -1;
  int best = -1;
  double bestD2 = 0.0;
  bool bestInside = false;
  for (int s = 0; s < numsectors; s++) {
    const int wp = sectors[s].wallptr, wn = sectors[s].wallnum;
    if (!wall_range_ok(wp, wn, numwalls)) continue;
    const bool inside = nearsect_z_inside(z, sectors[s].ceilingz, sectors[s].floorz);
    for (int w = wp; w < wp + wn; w++) {
      const int p2 = walls[w].point2;
      if (p2 < wp || p2 >= wp + wn) continue;
      const double d2 = nearsect_point_seg_dist2(x, y, walls[w].x, walls[w].y,
                                                 walls[p2].x, walls[p2].y);
      // Strictly less, so an equal distance never displaces an earlier sector; z may, only when the earlier lacked it.
      if (best < 0 || d2 < bestD2 || (d2 == bestD2 && inside && !bestInside)) {
        best = s; bestD2 = d2; bestInside = inside;
      }
    }
  }
  return best;
}
