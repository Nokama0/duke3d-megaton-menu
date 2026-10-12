#include "test_common.h"
#include "test_blocks.h"
#include "../dollhouse.h"
#include "../common.h"
#include "../freecam.h"
#include <math.h>

// Cutaway view (wishlist rank 25): the thunk decodes and pose parser the
// domost/drawpoly hooks rely on.
void tb_0131_dollhouse_rule() {
  // The thunk's rel32 lands on the body the hook expects to recover.
  CHECK(thunk_decode_target(rva::THUNK_DOMOST_RVA,
                            (unsigned)rva::THUNK_DOMOST_BYTES[1] | ((unsigned)rva::THUNK_DOMOST_BYTES[2] << 8) |
                            ((unsigned)rva::THUNK_DOMOST_BYTES[3] << 16) | ((unsigned)rva::THUNK_DOMOST_BYTES[4] << 24))
        == rva::DOMOST_BODY_RVA);
  CHECK(rva::THUNK_DOMOST_BYTES[0] == 0xE9);

  CHECK(rva::DRAWALLS_CALL_SITE_BYTES[0] == 0xE8);
  CHECK(rva::DRAWALLS_MIRROR_CALL_SITE_BYTES[0] == 0xE8);
  CHECK(thunk_decode_target(rva::DRAWALLS_MIRROR_CALL_SITE_RVA,
                            (unsigned)rva::DRAWALLS_MIRROR_CALL_SITE_BYTES[1] | ((unsigned)rva::DRAWALLS_MIRROR_CALL_SITE_BYTES[2] << 8) |
                            ((unsigned)rva::DRAWALLS_MIRROR_CALL_SITE_BYTES[3] << 16) | ((unsigned)rva::DRAWALLS_MIRROR_CALL_SITE_BYTES[4] << 24))
        == rva::POLYMOST_DRAWALLS_BODY_RVA);
  CHECK(rva::ZOFSLOPE_BODY_BYTES[0] == 0x55 && rva::ZOFSLOPE_BODY_BYTES[1] == 0x8B);
  CHECK(rva::THUNK_DRAWPOLY_BYTES[0] == 0xE9);
  // The two speed-ups (2026-09-11): an off-screen span paints nothing, and a
  // plane's height is read per vertex only when the plane is sloped.
  CHECK(dollhouse_plane_sloped(0) == 0);
  CHECK(dollhouse_plane_sloped(1) == 0);      // parallax alone is not a slope
  CHECK(dollhouse_plane_sloped(2) == 1);
  CHECK(dollhouse_plane_sloped(3) == 1);
  CHECK(dollhouse_plane_sloped(0x40) == 0);
  CHECK(thunk_decode_target(rva::THUNK_DRAWPOLY_RVA,
                            (unsigned)rva::THUNK_DRAWPOLY_BYTES[1] | ((unsigned)rva::THUNK_DRAWPOLY_BYTES[2] << 8) |
                            ((unsigned)rva::THUNK_DRAWPOLY_BYTES[3] << 16) | ((unsigned)rva::THUNK_DRAWPOLY_BYTES[4] << 24))
        == rva::DRAWPOLY_BODY_RVA);

  // The record-authority state machine: a matched floor-side call opens the
  // span, a sky sub-span never does, a solid call is its own span.
  {
    const float SOL = DOLLHOUSE_SOLID_LINE;
    DollhouseState s = dollhouse_state_reset();
    CHECK(dollhouse_depth_call_rec(&s, 3, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_FLOOR && s.rec == 3);
    CHECK(dollhouse_depth_call_rec(&s, -1, 12.f, 5.f, 16.f, 5.f).kind == DH_KIND_NONE);   // sky split
    CHECK(s.sx0 == 10.f && s.sx1 == 20.f && s.rec == 3);                                  // span kept
    CHECK(dollhouse_depth_call_rec(&s, -1, 20.f, 40.f, 10.f, 30.f).kind == DH_KIND_CEIL);
    CHECK(dollhouse_depth_call_rec(&s, -1, 20.f, 55.f, 10.f, 45.f).kind == DH_KIND_TOPSTEP);
    CHECK(dollhouse_depth_call_rec(&s, 3, 10.f, 70.f, 20.f, 75.f).kind == DH_KIND_BOTSTEP);
    CHECK(dollhouse_depth_call_rec(&s, 7, 30.f, SOL, 40.f, SOL).kind == DH_KIND_WALL && s.rec == 7);
    CHECK(dollhouse_depth_call_rec(&s, -1, 50.f, 80.f, 60.f, 90.f).kind == DH_KIND_NONE);  // unmatched: no span
    CHECK(s.sx0 == 30.f && s.rec == 7);
    CHECK(dollhouse_depth_call_rec(0, 1, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_NONE);
  }
  // The sky's flat top line bounds a wall from above, only inside the span.
  {
    DollhouseState s = dollhouse_state_reset();
    CHECK(dollhouse_depth_call_rec(&s, 2, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_FLOOR);
    dollhouse_note_sky_top(&s, 20.f, 40.f, 10.f, 30.f);    // not flat: ignored
    CHECK(!s.skyTopSet);
    dollhouse_note_sky_top(&s, 18.f, 35.f, 12.f, 35.f);    // flat, inside
    CHECK(s.skyTopSet && s.skyTop == 35.f);
    dollhouse_note_sky_top(&s, 30.f, 50.f, 25.f, 50.f);    // right of the span: ignored
    CHECK(s.skyTop == 35.f);
    CHECK(dollhouse_sky_clamped(20.0, &s) == 35.0);        // above the sky top: clamped down
    CHECK(dollhouse_sky_clamped(60.0, &s) == 60.0);        // below it: kept
    CHECK(dollhouse_depth_call_rec(&s, 4, 30.f, 80.f, 40.f, 90.f).kind == DH_KIND_FLOOR);
    CHECK(!s.skyTopSet);                                   // a new span forgets it
  }
  // The line clip keeps the positive side and splits a crossing edge.
  {
    DollhousePoly p; memset(&p, 0, sizeof p); p.n = 3;
    p.v[0].sx = 0; p.v[0].sy = 0; p.v[1].sx = 10; p.v[1].sy = 0; p.v[2].sx = 0; p.v[2].sy = 10;
    const DollhousePoly q = dollhouse_line_clip(&p, 0.0, -1.0, 5.0);   // keep sy <= 5
    CHECK(q.n == 4);
    int allBelow = 1; for (int i = 0; i < q.n; i++) if (q.v[i].sy > 5.0 + 1e-9) allBelow = 0;
    CHECK(allBelow);
    CHECK(dollhouse_line_clip(&p, 0.0, -1.0, -1.0).n == 0);   // nothing on the kept side
  }

  // The readout pose parser: finds the cam line anywhere in the copied text.
  {
    FreecamPose p;
    CHECK(freecam_parse_readout("domost calls=1\r\ncam x=-22684 y=24584 z=-176379 ang=15 pitch=-41 cap=374\r\n", &p));
    CHECK(p.x == -22684 && p.y == 24584 && p.z == -176379 && p.ang == 15 && p.pitch == -41);
    CHECK(!freecam_parse_readout("cam x=1 y=2", &p));
    CHECK(!freecam_parse_readout("", &p));
    CHECK(!freecam_parse_readout(0, &p));
  }
  CHECK(thunk_decode_target(rva::DRAWALLS_CALL_SITE_RVA,
                            (unsigned)rva::DRAWALLS_CALL_SITE_BYTES[1] | ((unsigned)rva::DRAWALLS_CALL_SITE_BYTES[2] << 8) |
                            ((unsigned)rva::DRAWALLS_CALL_SITE_BYTES[3] << 16) | ((unsigned)rva::DRAWALLS_CALL_SITE_BYTES[4] << 24))
        == rva::POLYMOST_DRAWALLS_BODY_RVA);

  // The renderer's pitch cap: a 1920x1080 viewport with xdimenscale 6<<16
  // allows about 63 degrees (around 360 units), well under FREECAM_PITCH_MAX;
  // unreal inputs answer the full range.
  {
    short sintable[2048];
    for (int i = 0; i < 2048; i++) sintable[i] = (short)floor(16384.0 * sin(i * 3.14159265358979323846 / 1024.0) + 0.5);
    const int cap = freecam_pitch_cap(960.0, 1080, 6 << 16, sintable);
    CHECK(cap >= 330 && cap <= 390);
    // Never below the floor: the formula collapsed to about 6 units in a
    // 2026-09-11 session and the clamp pinned the camera level, which in
    // play is "I cannot look down". A small answer is a loose cap now.
    CHECK(FREECAM_PITCH_CAP_MIN == 256);
    CHECK(freecam_pitch_cap(8.0, 1080, 6 << 16, sintable) == FREECAM_PITCH_CAP_MIN);
    CHECK(freecam_pitch_cap(960.0, 100000, 6 << 16, sintable) == FREECAM_PITCH_CAP_MIN);
    CHECK(freecam_pitch_cap(960.0, 1080, 1 << 30, sintable) == FREECAM_PITCH_CAP_MIN);
    CHECK(cap > FREECAM_PITCH_CAP_MIN);
    CHECK(cap < FREECAM_PITCH_MAX);
    CHECK(freecam_pitch_cap(0.0, 1080, 6 << 16, sintable) == FREECAM_PITCH_MAX);
    CHECK(freecam_pitch_cap(960.0, 0, 6 << 16, sintable) == FREECAM_PITCH_MAX);
    CHECK(freecam_pitch_cap(960.0, 1080, 6 << 16, 0) == FREECAM_PITCH_MAX);
    // A wide, short viewport tilts further than a tall one.
    CHECK(freecam_pitch_cap(960.0, 540, 6 << 16, sintable) > cap);
  }
}

// Cutaway view (wishlist rank 25): paint every surface as its own polygon
// and let the depth buffer sort it. The rules the DLL's drawpoly calls rest on.
void tb_0132_dollhouse_depth() {
  CHECK(rva::DRAWPOLY_BODY_BYTES[0] == 0x55 && rva::DRAWPOLY_BODY_BYTES[1] == 0x8B);
  CHECK(rva::DRAWPOLY_BODY_RVA == 0x00075190);

  // drawpoly's winding test: domost's own quad order is accepted, the same
  // quad reversed is not, and reversing a rejected one makes it drawable.
  {
    double px[4] = { 10, 20, 20, 10 }, py[4] = { 50, 60, 90, 80 };   // x0,cy0 x1,cy1 x1,fy1 x0,fy0
    CHECK(dollhouse_poly_accepted(px, py, 4) == 1);
    double rx[4] = { 10, 20, 20, 10 }, ry[4] = { 80, 90, 60, 50 };
    CHECK(dollhouse_poly_accepted(rx, ry, 4) == 0);
    CHECK(dollhouse_poly_fix_winding(rx, ry, 4) == 1);
    CHECK(ry[0] == 50.0 && ry[3] == 80.0);
    // A triangle of the same three corners follows the same rule.
    double tx[3] = { 10, 20, 20 }, ty[3] = { 50, 60, 90 };
    CHECK(dollhouse_poly_accepted(tx, ty, 3) == 1);
    double ux[3] = { 20, 20, 10 }, uy[3] = { 90, 60, 50 };
    CHECK(dollhouse_poly_accepted(ux, uy, 3) == 0);
    CHECK(dollhouse_poly_fix_winding(ux, uy, 3) == 1);
    // Collinear: no winding is drawable, so nothing is handed over.
    double cx[3] = { 0, 1, 2 }, cy[3] = { 0, 1, 2 };
    CHECK(dollhouse_poly_fix_winding(cx, cy, 3) == 0);
    CHECK(dollhouse_poly_accepted(0, py, 4) == 0);
    CHECK(dollhouse_poly_accepted(px, py, 2) == 0);
  }

  // The tilt clip: with gchang 0 and gshang 1 the half-plane is sy >= ghoriz+1.
  // A triangle with one vertex behind comes back as four, all in front.
  {
    DollhousePoly p;
    memset(&p, 0, sizeof p);
    p.n = 3;
    p.v[0].sy = 400; p.v[0].sx = 100;
    p.v[1].sy = 500; p.v[1].sx = 200;
    p.v[2].sy = 200; p.v[2].sx = 0;
    const DollhousePoly c = dollhouse_tilt_clip(&p, 400.0, 300.0, 0.0, 1.0);
    CHECK(c.n == 4);
    for (int i = 0; i < c.n; i++)
      CHECK(dollhouse_screen_depth(c.v[i].sy, 400.0, 300.0, 0.0, 1.0) >= 1.0);
    CHECK(c.v[2].sy == 301.0 && c.v[3].sy == 301.0);
    CHECK(fabs(c.v[3].sx - 50.5) < 1e-9);
    // Wholly behind: nothing to draw.
    DollhousePoly b = p;
    b.v[0].sy = 100; b.v[1].sy = 150; b.v[2].sy = 200;
    CHECK(dollhouse_tilt_clip(&b, 400.0, 300.0, 0.0, 1.0).n == 0);
  }

  // The near clip runs in view space, and world xy rides along so a slope's z
  // can still be asked for at the vertex the clip invented.
  {
    DollhousePoly p;
    memset(&p, 0, sizeof p);
    p.n = 3;
    p.v[0].wx = 0;  p.v[0].wy = 0;  p.v[0].depth = 3;
    p.v[1].wx = 10; p.v[1].wy = 0;  p.v[1].depth = 5;
    p.v[2].wx = 0;  p.v[2].wy = 10; p.v[2].depth = -1;
    const DollhousePoly c = dollhouse_near_clip(&p);
    CHECK(c.n == 4);
    CHECK(c.v[2].depth == 1.0 && c.v[3].depth == 1.0);
    CHECK(fabs(c.v[2].wx - 10.0 / 3.0) < 1e-9 && fabs(c.v[2].wy - 20.0 / 3.0) < 1e-9);
    CHECK(c.v[3].wx == 0.0 && c.v[3].wy == 5.0);
    DollhousePoly b = p;
    b.v[0].depth = 0.5; b.v[1].depth = 0.25; b.v[2].depth = -1;
    CHECK(dollhouse_near_clip(&b).n == 0);
  }

  // A six-wall sector holding two triangular loops: the split is the
  // predecessor's point2, and each loop's walls follow point2 from its start.
  {
    const int wp = 100, wn = 6;
    short p2[6] = { 101, 102, 100, 104, 105, 103 };
    CHECK(dollhouse_loop_start(p2, wp, wn, 0) == 1);
    CHECK(dollhouse_loop_start(p2, wp, wn, 1) == 0);
    CHECK(dollhouse_loop_start(p2, wp, wn, 2) == 0);
    CHECK(dollhouse_loop_start(p2, wp, wn, 3) == 1);
    CHECK(dollhouse_loop_start(p2, wp, wn, 4) == 0);
    CHECK(dollhouse_loop_start(p2, wp, wn, 5) == 0);
    short loop[8];
    CHECK(dollhouse_loop_walls(p2, wp, wn, 0, loop, 8) == 3);
    CHECK(loop[0] == 100 && loop[1] == 101 && loop[2] == 102);
    CHECK(dollhouse_loop_walls(p2, wp, wn, 3, loop, 8) == 3);
    CHECK(loop[0] == 103 && loop[1] == 104 && loop[2] == 105);
    // A chain that leaves the sector, and one too short to be a footprint.
    short bad[6] = { 101, 102, 200, 104, 105, 103 };
    CHECK(dollhouse_loop_walls(bad, wp, wn, 0, loop, 8) == 0);
    short two[2] = { 101, 100 };
    CHECK(dollhouse_loop_walls(two, wp, 2, 0, loop, 8) == 0);
    CHECK(dollhouse_loop_walls(p2, wp, wn, 0, loop, 2) == 0);
  }

  // What each domost call of one wall stands for, in call order.
  {
    DollhouseState s = dollhouse_state_reset();
    const float SOL = DOLLHOUSE_SOLID_LINE;
    CHECK(dollhouse_depth_call(&s, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_FLOOR);
    const DollhouseDepthCall ce = dollhouse_depth_call(&s, 20.f, 40.f, 10.f, 30.f);
    CHECK(ce.kind == DH_KIND_CEIL && ce.ay0 == 30.f && ce.ay1 == 40.f);
    // A narrower call on the same span is a parallax sub-span split.
    CHECK(dollhouse_depth_call(&s, 15.f, 40.f, 10.f, 30.f).kind == DH_KIND_NONE);
    const DollhouseDepthCall top = dollhouse_depth_call(&s, 20.f, 55.f, 10.f, 45.f);
    CHECK(top.kind == DH_KIND_TOPSTEP && top.ay0 == 30.f && top.by0 == 45.f && top.by1 == 55.f);
    const DollhouseDepthCall bot = dollhouse_depth_call(&s, 10.f, 70.f, 20.f, 75.f);
    CHECK(bot.kind == DH_KIND_BOTSTEP && bot.ay0 == 70.f && bot.by0 == 80.f && bot.by1 == 90.f);
    const DollhouseDepthCall wall = dollhouse_depth_call(&s, 10.f, SOL, 20.f, SOL);
    CHECK(wall.kind == DH_KIND_WALL && wall.ay0 == 30.f && wall.ay1 == 40.f);
    CHECK(wall.by0 == 80.f && wall.by1 == 90.f);
    // The next wall's floor claim opens a new span and forgets the old lines.
    CHECK(dollhouse_depth_call(&s, 30.f, 80.f, 40.f, 90.f).kind == DH_KIND_FLOOR);
    // A solid wall's call is its own span now (its lines come from the record),
    // so it is a wall even before a ceiling claim.
    CHECK(dollhouse_depth_call(&s, 30.f, SOL, 40.f, SOL).kind == DH_KIND_WALL);
    CHECK(dollhouse_depth_call(&s, 50.f, SOL, 60.f, SOL).kind == DH_KIND_WALL);   // and off-span too
    CHECK(s.sx0 == 50.f && s.sx1 == 60.f && s.rec == -1);
    CHECK(dollhouse_depth_call(0, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_NONE);
  }

  // Screen y grows downward: a quad whose lines cross at both ends is empty.
  CHECK(dollhouse_quad_empty(30.f, 40.f, 80.f, 90.f) == 0);
  CHECK(dollhouse_quad_empty(80.f, 90.f, 30.f, 40.f) == 1);
  CHECK(dollhouse_quad_empty(50.f, 50.f, 50.f, 50.f) == 1);
  CHECK(dollhouse_quad_empty(50.f, 90.f, 80.f, 40.f) == 1);   // crossed at one end: a bowtie
}

// Cutaway view: a wall's lines come from the wall, and a sky band paints a
// footprint cut to its own columns.
void tb_0133_dollhouse_wall_records() {
  CHECK(rva::P2 == 0x019CF620 && rva::THEWALL == 0x019782A0);

  // The x-span clip on a triangle straddling both bounds: the two crossings
  // per bound survive and every vertex lands inside the band.
  {
    DollhousePoly p;
    memset(&p, 0, sizeof p);
    p.n = 3;
    p.v[0].sx = 0;   p.v[0].sy = 0;
    p.v[1].sx = 100; p.v[1].sy = 0;
    p.v[2].sx = 50;  p.v[2].sy = 100;
    const DollhousePoly c = dollhouse_xspan_clip(&p, 20.0, 80.0);
    CHECK(c.n == 5);
    for (int i = 0; i < c.n; i++) CHECK(c.v[i].sx >= 20.0 && c.v[i].sx <= 80.0);
    CHECK(c.v[0].sx == 20.0 && c.v[1].sx == 80.0);
    CHECK(c.v[2].sx == 80.0 && fabs(c.v[2].sy - 40.0) < 1e-9);
    CHECK(c.v[3].sx == 50.0 && c.v[3].sy == 100.0);
    // A band the triangle misses entirely paints nothing.
    CHECK(dollhouse_xspan_clip(&p, 200.0, 300.0).n == 0);
    CHECK(dollhouse_xspan_clip(0, 20.0, 80.0).n == 0);
  }

  // Matching a call's span to a wall: nearest wins, a 1.5 px miss does not.
  {
    DollhouseWallRec r[3];
    memset(r, 0, sizeof r);
    r[0].sx0 = 10.0f;  r[0].sx1 = 20.0f;
    r[1].sx0 = 30.0f;  r[1].sx1 = 40.0f;
    r[2].sx0 = 10.5f;  r[2].sx1 = 20.4f;
    CHECK(dollhouse_match_record(r, 3, 10.0f, 20.0f) == 0);
    CHECK(dollhouse_match_record(r, 3, 10.4f, 20.3f) == 2);
    CHECK(dollhouse_match_record(r, 3, 30.2f, 40.1f) == 1);
    CHECK(dollhouse_match_record(r, 3, 11.5f, 20.0f) == -1);   // a 1.5 px miss
    CHECK(dollhouse_match_record(r, 3, 500.0f, 600.0f) == -1);
    CHECK(dollhouse_match_record(0, 3, 10.0f, 20.0f) == -1);
    CHECK(dollhouse_match_record(r, 0, 10.0f, 20.0f) == -1);
  }

  // The quads a record hands over: the solid wall spans ceiling to floor, the
  // top step ceiling to the next ceiling, the bottom step next floor to floor.
  {
    DollhouseWallRec r;
    memset(&r, 0, sizeof r);
    r.sx0 = 10.0f; r.sx1 = 20.0f;
    r.cy0 = 30.0; r.cy1 = 40.0; r.fy0 = 80.0; r.fy1 = 90.0;
    r.ncy0 = 45.0; r.ncy1 = 55.0; r.nfy0 = 70.0; r.nfy1 = 75.0;
    r.twoSided = 1;
    const DollhouseQuad w = dollhouse_record_quad(&r, DH_KIND_WALL);
    CHECK(w.ok == 1 && w.ay0 == 30.0 && w.ay1 == 40.0 && w.by0 == 80.0 && w.by1 == 90.0);
    const DollhouseQuad t = dollhouse_record_quad(&r, DH_KIND_TOPSTEP);
    CHECK(t.ok == 1 && t.ay0 == 30.0 && t.ay1 == 40.0 && t.by0 == 45.0 && t.by1 == 55.0);
    const DollhouseQuad b = dollhouse_record_quad(&r, DH_KIND_BOTSTEP);
    CHECK(b.ok == 1 && b.ay0 == 70.0 && b.ay1 == 75.0 && b.by0 == 80.0 && b.by1 == 90.0);
    CHECK(dollhouse_quad_empty(w.ay0, w.ay1, w.by0, w.by1) == 0);
    CHECK(dollhouse_quad_empty(t.ay0, t.ay1, t.by0, t.by1) == 0);
    // A plane call is no wall piece, and neither is a call off any span.
    CHECK(dollhouse_record_quad(&r, DH_KIND_FLOOR).ok == 0);
    CHECK(dollhouse_record_quad(&r, DH_KIND_CEIL).ok == 0);
    CHECK(dollhouse_record_quad(&r, DH_KIND_NONE).ok == 0);
    CHECK(dollhouse_record_quad(0, DH_KIND_WALL).ok == 0);
    // One-sided: the wall paints, the steps do not exist.
    r.twoSided = 0;
    CHECK(dollhouse_record_quad(&r, DH_KIND_WALL).ok == 1);
    CHECK(dollhouse_record_quad(&r, DH_KIND_TOPSTEP).ok == 0);
    CHECK(dollhouse_record_quad(&r, DH_KIND_BOTSTEP).ok == 0);
  }

  // A wall with one end behind the near plane keeps its world xy at the cut.
  {
    DollhouseVert a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.wx = 0;  a.depth = 3;
    b.wx = 10; b.depth = -1;
    CHECK(dollhouse_clip_seg(&a, &b) == 1);
    CHECK(b.depth == 1.0 && b.wx == 5.0 && a.depth == 3.0);
    a.depth = 0.5; b.depth = 0.25;
    CHECK(dollhouse_clip_seg(&a, &b) == 0);
  }

  // A new span forgets the record the previous span matched.
  {
    DollhouseState s = dollhouse_state_reset();
    CHECK(s.rec == -1);
    s.rec = 7;
    CHECK(dollhouse_depth_call(&s, 10.f, 80.f, 20.f, 90.f).kind == DH_KIND_FLOOR);
    CHECK(s.rec == -1);
  }
}
