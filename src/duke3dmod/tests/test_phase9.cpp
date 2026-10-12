#include "test_common.h"
#include "../world.h"
#include "../esp_tint.h"

// The in-world layer's pure half: world.cpp is not linkable into test_host, so the vertex formula, bounded queue and
// depth/polygon-offset constants live in world.h, pinned here.
void tb_0102_phase_9_task1_world_layer() {
  // r = gdepth / gviewxrange; X = (px - ghalfx) * r * grhalfxdown10 * s; Y = (ghoriz - py) * r * grhalfxdown10 * s; Z =
  // r * s / 1024, pitch factor s = oz2 / ghalfx, with a proportional depth bias on all three. Every WorldPoint below
  // carries oz2 == ghalfx (a level view, s == 1).
  WorldFrame f = WorldFrame();
  f.valid = true; f.ghalfx = 860.0; f.ghoriz = 450.0; f.grhalfxdown10 = 1.0 / (860.0 * 1024.0); f.gviewxrange = 200.0;
  const double keep = 1.0 - WORLD_DEPTH_BIAS;
  WorldVertex v;
  CHECK(world_vertex(WorldPoint{ 860, 450, 400.0, 860.0 }, f, &v));
  CHECK(fabs(v.x) < 1e-12 && fabs(v.y) < 1e-12);
  CHECK(fabs(v.z - (400.0 / 200.0) / 1024.0 * keep) < 1e-15);
  CHECK(world_vertex(WorldPoint{ 1720, 450, 400.0, 860.0 }, f, &v));
  CHECK(fabs(v.x - 860.0 * 2.0 / (860.0 * 1024.0) * keep) < 1e-15);
  CHECK(world_vertex(WorldPoint{ 860, 0, 400.0, 860.0 }, f, &v));
  CHECK(v.y > 0.0);
  WorldVertex a, b;
  world_vertex(WorldPoint{ 100, 100, 400.0, 860.0 }, f, &a);
  world_vertex(WorldPoint{ 100, 100, 800.0, 860.0 }, f, &b);
  CHECK(b.z > a.z && fabs(b.x - 2.0 * a.x) < 1e-15);
  CHECK(!world_vertex(WorldPoint{ 10, 10, 0.0, 860.0 }, f, &v));
  CHECK(!world_vertex(WorldPoint{ 10, 10, -5.0, 860.0 }, f, &v));
  WorldFrame g = f; g.gviewxrange = 0.0;
  CHECK(!world_vertex(WorldPoint{ 10, 10, 400.0, 860.0 }, g, &v));     // a zero scale is a refusal, not a division
  WorldFrame h = f; h.valid = false;
  CHECK(!world_vertex(WorldPoint{ 10, 10, 400.0, 860.0 }, h, &v));

  // The vertex carries Polymost's pitch factor: the point is already PITCHED, but depth built from gdepth alone was not.
  // 1. A level view (oz2 == ghalfx) gives factor 1.
  WorldVertex lvl;
  CHECK(world_vertex(WorldPoint{ 300, 200, 400.0, f.ghalfx }, f, &lvl));
  CHECK(fabs(lvl.x - (300.0 - 860.0) * (400.0 / 200.0) * f.grhalfxdown10 * keep) < 1e-18);
  CHECK(fabs(lvl.y - (450.0 - 200.0) * (400.0 / 200.0) * f.grhalfxdown10 * keep) < 1e-18);
  CHECK(fabs(lvl.z - (400.0 / 200.0) / 1024.0 * keep) < 1e-18);
  // 2. The factor is uniform: at twice ghalfx every component doubles.
  WorldVertex dbl;
  CHECK(world_vertex(WorldPoint{ 300, 200, 400.0, 2.0 * f.ghalfx }, f, &dbl));
  CHECK(fabs(dbl.x - 2.0 * lvl.x) < 1e-18);
  CHECK(fabs(dbl.y - 2.0 * lvl.y) < 1e-18);
  CHECK(fabs(dbl.z - 2.0 * lvl.z) < 1e-18);
  // 3. Screen position never depends on it: X/Z, Y/Z are identical across three oz2.
  WorldVertex third;
  CHECK(world_vertex(WorldPoint{ 300, 200, 400.0, 0.25 * f.ghalfx }, f, &third));
  CHECK(fabs(dbl.x / dbl.z - lvl.x / lvl.z) < 1e-12);
  CHECK(fabs(dbl.y / dbl.z - lvl.y / lvl.z) < 1e-12);
  CHECK(fabs(third.x / third.z - lvl.x / lvl.z) < 1e-12);
  CHECK(fabs(third.y / third.z - lvl.y / lvl.z) < 1e-12);
  // 4. At/behind the pitched view plane is a refusal.
  CHECK(!world_vertex(WorldPoint{ 300, 200, 400.0, 0.0 }, f, &v));
  CHECK(!world_vertex(WorldPoint{ 300, 200, 400.0, -100.0 }, f, &v));

  // The proportional depth bias: engine float32 endpoints and depth scales disagree at 1e-5, which a fixed polygon offset
  // cannot cover (world.h). The vertex moves along its own view ray. 1. Screen position is untouched.
  {
    WorldVertex bi;
    CHECK(world_vertex(WorldPoint{ 300, 200, 400.0, f.ghalfx }, f, &bi));
    CHECK(fabs(bi.x / bi.z - (300.0 - 860.0) * f.grhalfxdown10 * 1024.0) < 1e-12);
    CHECK(fabs(bi.y / bi.z - (450.0 - 200.0) * f.grhalfxdown10 * 1024.0) < 1e-12);
    CHECK(bi.z == (400.0 / 200.0) * (f.ghalfx / f.ghalfx) * (1.0 / 1024.0) * (1.0 - WORLD_DEPTH_BIAS));
    CHECK(bi.z < (400.0 / 200.0) / 1024.0);
  }
  // 3. Bound rather than exact: positive (toward viewer) and under 1/64 (16 Build units at the 8192 draw-distance cap).
  CHECK(WORLD_DEPTH_BIAS > 0.0 && WORLD_DEPTH_BIAS < 1.0 / 64.0);

  // The queue: bounded, overflow flagged, reset clears both. STATIC: a WorldQueue is ~53 KB against test_host's 1 MB stack.
  static WorldQueue q; world_queue_reset(&q);
  CHECK(q.n == 0 && !q.overflow);
  WorldQuad wq; for (int i = 0; i < 4; i++) wq.p[i] = WorldPoint{ 10 * i, 20 * i, 100.0 + i, 860.0 }; wq.rgba = 0x80FF0000u;
  for (int i = 0; i < WORLD_QUAD_MAX; i++) CHECK(world_queue_push(&q, wq));
  CHECK(q.n == WORLD_QUAD_MAX && !q.overflow);
  CHECK(!world_queue_push(&q, wq));
  CHECK(q.n == WORLD_QUAD_MAX && q.overflow);
  world_queue_reset(&q);
  CHECK(q.n == 0 && !q.overflow);

  CHECK(world_quad_ok(wq));
  WorldQuad bad = wq; bad.p[2].gdepth = 0.0;
  CHECK(!world_quad_ok(bad));

  // The depth constants are the masks pass's (docs/re/fog-shade-hud.md B).
  CHECK(WORLD_DEPTH_NEAR == 0.0 && fabs(WORLD_DEPTH_FAR - 0.99999) < 1e-9);
  CHECK(WORLD_OFFSET_FACTOR < 0.0f && WORLD_OFFSET_UNITS < 0.0f);
}

// The combination switch tint's pure half: esp_tint.cpp is DLL-only, so the verdict-to-colour rule and the two corner
// rules live in esp_tint.h, pinned here.
void tb_0103_phase_9_task2_combination_tint() {
  CHECK(tint_verdict(SW_COMBINATION, "Flip")  == TINT_FLIP);
  CHECK(tint_verdict(SW_COMBINATION, "Leave") == TINT_LEAVE);
  CHECK(tint_verdict(SW_COMBINATION, 0)       == TINT_NONE);
  CHECK(tint_verdict(SW_TWOSTATE, "Flip")     == TINT_NONE);
  CHECK(tint_verdict(SW_ACCESS, "Needs blue card") == TINT_NONE);
  CHECK(tint_colour(TINT_FLIP, 0x60A0FF60u, 0x60FF6060u) == 0x60A0FF60u);
  CHECK(tint_colour(TINT_LEAVE, 0x60A0FF60u, 0x60FF6060u) == 0x60FF6060u);
  CHECK(tint_colour(TINT_NONE, 0x60A0FF60u, 0x60FF6060u) == 0u);

  // Wall sprite: xv = xrepeat * sin(ang) / 65536 per texel, endpoints about the centre shifted by xoff. Endpoints are
  // FRACTIONAL, never rounded to the lattice (rounding tilted the tint's plane through the sprite's own). Only ang 256
  // is non-integer and catches the rounding.
  short sintable[2048]; for (int i = 0; i < 2048; i++) sintable[i] = (short)floor(16384.0 * sin(i * 3.14159265358979323846 / 1024.0) + 0.5);
  WorldXYd a, b;
  tint_wall_sprite_endpoints(1000, 2000, 512, 64, 32, 0, sintable, &a, &b);
  CHECK(a.x == 1000 - 16 * 16 && a.y == 2000);
  CHECK(b.x == a.x + 16 * 32 && b.y == 2000);
  tint_wall_sprite_endpoints(1000, 2000, 0, 64, 32, 0, sintable, &a, &b);
  CHECK(a.x == 1000 && a.y == 2000 + 16 * 16);
  CHECK(b.y == a.y - 16 * 32);
  tint_wall_sprite_endpoints(1000, 2000, 512, 64, 32, 4, sintable, &a, &b);
  CHECK(a.x == 1000 - 16 * 20 && b.x == a.x + 16 * 32);
  // ang 256, the 45-degree case: sintable[256] = 11585, so xv = 64 * 11585 / 65536 = 11.3134765625 exactly (a binary
  // fraction, so the equality is not a tolerance), f = 16.
  tint_wall_sprite_endpoints(1000, 2000, 256, 64, 32, 0, sintable, &a, &b);
  CHECK(sintable[256] == 11585 && sintable[1792] == -11585);
  CHECK(a.x == 1000.0 - 16.0 * 11.3134765625);
  CHECK(a.y == 2000.0 + 16.0 * 11.3134765625);
  CHECK(b.x == a.x + 32.0 * 11.3134765625);
  CHECK(b.y == a.y - 32.0 * 11.3134765625);
  CHECK(a.x != floor(a.x + 0.5) && a.y != floor(a.y + 0.5));
  CHECK(b.x != floor(b.x + 0.5) && b.y != floor(b.y + 0.5));

  int top, bottom;
  tint_wall_sprite_zrange(5000, 64, 32, 0, false, &top, &bottom);
  CHECK(bottom == 5000 && top == 5000 - 4 * 64 * 32);
  tint_wall_sprite_zrange(5000, 64, 32, 0, true, &top, &bottom);
  CHECK(bottom == 5000 + 2 * 32 * 64 && top == bottom - 4 * 64 * 32);
  tint_wall_sprite_zrange(5000, 64, 33, 0, true, &top, &bottom);        // odd tilesizy adds 2 * yrepeat more
  CHECK(bottom == 5000 + 2 * 33 * 64 + 2 * 64);
  tint_wall_sprite_zrange(5000, 64, 32, 3, false, &top, &bottom);       // yoff: z -= 4 * yrepeat * yoff
  CHECK(bottom == 5000 - 4 * 64 * 3);

  // The in-world vertex formula subtracts Polymost's ghalfx/ghoriz, measured inside the 3D VIEWPORT, while project_world
  // returns SCREEN coordinates, so the viewport origin must come back off or every quad is displaced once the 3D view is
  // not the whole screen. oz2 passes through beside gdepth.
  WorldPoint vp = world_view_point(500, 300, 40, 60, 128.0, 512.0);
  CHECK(vp.px == 460 && vp.py == 240 && vp.gdepth == 128.0 && vp.oz2 == 512.0);
  WorldPoint vp0 = world_view_point(500, 300, 0, 0, 8.0, 900.0);
  CHECK(vp0.px == 500 && vp0.py == 300 && vp0.oz2 == 900.0);
  ScreenPt sp = ScreenPt(); sp.x = 500; sp.y = 300; sp.visible = true;
  CamState wc = CamState(); wc.windowx1 = 40; wc.windowy1 = 60;
  WorldPoint vps = world_view_point(sp, wc, 128.0, 512.0);
  CHECK(vps.px == 460 && vps.py == 240 && vps.gdepth == 128.0 && vps.oz2 == 512.0);

  // The engine's wall-sprite clamp is against the sector's FLAT ceilingz/floorz, each side refused on its own parallax
  // bit. Build z grows downward: the ceiling raises `top`, the floor lowers `bottom`.
  int ct = -100, cb = 900;
  tint_clamp_to_sector(0, false, 500, false, &ct, &cb);
  CHECK(ct == 0 && cb == 500);
  ct = -100; cb = 900;
  tint_clamp_to_sector(0, true, 500, true, &ct, &cb);
  CHECK(ct == -100 && cb == 900);                                       // a parallaxed surface is no surface to clamp to
  ct = 100; cb = 400;
  tint_clamp_to_sector(0, false, 500, false, &ct, &cb);
  CHECK(ct == 100 && cb == 400);
  ct = -100; cb = 900;
  tint_clamp_to_sector(0, false, 500, true, &ct, &cb);
  CHECK(ct == 0 && cb == 900);                                          // each side is refused on its OWN bit

  // The wall-switch face is the whole wall, top/bottom from the OWNING sector's slopes. Synthetic snapshot, since esp.cpp
  // cannot link into test_host.
  EspWallSnapshot ws;
  ws.seq = 0; ws.count = 0; ws.overflow = 0;
  EspWallMark& mk = ws.m[ws.count++];
  mk.kind = ESP_WKIND_SWITCH; mk.wallnum = 12; mk.x = 100; mk.y = 200; mk.z = 300;
  mk.picnum = 162; mk.pal = 0; mk.hitag = 0; mk.lotag = 7; mk.sectnum = 41;
  const EspWallSnapshot copy = ws;
  CHECK(copy.count == 1 && copy.m[0].sectnum == 41);
  CHECK(copy.m[0].kind == ESP_WKIND_SWITCH && copy.m[0].wallnum == 12);
  // hitag 0 wants the base frame: pressed 163 is "Flip", base 162 is "Leave".
  CHECK(tint_verdict(switch_classify(163, 0, 0, 7, 0).family,
                     switch_classify(163, 0, 0, 7, 0).line) == TINT_FLIP);
  CHECK(tint_verdict(switch_classify(162, 0, 0, 7, 0).family,
                     switch_classify(162, 0, 0, 7, 0).line) == TINT_LEAVE);

  // project_world_d projects fractional endpoints without lattice rounding; it is the SAME arithmetic factored
  // (project_core). Fixture as test_esp.cpp's projection block.
  CamState cam = CamState();
  cam.posx = 0; cam.posy = 0; cam.posz = 0; cam.ang = 0;
  cam.horiz = 720; cam.ydimen = 1440; cam.halfxdimen = 1720;
  cam.xdimenscale = 65536; cam.viewingrange = 65536;
  cam.windowx1 = 0; cam.windowy1 = 0; cam.valid = true;
  const GProj g = proj_gproj_recomputed(cam, sintable);
  CHECK(g.valid && g.cosang == 16384.0 / 262144.0 && g.sinang == 0.0);
  {
    // 1. Handed the same integers, it is the same point, bit for bit.
    ProjectDebug di, dd;
    const ScreenPt pi = project_world  (cam, g, sintable, 4096, -1024, 409600, &di);
    const ScreenPt pd = project_world_d(cam, g, sintable, 4096.0, -1024.0, 409600.0, &dd);
    CHECK(pi.visible && pd.visible);
    CHECK(pd.x == pi.x && pd.y == pi.y && pd.depth == pi.depth && pd.dist2d == pi.dist2d);
    CHECK(dd.gdepth == di.gdepth && dd.oz2 == di.oz2 && dd.glat == di.glat);
    CHECK(dd.fwd == di.fwd && dd.lat == di.lat);
    CHECK(di.fwd == 4096 && di.lat == -1024);
    // 2. A fraction survives to the depth.
    ProjectDebug dh;
    const ScreenPt ph = project_world_d(cam, g, sintable, 4096.5, -1024.5, 409600.0, &dh);
    CHECK(ph.visible);
    CHECK(dh.gdepth == di.gdepth + 0.5 * g.cosang2 + 0.5 * g.sinang2);
    CHECK(dh.gdepth != di.gdepth);
    // 3. The ang 256 endpoint, fractional vs. rounded, gives different depths.
    ProjectDebug dfrac, dround;
    project_world_d(cam, g, sintable, a.x, a.y, 409600.0, &dfrac);
    project_world  (cam, g, sintable, (int)floor(a.x + 0.5), (int)floor(a.y + 0.5), 409600, &dround);
    CHECK(dfrac.gdepth != dround.gdepth);
  }

  // A face sprite's four corners: drawpoly rotates every vertex separately (docs/re/esp-sprites.md A2), so a pitched
  // sprite's four Zs carry four oz2; project_sprite_corners returns them in a fixed ring order.
  {
    const SpriteExtents ext = proj_sprite_extents(409600, 0, 64, 64, 32, 32);
    CHECK(ext.halfWidth == 64 * 32 / PROJ_SPRITE_XDIV);
    CHECK(ext.bottomZ == 409600 && ext.topZ == 409600 - 64 * 32 * PROJ_SPRITE_ZSCALE);

    ScreenPt pts[4]; ProjectDebug dbgs[4];
    const int n = project_sprite_corners(cam, g, sintable, 4096, -1024, ext, pts, dbgs);
    CHECK(n == 4 && pts[0].visible && pts[1].visible && pts[2].visible && pts[3].visible);

    // 1. The box is unchanged: project_sprite_box takes min/max over these same four corners.
    const ScreenBox box = project_sprite_box(cam, g, sintable, 4096, -1024, ext);
    CHECK(box.valid && box.corners == n);
    long long minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (int k = 1; k < 4; k++) {
      if (pts[k].x < minX) minX = pts[k].x;
      if (pts[k].x > maxX) maxX = pts[k].x;
      if (pts[k].y < minY) minY = pts[k].y;
      if (pts[k].y > maxY) maxY = pts[k].y;
    }
    CHECK(maxX - minX > PROJ_BOX_MIN_W && maxY - minY > PROJ_BOX_MIN_H);
    CHECK(box.w == (int)(maxX - minX) && box.h == (int)(maxY - minY));
    CHECK(box.x == (int)((minX + maxX) / 2 - (maxX - minX) / 2));
    CHECK(box.y == (int)minY);

    // 2. The ring is (c0,r0) (c1,r0) (c1,r1) (c0,r1): corners 0,3 are one column, 1,2 the other.
    CHECK(dbgs[0].dx == dbgs[3].dx && dbgs[0].dy == dbgs[3].dy);
    CHECK(dbgs[1].dx == dbgs[2].dx && dbgs[1].dy == dbgs[2].dy);
    CHECK(dbgs[0].dy != dbgs[1].dy);
    CHECK(pts[0].y == pts[1].y && pts[2].y == pts[3].y);
    CHECK(pts[0].y < pts[2].y);                          // row 0 is the TOP one

    // 3. One depth per column.
    CHECK(dbgs[0].gdepth == dbgs[3].gdepth && dbgs[1].gdepth == dbgs[2].gdepth);
    CHECK(dbgs[0].gdepth == dbgs[1].gdepth && dbgs[0].gdepth == dbgs[2].gdepth);
    // 4. Level, oz2 matches for all four; pitched, the top/bottom rows differ.
    CHECK(dbgs[0].oz2 == dbgs[2].oz2);
    CamState pit = cam; pit.horiz = 720 - 258;
    const GProj gp = proj_gproj_recomputed(pit, sintable);
    ScreenPt ppts[4]; ProjectDebug pdbgs[4];
    CHECK(project_sprite_corners(pit, gp, sintable, 4096, -1024, ext, ppts, pdbgs) == 4);
    CHECK(pdbgs[0].oz2 == pdbgs[1].oz2);
    CHECK(pdbgs[2].oz2 == pdbgs[3].oz2);
    CHECK(pdbgs[0].oz2 != pdbgs[2].oz2);
    // 5. No array for pts is not a projection: pts stays required.
    CHECK(project_sprite_corners(cam, g, sintable, 4096, -1024, ext, nullptr, dbgs) == 0);
    // 6. dbgs is optional.
    ScreenPt npts[4];
    const int nn = project_sprite_corners(cam, g, sintable, 4096, -1024, ext, npts, nullptr);
    CHECK(nn == n);
    for (int k = 0; k < 4; k++) {
      CHECK(npts[k].visible == pts[k].visible);
      CHECK(npts[k].x == pts[k].x && npts[k].y == pts[k].y);
    }
  }
}

#include "../wswitch.h"
#include "../firerate.h"

static int ws_engine_step(int pos, int* flips, int* finished) {
  if (pos == 0) return 0;
  if (pos == -9) { (*flips)++; return 10; }
  if (pos - 1 == 0) (*finished)++;
  return pos - 1;
}

void tb_0104_phase_9_task3_weapon_switch_speed() {
  // Never from rest, the flip, past -8 or 1, or backwards.
  CHECK(wswitch_advance(0, 5) == 0);
  CHECK(wswitch_advance(-9, 5) == -9);
  CHECK(wswitch_advance(-1, 1) == -2);
  CHECK(wswitch_advance(-1, 99) == -8);
  CHECK(wswitch_advance(-8, 3) == -8);
  CHECK(wswitch_advance(10, 3) == 7);
  CHECK(wswitch_advance(10, 99) == 1);
  CHECK(wswitch_advance(1, 3) == 1);
  CHECK(wswitch_advance(5, 0) == 5 && wswitch_advance(5, -2) == 5);
  CHECK(wswitch_advance(-3, 0) == -3);

  // A switch from -1 at 100 takes the stock 19 tics; at 400 fewer; the flip and the finish happen once each.
  const int rates[7] = { 100, 150, 200, 250, 300, 350, 400 };
  int stockTics = 0;
  { int pos = -1, flips = 0, fin = 0; while (pos != 0 && stockTics < 100) { pos = ws_engine_step(pos, &flips, &fin); stockTics++; } CHECK(stockTics == 19 && flips == 1 && fin == 1); }
  for (int ri = 0; ri < 7; ri++) {
    int pos = -1, flips = 0, fin = 0, tics = 0; double carry = 0.0;
    while (pos != 0 && tics < 100) {
      pos = ws_engine_step(pos, &flips, &fin); tics++;
      if (pos != 0) pos = wswitch_advance(pos, firerate_steps(rates[ri], &carry));
    }
    CHECK(flips == 1 && fin == 1);
    CHECK(tics <= stockTics);
    if (rates[ri] == 100) CHECK(tics == stockTics);
    if (rates[ri] == 400) CHECK(tics <= 7);
  }

  CHECK( wswitch_gate(true,  true,  false, false, 200, 5));
  CHECK(!wswitch_gate(false, true,  false, false, 200, 5));
  CHECK(!wswitch_gate(true,  false, false, false, 200, 5));
  CHECK(!wswitch_gate(true,  true,  true,  false, 200, 5));
  CHECK(!wswitch_gate(true,  true,  false, true,  200, 5));
  CHECK(!wswitch_gate(true,  true,  false, false, 100, 5));
  CHECK(!wswitch_gate(true,  true,  false, false, 200, 0));
  CHECK(!wswitch_gate(true,  true,  false, false, 200, -9));
  CHECK(streq(wswitch_gate_reason(true, true, false, false, 200, 5), "on"));
  CHECK(streq(wswitch_gate_reason(true, true, false, false, 100, 5), "off"));
  CHECK(streq(wswitch_gate_reason(true, true, false, false, 200, -9), "holster"));
}

#include "../trigger.h"

// The SECRET volume's pure half: trigger.cpp stages walls off live sector memory (DLL-only), so the staging plan,
// cap/fallback and curtain geometry live in trigger.h.
void tb_0105_phase_9_task4_secret_volume() {
  bool fb = false;
  CHECK(trigvol_stage_plan(12, 64, 0, 512, &fb) == 12 && !fb);
  CHECK(trigvol_stage_plan(70, 64, 0, 512, &fb) == 0 && fb);
  fb = false;
  CHECK(trigvol_stage_plan(12, 64, 505, 512, &fb) == 0 && fb);
  fb = false;
  CHECK(trigvol_stage_plan(0, 64, 0, 512, &fb) == 0 && fb);             // a sector with no walls: curtain
  // Both caps are INCLUSIVE; `fb` starts true so a fitting plan must CLEAR it.
  fb = true;
  CHECK(trigvol_stage_plan(12, 64, 500, 512, &fb) == 12 && !fb);
  fb = true;
  CHECK(trigvol_stage_plan(64, 64, 0, 512, &fb) == 64 && !fb);
  short sintable[2048]; for (int i = 0; i < 2048; i++) sintable[i] = (short)floor(16384.0 * sin(i * 3.14159265358979323846 / 1024.0) + 0.5);
  TrigWall c;
  trigvol_curtain(1000, 2000, 0, 256, 100, 900, sintable, &c);
  CHECK(c.x0 == 1000 && c.x1 == 1000 && c.y0 == 2000 - 256 && c.y1 == 2000 + 256);
  CHECK(c.z0top == 100 && c.z0bot == 900 && c.z1top == 100 && c.z1bot == 900);
  trigvol_curtain(1000, 2000, 512, 256, 100, 900, sintable, &c);
  CHECK(c.y0 == 2000 && c.y1 == 2000 && c.x0 == 1000 + 256 && c.x1 == 1000 - 256);
  WorldXY a, b; int at, ab, bt, bb;
  CHECK(trigvol_quad_corners(c, &a, &b, &at, &ab, &bt, &bb));
  CHECK(a.x == c.x0 && b.x == c.x1 && at == 100 && ab == 900 && bt == 100 && bb == 900);
  TrigWall bad = c; bad.z0top = 900; bad.z0bot = 100;
  CHECK(!trigvol_quad_corners(bad, &a, &b, &at, &ab, &bt, &bb));
  // A degenerate wall (both endpoints at the same x/y) is a zero-area quad, refused like the inverted height pair.
  TrigWall degenerate = c; degenerate.x1 = degenerate.x0; degenerate.y1 = degenerate.y0;
  CHECK(!trigvol_quad_corners(degenerate, &a, &b, &at, &ab, &bt, &bb));
  CHECK(TRIGVOL_WALLS_PER_SECTOR == 64 && TRIGVOL_WALLS_MAX == 512);
  // The in-world queue must hold >= 8 full rooms; pinned as a RELATION to the staging cap.
  CHECK(WORLD_QUAD_MAX >= 8 * TRIGVOL_WALLS_PER_SECTOR);

  // WHICH sectors are staged is the SECRET sector, not the room granting access to it.
  CHECK(secretvol_wanted(TRIG_SECRET_LOTAG));
  CHECK(secretvol_wanted(32767));
  CHECK(!secretvol_wanted(0));
  CHECK(!secretvol_wanted(20));           // a door: what the plate OPENS, not the secret
  CHECK(!secretvol_wanted(-1));           // engine_sector_lotag's refusal value
  CHECK(!secretvol_wanted(32766));
  {
    bool agree = true;
    for (int lt = -2; lt <= 2; lt++)
      if (secretvol_wanted(lt) != trigger_sector_is_secret(lt)) agree = false;
    if (secretvol_wanted(TRIG_SECRET_LOTAG) != trigger_sector_is_secret(TRIG_SECRET_LOTAG)) agree = false;
    CHECK(agree);
  }
  // The staging plan is applied per secret sector: 12 walls stage as 12, an ordinary room is never asked, and an over-cap
  // secret room falls to the curtain rather than being truncated.
  {
    const int lotags[4] = { TRIG_SECRET_LOTAG, 0, 20, TRIG_SECRET_LOTAG };
    const int wallnums[4] = { 12, 12, 12, 70 };
    int stagedWalls = 0, rooms = 0, curtains = 0;
    for (int k = 0; k < 4; k++) {
      if (!secretvol_wanted(lotags[k])) continue;
      bool f = false;
      const int plan = trigvol_stage_plan(wallnums[k], TRIGVOL_WALLS_PER_SECTOR,
                                          stagedWalls, TRIGVOL_WALLS_MAX, &f);
      stagedWalls += plan;
      if (f) curtains++; else rooms++;
    }
    CHECK(rooms == 1 && curtains == 1);
    CHECK(stagedWalls == 12);
  }

  // The curtain lies ALONG the room's first wall (a sector has no angle); secretvol_curtain_angle is a quarter turn from
  // the wall's direction, since trigvol_curtain builds ACROSS its angle.
  for (int wa = 0; wa < 2048; wa += 128) {
    const int ca = secretvol_curtain_angle(wa);
    CHECK(ca >= 0 && ca < 2048);
    TrigWall cw;
    trigvol_curtain(4000, 5000, ca, 1024, 100, 900, sintable, &cw);
    const double wx = (double)sintable[(wa + 512) & 2047], wy = (double)sintable[wa & 2047];
    const double px = (double)(cw.x1 - cw.x0), py = (double)(cw.y1 - cw.y0);
    // Parallel: cross product vanishes (bounded: floor(v + 0.5) can move each endpoint by half a unit).
    CHECK(fabs(wx * py - wy * px) < 2.0 * 16384.0);
    CHECK(px * px + py * py > 1000.0 * 1000.0);
  }
  CHECK(secretvol_curtain_angle(0) == 512);
  CHECK(secretvol_curtain_angle(1600) == 64);

  CHECK(secretvol_curtain_angle(2047) == 511);
}
