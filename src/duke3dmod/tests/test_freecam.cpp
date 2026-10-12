#include "test_common.h"

#include "test_blocks.h"

// Test-only bridge: freecam.h's turn rate is milli angle units per mouse
// count now, not a percent. 100% == one angle unit per count == FREECAM_SENS_MILLI_UNIT.
static FreecamSens pct(int p) { FreecamSens s = { p * 10, p * 10 }; return s; }



// To add a check: append tb_ to the right area file, declare it in test_blocks.h and call it from test_host.cpp's
// main() after whatever it depends on.




void tb_0001_harness_smoke_test_clamp_int_first() {
CHECK(clamp_int(5, 0, 10) == 5);
  CHECK(clamp_int(-3, 0, 10) == 0);
  CHECK(clamp_int(99, 0, 10) == 10);

  // clamp_double_to_int (common.h): in range truncates toward zero; out of range saturates instead of UB; NaN takes the
  // low branch, unreachable from project_world, so the NaN and -inf checks below are the only pin on the guard's
  // direction.
  CHECK(clamp_double_to_int(0.0) == 0);
  CHECK(clamp_double_to_int(1499.9999) == 1499);       // truncates toward zero
  CHECK(clamp_double_to_int(-1499.9999) == -1499);     // on both sides
  CHECK(clamp_double_to_int(2147483647.0) == 2147483647);
  CHECK(clamp_double_to_int(-2147483648.0) == -2147483647 - 1);
  CHECK(clamp_double_to_int(1e18) == 2147483647);
  CHECK(clamp_double_to_int(-1e18) == -2147483647 - 1);
  CHECK(clamp_double_to_int(std::numeric_limits<double>::infinity()) == 2147483647);
  CHECK(clamp_double_to_int(-std::numeric_limits<double>::infinity()) == -2147483647 - 1);
  CHECK(clamp_double_to_int(std::numeric_limits<double>::quiet_NaN()) == -2147483647 - 1);

  // clamp_ll_to_int (common.h): saturating, never wrapping. project_world
  // widens its pitch subtraction to long long to survive a hostile globalhoriz.
  CHECK(clamp_ll_to_int(0) == 0);
  CHECK(clamp_ll_to_int(2147483647LL) == 2147483647);
  CHECK(clamp_ll_to_int(2147483648LL) == 2147483647);
  CHECK(clamp_ll_to_int(-2147483648LL) == -2147483647 - 1);
  CHECK(clamp_ll_to_int(-2147483649LL) == -2147483647 - 1);
}


void tb_0013_remove_tmp_config_round_trip() {
remove(tmp);

  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenukey=112\nuiscale=150\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menukey == 112);
  CHECK(g_cfg.uiscale == 150);
  CHECK(config_save(tmp));
  g_cfg.menukey = 0;
  g_cfg.uiscale = 0;
  config_load(tmp);
  CHECK(g_cfg.menukey == 112);   // survives a save+reload, not just the first load
  CHECK(g_cfg.uiscale == 150);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenukey=999\nuiscale=1\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menukey == 254);   // clamped to [1,254]
  // uiscale floor moved from 50 to UI_SCALE_MIN; a saved 50 migrates up.
  CHECK(g_cfg.uiscale == UI_SCALE_MIN);   // clamped to [UI_SCALE_MIN, UI_SCALE_MAX]
  remove(tmp);

  CHECK(g_cfg.espenemies == 0);   // default off (from the very first config_load above)
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nespenemies=1\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.espenemies == 1);
  CHECK(config_save(tmp));
  g_cfg.espenemies = 0;
  config_load(tmp);
  CHECK(g_cfg.espenemies == 1);   // survives a save+reload
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nespenemies=42\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.espenemies == 1);   // any non-zero value collapses to 1
  remove(tmp);

  // A sign-flip bug still lands on 65536 at 90 degrees, so the direction/monotonicity checks below pin the sign.
  CHECK(fov_to_viewingrange(90) == 65536);
  CHECK(fov_to_viewingrange(120) > fov_to_viewingrange(90));
  CHECK(fov_to_viewingrange(90)  > fov_to_viewingrange(70));
  for (int d = FOV_MIN; d < FOV_MAX; d++)
    CHECK(fov_to_viewingrange(d) < fov_to_viewingrange(d + 1));
  CHECK(fov_to_viewingrange(10)  == fov_to_viewingrange(FOV_MIN));
  CHECK(fov_to_viewingrange(400) == fov_to_viewingrange(FOV_MAX));
  // Exact spot values (+/- 1 for rounding) so a future sign or reciprocal
  // error cannot slip back in even if it happened to preserve direction.
  CHECK(abs(fov_to_viewingrange(70)  - 45889)  <= 1);
  CHECK(abs(fov_to_viewingrange(100) - 78103)  <= 1);
  CHECK(abs(fov_to_viewingrange(120) - 113512) <= 1);
  CHECK(abs(fov_to_viewingrange(140) - 180059) <= 1);
}


void tb_0033_offset_0_width_4_reads() {
    static short st[2048];
    for (int i = 0; i < 2048; i++)
      st[i] = (short)(16384.0 * sin((double)i * 2.0 * 3.14159265358979323846 / 2048.0) + 0.5);

    CamState cam = {};
    cam.posx = 0; cam.posy = 0; cam.posz = 0;
    cam.ang = 0;                 // facing +x in Build's convention
    // cam.horiz is globalhoriz in VIEWPORT PIXELS (drawrooms 0x00429689: mulscale16(horiz-100, xdimenscale) +
    // (ydimen>>1)), not Build's 0..200 slider. 720 == ydimen>>1 makes this camera dead level (dh == 0), so
    // `cam.windowy1 + cam.horiz` is the horizon for this block.
    cam.horiz = 720;
    cam.ydimen = 1440;           // 3D viewport height; ydimen>>1 == 720
    cam.halfxdimen = 1720;       // 3440 / 2
    cam.xdimenscale = 65536;
    cam.viewingrange = 65536;    // neutral 1.0x, the 90-degree FOV value
    cam.windowx1 = 0; cam.windowy1 = 0;
    cam.valid = true;

    ScreenPt a = project_world(cam, gcam(cam, st), st, 4096, 0, 0);
    CHECK(a.visible);
    CHECK(a.x == cam.windowx1 + cam.halfxdimen);
    // Depth pins the rotation shift; X assertions are shift-invariant (symmetry, convergence). At ang 0, cosang is
    // 16384, so fwd = (4096 * 16384) >> 14 = 4096.
    CHECK(a.depth == 4096);

    ScreenPt b = project_world(cam, gcam(cam, st), st, -4096, 0, 0);
    CHECK(!b.visible);

    ScreenPt c = project_world(cam, gcam(cam, st), st, 0, 0, 0);
    CHECK(!c.visible);

    ScreenPt l = project_world(cam, gcam(cam, st), st, 4096, -1024, 0);
    ScreenPt r = project_world(cam, gcam(cam, st), st, 4096,  1024, 0);
    CHECK(l.visible && r.visible);
    CHECK(l.x != a.x && r.x != a.x);
    CHECK((l.x - a.x) == -(r.x - a.x));

    // Screen-space X offset is (lat * halfxdimen) / depth, not (lat * xdimenscale) / depth, confirmed against
    // polymost_drawsprite (halfxdimen=1720 vs xdimenscale=65536 here, so a reverted numerator cannot pass): x_offset =
    // (-1024 * 1720) / 4096 = -430.
    CHECK(l.x == cam.windowx1 + cam.halfxdimen - 430);
    CHECK(r.x == cam.windowx1 + cam.halfxdimen + 430);

    // Y magnitude derived from the engine's formula (project.cpp), not from project_world's output: gcosang2=0.0625,
    // depth_engine=256, gyxscale=0.5, dz=409600, so dpy-ghoriz = dz*gyxscale/depth_engine = 800 with dh==0.
    // project_world's depth omits the engine's 1/16 factor, so this is 16x the naive value: load-bearing.
    ScreenPt zoff = project_world(cam, gcam(cam, st), st, 4096, 0, 409600);
    CHECK(zoff.visible);
    CHECK(zoff.y == cam.windowy1 + (cam.ydimen >> 1) + 800);
    CHECK(zoff.y == cam.windowy1 + cam.horiz + 800);   // same thing, dh == 0 here

    // Doubling viewingrange (FOV) must double depth and halve the lateral
    // screen offset for the same world point; dist2d stays FOV-independent
    // since it is built from unscaled fwd/lat.
    {
      CamState cam2x = cam;
      cam2x.viewingrange = 65536 * 2;
      ScreenPt l2x = project_world(cam2x, gcam(cam2x, st), st, 4096, -1024, 0);
      CHECK(l2x.visible);
      CHECK(l2x.depth == 8192);                          // 4096 * (131072/65536)
      CHECK(l2x.x == cam.windowx1 + cam.halfxdimen - 215); // (-1024*1720)/8192 == -215 exactly
      CHECK(l2x.dist2d == l.dist2d);

      // Y divides by the SAME viewingrange-scaled depth as X (polymost_drawsprite shares one rcp=1/depth, 0x00482d48).
      // Other dz>0 cases run at neutral viewingrange where fwd==depth, so dividing by `fwd` would pass unnoticed:
      // zoff's 800 must halve to 409600*65536/(8192*8192) == 400.
      ScreenPt z2x = project_world(cam2x, gcam(cam2x, st), st, 4096, 0, 409600);
      CHECK(z2x.visible);
      CHECK(z2x.depth == 8192);
      CHECK(z2x.y == cam.windowy1 + (cam.ydimen >> 1) + 400);
    }

    // xdimenscale must be read, not hardcoded to 65536: it is the numerator of gyxscale = xdimenscale/131072, so
    // halving it halves the Y offset and leaves X (numerator halfxdimen) alone.
    {
      CamState halfScale = cam;
      halfScale.xdimenscale = 32768;
      ScreenPt h = project_world(halfScale, gcam(halfScale, st), st, 4096, 0, 409600);
      CHECK(h.visible);
      CHECK(h.y == cam.windowy1 + (cam.ydimen >> 1) + 400);   // zoff's 800, halved
      CHECK(h.x == zoff.x);                                    // X does not use it
    }

    // dist2d is true 2D distance, not forward-axis-only depth: equal to depth
    // dead ahead, exceeds it off to one side (Pythagoras).
    CHECK(a.dist2d == a.depth);
    CHECK(l.dist2d > l.depth && r.dist2d > r.depth);
    CHECK(l.dist2d == r.dist2d);
    // BUILD_UNITS_PER_METRE: the constant esp.cpp divides dist2d by; pinned
    // so it cannot silently drift from Build's native 512-units-per-metre.
    CHECK(BUILD_UNITS_PER_METRE == 512);

    ScreenPt near_ = project_world(cam, gcam(cam, st), st, 2048, 512, 0);
    ScreenPt far_  = project_world(cam, gcam(cam, st), st, 8192, 512, 0);
    CHECK(abs(far_.x - a.x) < abs(near_.x - a.x));
    CHECK(far_.depth > near_.depth);
    CHECK(far_.depth == 8192);

    // z increases downward in Build and on screen; level with the camera projects at the horizon.
    CHECK(project_world(cam, gcam(cam, st), st, 4096, 0, 0).y == cam.windowy1 + cam.horiz);
    // 409600 clears the truncation floor of the Y formula (depth*8192) with room to spare. Greater z is lower, so it
    // must project lower on screen.
    CHECK(project_world(cam, gcam(cam, st), st, 4096, 0,  409600).y > cam.windowy1 + cam.horiz);
    CHECK(project_world(cam, gcam(cam, st), st, 4096, 0, -409600).y < cam.windowy1 + cam.horiz);
    { const int hz = cam.windowy1 + cam.horiz;
      const int below = project_world(cam, gcam(cam, st), st, 4096, 0,  409600).y;
      const int above = project_world(cam, gcam(cam, st), st, 4096, 0, -409600).y;
      CHECK((below - hz) == (hz - above)); }

    // Depth must not overflow for far targets at Build's coordinate scale.
    ScreenPt v = project_world(cam, gcam(cam, st), st, 100000000, 0, 0);
    CHECK(v.depth > 0);

    // ProjectDebug must report the EXACT intermediates project_world used,
    // not a second independently-computed copy that could lie to a caller.
    {
      ProjectDebug dbg;
      ScreenPt a2 = project_world(cam, gcam(cam, st), st, 4096, 0, 0, &dbg);
      CHECK(a2.visible);
      CHECK(dbg.dx == 4096 && dbg.dy == 0);
      CHECK(dbg.sinang == 0 && dbg.cosang == 16384);
      CHECK(dbg.fwd == 4096 && dbg.lat == 0);
      CHECK(dbg.viewingrange == cam.viewingrange);
      CHECK((int)dbg.fwd == a2.depth);
      CHECK(dbg.dh == 0);                       // level camera: no pitch
      CHECK(dbg.oz2 == (double)cam.halfxdimen); // dh == 0 -> oz2 == oz == ghalfx
      CHECK(dbg.reject == PROJ_OK);
    }
    {
      // A near-clip-rejected point still populates dbg: those fields are
      // computed before the NEAR_CLIP check, so a caller gets real numbers.
      ProjectDebug dbg;
      ScreenPt behind = project_world(cam, gcam(cam, st), st, -4096, 0, 0, &dbg);
      CHECK(!behind.visible);
      CHECK(dbg.dx == -4096 && dbg.dy == 0);
      CHECK(dbg.fwd < 64);   // NEAR_CLIP; this is exactly why it was rejected
      CHECK(dbg.viewingrange == cam.viewingrange);
      CHECK(dbg.reject == PROJ_REJECT_NEAR);
      // dh is point-independent so it is reported even on rejection; oz2
      // is not, since the call never reached the divide that produces it.
      CHECK(dbg.dh == 0);
      CHECK(dbg.oz2 == 0.0);
    }
    {
      ProjectDebug dbg;
      CamState bad2 = cam; bad2.valid = false;
      ScreenPt inv = project_world(bad2, gcam(bad2, st), st, 4096, 0, 0, &dbg);
      CHECK(!inv.visible);
      CHECK(dbg.dx == 0 && dbg.dy == 0 && dbg.fwd == 0 && dbg.lat == 0 && dbg.viewingrange == 0);
      CHECK(dbg.dh == 0 && dbg.oz2 == 0.0);
      // reject is the one field not zeroed: it names which early return
      // fired, so a caller can tell invalid-camera apart from near/pitch clip.
      CHECK(dbg.reject == PROJ_REJECT_INVALID);
    }

    // halfxdimen/ydimen == 0 must be rejected on its own merits, not just via `valid`: both are divisors in
    // project_world.
    {
      CamState noX = cam; noX.halfxdimen = 0;
      CamState noY = cam; noY.ydimen = 0;
      CHECK(!project_world(noX, gcam(noX, st), st, 4096, 0, 0).visible);
      CHECK(!project_world(noY, gcam(noY, st), st, 4096, 0, 0).visible);
      ProjectDebug dbg;
      project_world(noY, gcam(noY, st), st, 4096, 0, 0, &dbg);
      CHECK(dbg.reject == PROJ_REJECT_INVALID);
    }

    // windowx1/windowy1: every other case leaves both at 0, so this pins two DIFFERENT non-zero offsets (dropping or
    // transposing either fails). resizeglcheck (0x00474fc0) calls bglViewport(windowx1, yres-(windowy2+1), ...), and
    // GL's bottom-left origin puts the sub-rect's top-left at (windowx1, windowy1) in this overlay's coordinates.
    {
      CamState off = cam;
      off.windowx1 = 37;
      off.windowy1 = 91;
      // Same geometry as `l` and `zoff` above: ox == -430, oy == 800, dh == 0 so the rotation is the identity.
      ScreenPt w = project_world(off, gcam(off, st), st, 4096, -1024, 409600);
      CHECK(w.visible);
      CHECK(w.x == 37 + 1720 - 430);   // 1327; 1290 if windowx1 were dropped
      CHECK(w.y == 91 + 720 + 800);    // 1611; 1520 if windowy1 were dropped
    }

    // Pitch: polymost_drawsprite produces pre-rotation coordinates against ghoriz == ydimen>>1 and drawpoly applies the
    // gshang/gchang rotation; substituting globalhoriz for ghoriz is Build's classic shear, exact only at a level view.
    // Inputs give exact integers: H=1000, ydimen 2000, globalhoriz 250 (dh=750, radius 1250, gshang 0.6, gchang 0.8).

    // Point (512, 512, 163840), camera at the origin facing +x: depth 512, ox=1000, oy=2560, oz=1000, oy2=1448,
    // oz2=2336, so px=1428, py=1619. It discriminates: no rotation gives (2000, 2810); the old 16x Y error (2116, 473);
    // globalhoriz as vertical origin y=869.
    {
      CamState pit = {};
      pit.posx = 0; pit.posy = 0; pit.posz = 0;
      pit.ang = 0;
      pit.horiz = 250;             // globalhoriz, in viewport pixels
      pit.ydimen = 2000;
      pit.halfxdimen = 1000;
      pit.xdimenscale = 65536;
      pit.viewingrange = 65536;
      pit.windowx1 = 0; pit.windowy1 = 0;
      pit.valid = true;

      ProjectDebug dbg;
      ScreenPt q = project_world(pit, gcam(pit, st), st, 512, 512, 163840, &dbg);
      CHECK(q.visible);
      CHECK(q.depth == 512);
      CHECK(dbg.dh == 750);
      CHECK(dbg.oz2 == 2336.0);
      CHECK(q.x == 1428);
      CHECK(q.y == 1619);

      // Pitch must move X too: the same H/oz2 factor scales both axes, which the classic-shear substitution lost. Dead
      // level, the identical point lands at H + ox and ghoriz + oy.
      CamState lvl = pit;
      lvl.horiz = 1000;            // dh == 0
      ScreenPt qLevel = project_world(lvl, gcam(lvl, st), st, 512, 512, 163840);
      CHECK(qLevel.visible);
      CHECK(qLevel.x == 1000 + 1000);
      CHECK(qLevel.y == 1000 + 2560);
      CHECK(q.x != qLevel.x);      // the pitch really does scale X
      CHECK(q.y != qLevel.y);

      // oz2 clip: the pitch rotation can swing a point behind the view plane, where the divide is meaningless; drawpoly
      // compares at 0x00475527 (continue only when 1.0 <= oz2). oz2 == 0.6*oy + 800, so the boundary is between oy ==
      // -1331 (accepted) and -1332 (rejected); dz == oy*64 here.
      ScreenPt justIn  = project_world(pit, gcam(pit, st), st, 512, 512, -1331 * 64);
      ScreenPt justOut = project_world(pit, gcam(pit, st), st, 512, 512, -1332 * 64);
      CHECK(justIn.visible);
      CHECK(!justOut.visible);
      CHECK(justOut.x == 0 && justOut.y == 0 && justOut.depth == 0 && justOut.dist2d == 0);
      ProjectDebug rej;
      project_world(pit, gcam(pit, st), st, 512, 512, -1400 * 64, &rej);
      CHECK(rej.reject == PROJ_REJECT_PITCH);   // not PROJ_REJECT_NEAR: depth was fine
      CHECK(rej.fwd == 512);                    // rotation intermediates still real
      CHECK(rej.oz2 < 0.0);
    }

    // p.x/p.y are clamped like p.depth, not bare (int) casts: a steep pitch shrinks oz2 and inflates H/oz2, so a
    // far-off-axis point can drive the product past int range.
    {
      CamState edge = {};
      edge.posx = 0; edge.posy = 0; edge.posz = 0;
      edge.ang = 0;
      edge.horiz = 1750;           // dh = 1000 - 1750 = -750
      edge.ydimen = 2000;
      edge.halfxdimen = 1000;
      edge.xdimenscale = 65536;
      edge.viewingrange = 65536;
      edge.windowx1 = 0; edge.windowy1 = 0;
      edge.valid = true;
      ScreenPt hi = project_world(edge, gcam(edge, st), st,  64,  1000000, 10640);
      ScreenPt lo = project_world(edge, gcam(edge, st), st,  64, -1000000, 10640);
      CHECK(hi.visible && lo.visible);
      CHECK(hi.x ==  2147483647);        // clamped, not wrapped
      CHECK(lo.x == -2147483647 - 1);
      CHECK(hi.y > 0 && hi.y == lo.y);   // the Y axis is unaffected by lat
    }

    CamState bad = cam; bad.valid = false;
    CHECK(!project_world(bad, gcam(bad, st), st, 4096, 0, 0).visible);

    // proj_on_screen: the marker cull shared by the calibration overlay's
    // classification and draw loop. Asserted at the exact margin boundary.
    CHECK(PROJ_CULL_MARGIN == 64);
    CHECK(proj_on_screen(0, 0, 3440, 1440));
    CHECK(proj_on_screen(3440, 1440, 3440, 1440));
    CHECK(proj_on_screen(-64, -64, 3440, 1440));           // exactly on the margin: kept
    CHECK(proj_on_screen(3440 + 64, 1440 + 64, 3440, 1440));
    CHECK(!proj_on_screen(-65, 0, 3440, 1440));            // one past it: culled
    CHECK(!proj_on_screen(0, -65, 3440, 1440));
    CHECK(!proj_on_screen(3440 + 65, 0, 3440, 1440));
    CHECK(!proj_on_screen(0, 1440 + 65, 3440, 1440));
    CHECK(!proj_on_screen(-100000, 720, 3440, 1440));
    CHECK(!proj_on_screen(1720, 100000, 3440, 1440));
    CHECK(!proj_on_screen(2147483647, 720, 3440, 1440));
    CHECK(!proj_on_screen(-2147483647 - 1, 720, 3440, 1440));
  }



// The freecam/accuracy constants and the two pure brackets. Everything is pinned to a LITERAL, never to another
// constant in the table, since two wrong numbers that agree would pass; a derived value (a thunk's body address, a call
// site's rel32) is written both ways.
void tb_0058_phase_4_task_2_freecam() {
    // A. displayrooms, the freecam detour point: DISPLAYROOMS_BODY_RVA is what the thunk's own rel32 decodes to, not a
    // second independent number.
    CHECK(rva::THUNK_DISPLAYROOMS_RVA == 0x0000182Au);   // VA 0x0040182A
    CHECK(rva::THUNK_DISPLAYROOMS_BYTES[0] == 0xE9);     // JMP rel32
    CHECK(rva::THUNK_DISPLAYROOMS_BYTES[1] == 0xB1);
    CHECK(rva::THUNK_DISPLAYROOMS_BYTES[2] == 0xF2);
    CHECK(rva::THUNK_DISPLAYROOMS_BYTES[3] == 0x0B);
    CHECK(rva::THUNK_DISPLAYROOMS_BYTES[4] == 0x00);
    CHECK(rva::DISPLAYROOMS_BODY_RVA == 0x000C0AE0u);    // VA 0x004C0AE0
    {
      const unsigned rel32 = (unsigned)rva::THUNK_DISPLAYROOMS_BYTES[1]
                           | ((unsigned)rva::THUNK_DISPLAYROOMS_BYTES[2] << 8)
                           | ((unsigned)rva::THUNK_DISPLAYROOMS_BYTES[3] << 16)
                           | ((unsigned)rva::THUNK_DISPLAYROOMS_BYTES[4] << 24);
      CHECK(thunk_decode_target(rva::THUNK_DISPLAYROOMS_RVA, rel32) == rva::DISPLAYROOMS_BODY_RVA);
    }
    // Not any of the thunks this DLL already hooks: a transposed digit here
    // would install the camera detour over the aim/aspect/input hooks.
    CHECK(rva::THUNK_DISPLAYROOMS_RVA != rva::THUNK_SHOOT_RVA);
    CHECK(rva::THUNK_DISPLAYROOMS_RVA != rva::THUNK_PROCESSINPUT_RVA);
    CHECK(rva::THUNK_DISPLAYROOMS_RVA != rva::THUNK_GETINPUT_RVA);
    CHECK(rva::THUNK_DISPLAYROOMS_RVA != rva::THUNK_SETASPECT_RVA);

    CHECK(rva::RVA_SCREENPEEK == 0x015F1C78u);           // VA 0x019F1C78
    CHECK(rva::RVA_SCREENPEEK != rva::RVA_MYCONNECTINDEX);
    CHECK(rva::RVA_SCREENPEEK != rva::RVA_PS);

    // B. the sector and slope bodies, called directly (real bodies, not thunks). getflorzofslope and getceilzofslope
    // are 352 bytes apart, and swapping them puts the camera's floor where its ceiling should be, so all three are
    // pinned apart as well as to literals.
    CHECK(rva::UPDATESECTORZ_BODY_RVA == 0x00043910u);   // VA 0x00443910
    CHECK(rva::GETFLORZ_BODY_RVA      == 0x00046750u);   // VA 0x00446750
    CHECK(rva::GETCEILZ_BODY_RVA      == 0x000465F0u);   // VA 0x004465F0
    CHECK(rva::GETFLORZ_BODY_RVA != rva::GETCEILZ_BODY_RVA);
    CHECK(rva::UPDATESECTORZ_BODY_RVA != rva::CANSEE_BODY_RVA);
    CHECK(rva::UPDATESECTORZ_BODY_RVA != rva::GETANGLE_BODY_RVA);

    CHECK(rva::HITSCAN_BODY_RVA == 0x0003F180u);   // VA 0x0043F180
    CHECK(rva::HITSCAN_CLIPTYPE == 0x01000040u);   // CLIPMASK1
    CHECK(rva::HITSCAN_BODY_RVA != rva::UPDATESECTORZ_BODY_RVA);
    CHECK(rva::HITSCAN_BODY_RVA != rva::GETFLORZ_BODY_RVA);
    CHECK(rva::HITSCAN_BODY_RVA != rva::GETCEILZ_BODY_RVA);
    CHECK(rva::HITSCAN_BODY_RVA != rva::CANSEE_BODY_RVA);

    CHECK(rva::PAUSE_ON == 0x017645B8u);   // VA 0x01B645B8
    CHECK(rva::PAUSE_ON != rva::UD);
    CHECK(rva::PAUSE_ON != rva::HITSCAN_BODY_RVA);
    CHECK(rva::PAUSE_ON != rva::NUMPLAYERS);
    CHECK(rva::PAUSE_ON != rva::TOTALCLOCK);

    // hitscan_call's fail-closed seeding: a body that writes NOTHING to any out-parameter must report "nothing hit"
    // (all -1, impact at origin), never a hitsect seeded from the starting sector, which would read as a hit.
    {
      int hitsect = 12345, hitwall = 12345, hitsprite = 12345;   // poisoned
      int hitx = 12345, hity = 12345, hitz = 12345;
      CHECK(hitscan_call(fake_hitscan_writes_nothing, 0, 0, 0, (short)7,
                         16384, 0, 0, rva::HITSCAN_CLIPTYPE,
                         &hitsect, &hitwall, &hitsprite, &hitx, &hity, &hitz));
      CHECK(hitsect == -1);      // a "seed hs from sectnum" bug reports 7 here
      CHECK(hitwall == -1);
      CHECK(hitsprite == -1);
      CHECK(hitx == 0 && hity == 0 && hitz == 0);

      const TeleportAim a = freecam_teleport_aim(hitsect, hitwall, hitsprite,
                                                 hitx, hity, hitz, 16384, 0);
      CHECK(!a.ok);
      CHECK(strcmp(a.reason, TELEPORT_REASON_NOTHING) == 0);

      // Null guards: a null function or a null out-parameter refuses rather
      // than faulting, touching nothing.
      int untouched = 999;
      CHECK(!hitscan_call(0, 0, 0, 0, (short)7, 16384, 0, 0, rva::HITSCAN_CLIPTYPE,
                          &untouched, &untouched, &untouched, &untouched, &untouched, &untouched));
      CHECK(!hitscan_call(fake_hitscan_writes_nothing, 0, 0, 0, (short)7,
                          16384, 0, 0, rva::HITSCAN_CLIPTYPE,
                          0, &hitwall, &hitsprite, &hitx, &hity, &hitz));
    }

    // The five player_struct offsets, from the shipping PDB layout (940 bytes == PS_STRIDE): literals first, then
    // adjacency (oposx/y/z are contiguous longs, opyoff follows pyoff); each field's last byte stays inside the stride.
    CHECK(rva::PS_OPOSX == 568u && rva::PS_OPOSY == 572u && rva::PS_OPOSZ == 576u);
    CHECK(rva::PS_OPYOFF == 584u);
    CHECK(rva::PS_POSZV == 596u);
    CHECK(rva::PS_OPOSY == rva::PS_OPOSX + 4 && rva::PS_OPOSZ == rva::PS_OPOSY + 4);
    CHECK(rva::PS_OPYOFF == rva::PS_PYOFF + 4);          // pyoff 580, opyoff 584
    CHECK(rva::PS_OPOSZ + 4 <= rva::PS_STRIDE);
    CHECK(rva::PS_POSZV + 4 <= rva::PS_STRIDE);
    CHECK(rva::PS_OPOSX != rva::PS_POSX && rva::PS_OPOSY != rva::PS_POSY);
    CHECK(rva::PS_OPOSZ != rva::PS_POSZ);
    CHECK(rva::PS_POSX == 532u && rva::PS_POSY == 536u && rva::PS_POSZ == 540u);
    CHECK(rva::PS_ROTSCRNANG == 810u && rva::PS_OROTSCRNANG == 808u);
    CHECK(rva::PS_ROTSCRNANG + 2 <= rva::PS_STRIDE);
    CHECK(rva::PS_OVER_SHOULDER_ON == 746u);

    // C. the six spread sites: each site's five bytes are written both as the rel32 arithmetic `KRAND_THUNK_RVA - (site
    // + 5)` and as literal bytes from the shipping duke3d.exe; only both together pin them.
    CHECK(rva::KRAND_THUNK_RVA == 0x000018D9u);          // VA 0x004018D9
    {
      struct Expect { unsigned rvaSite; unsigned char centre; unsigned char bytes[5]; };
      const Expect ex[6] = {
        { 0x000F422Cu, 0x10, { 0xE8, 0xA8, 0xD6, 0xF0, 0xFF } },   // pistol yaw   VA 0x004F422C
        { 0x000F4273u, 0x80, { 0xE8, 0x61, 0xD6, 0xF0, 0xFF } },   // pistol pitch VA 0x004F4273
        { 0x000F428Cu, 0x10, { 0xE8, 0x48, 0xD6, 0xF0, 0xFF } },   // hitscan yaw  VA 0x004F428C
        { 0x000F42DCu, 0x80, { 0xE8, 0xF8, 0xD5, 0xF0, 0xFF } },   // hitscan pitch VA 0x004F42DC
        { 0x000F5E00u, 0x10, { 0xE8, 0xD4, 0xBA, 0xF0, 0xFF } },   // expander yaw VA 0x004F5E00
        { 0x000F5E47u, 0x80, { 0xE8, 0x8D, 0xBA, 0xF0, 0xFF } },   // expander pitch VA 0x004F5E47
      };
      const rva::AccSite* site[6] = {
        &rva::ACC_PISTOL[0],   &rva::ACC_PISTOL[1],
        &rva::ACC_HITSCAN[0],  &rva::ACC_HITSCAN[1],
        &rva::ACC_EXPANDER[0], &rva::ACC_EXPANDER[1],
      };
      for (int i = 0; i < 6; i++) {
        CHECK(site[i]->rva == ex[i].rvaSite);
        CHECK(site[i]->centre == ex[i].centre);

        unsigned char got[ACC_SITE_BYTES];
        acc_expected_bytes(site[i]->rva, got);
        CHECK(memcmp(got, ex[i].bytes, ACC_SITE_BYTES) == 0);   // literals == derivation

        // Spelled out here rather than delegated, so a mutation of
        // acc_expected_bytes cannot mark its own homework.
        const unsigned rel = rva::KRAND_THUNK_RVA - (ex[i].rvaSite + 5u);
        CHECK(got[0] == 0xE8);                                   // CALL rel32
        CHECK(got[1] == (unsigned char)(rel & 0xFFu));
        CHECK(got[2] == (unsigned char)((rel >> 8) & 0xFFu));
        CHECK(got[3] == (unsigned char)((rel >> 16) & 0xFFu));
        CHECK(got[4] == (unsigned char)((rel >> 24) & 0xFFu));
        CHECK(thunk_decode_target(ex[i].rvaSite, rel) == rva::KRAND_THUNK_RVA);
        CHECK(thunk_encode_rel32(ex[i].rvaSite, rva::KRAND_THUNK_RVA) == rel);

        unsigned char patch[ACC_SITE_BYTES];
        acc_patch_bytes(site[i]->centre, patch);
        CHECK(patch[0] == 0xB8);                                 // MOV EAX, imm32
        CHECK(patch[1] == ex[i].centre);
        CHECK(patch[2] == 0x00 && patch[3] == 0x00 && patch[4] == 0x00);
        // A patch that equalled the original would make every PatchBracket
        // test below vacuous.
        CHECK(memcmp(patch, got, ACC_SITE_BYTES) != 0);
      }
      // Six distinct addresses -- the expander pair is a different handler
      // from the shared hitscan pair, not a copy of it.
      for (int i = 0; i < 6; i++)
        for (int j = i + 1; j < 6; j++)
          CHECK(site[i]->rva != site[j]->rva);
      // Yaw sites centre on 16 (mask 0x1f), pitch sites on 128 (mask 0xff).
      CHECK(rva::ACC_PISTOL[0].centre == 0x10 && rva::ACC_PISTOL[1].centre == 0x80);
      CHECK(rva::ACC_HITSCAN[0].centre == 0x10 && rva::ACC_HITSCAN[1].centre == 0x80);
      CHECK(rva::ACC_EXPANDER[0].centre == 0x10 && rva::ACC_EXPANDER[1].centre == 0x80);
      // Never the enemy-fire expander sites (VA 0x004F5ED5 / 0x004F5EEC).
      CHECK(rva::ACC_EXPANDER[0].rva != 0x000F5ED5u && rva::ACC_EXPANDER[1].rva != 0x000F5ED5u);
      CHECK(rva::ACC_EXPANDER[0].rva != 0x000F5EECu && rva::ACC_EXPANDER[1].rva != 0x000F5EECu);
    }

    // The camera field table: offsets and widths pinned field by field against the PDB literals (unreachable from any
    // live test). A width one step too wide writes over the next field of a live player struct (horizoff at 702 and
    // wantweaponfire at 704 are two bytes apart).
    {
      struct FieldExpect { int field; unsigned off; int width; };
      const FieldExpect fe[CF_COUNT] = {
        { CF_POSX,        532, 4 }, { CF_OPOSX,       568, 4 },
        { CF_POSY,        536, 4 }, { CF_OPOSY,       572, 4 },
        { CF_POSZ,        540, 4 }, { CF_OPOSZ,       576, 4 },
        { CF_ANG,         652, 2 }, { CF_OANG,        654, 2 },
        { CF_HORIZ,       544, 4 }, { CF_OHORIZ,      548, 4 },
        { CF_HORIZOFF,    702, 2 }, { CF_OHORIZOFF,   552, 4 },
        { CF_PYOFF,       580, 4 }, { CF_OPYOFF,      584, 4 },
        { CF_ROTSCRNANG,  810, 2 }, { CF_OROTSCRNANG, 808, 2 },
        { CF_CURSECTNUM,  658, 2 }, { CF_POSZV,       596, 4 },
        { CF_LOOK_ANG,    660, 2 },
      };
      CHECK(CF_COUNT == 19);
      for (int f = 0; f < CF_COUNT; f++) {
        CHECK(fe[f].field == f);                       // the table is in enum order
        CHECK(camera_field_offset(f) == fe[f].off);
        CHECK(camera_field_width(f) == fe[f].width);
        CHECK(camera_field_offset(f) + (unsigned)camera_field_width(f) <= rva::PS_STRIDE);
      }
      // horizoff is a SHORT and ohorizoff is a LONG -- the one asymmetric
      // pair in the set, straight out of the PDB.
      CHECK(camera_field_width(CF_HORIZOFF) == 2 && camera_field_width(CF_OHORIZOFF) == 4);
      for (int a = 0; a < CF_COUNT; a++)
        for (int b = a + 1; b < CF_COUNT; b++)
          CHECK(camera_field_offset(a) != camera_field_offset(b));
      CHECK(camera_field_offset(CF_COUNT) == 0 && camera_field_width(CF_COUNT) == 0);
      CHECK(camera_field_offset(-1) == 0 && camera_field_width(-1) == 0);
    }

    // Which value each camera field receives: eleven distinct non-zero inputs including every o-member, so a mutation
    // collapsing an o-member onto its pair partner (the "laggy movement" defect) is caught. Zeroed fields stay zero,
    // and no real input is 0.
    {
      CameraFields c;
      c.posx = 111; c.posy = 222; c.posz = 333;
      c.ang = 444;  c.horiz = 555; c.cursectnum = 66;
      c.oposx = 1011; c.oposy = 1022; c.oposz = 1033;
      c.oang = 1044;  c.ohoriz = 1055;
      long v;
      const struct { int field; long want; } want[] = {
        { CF_POSX, 111 },  { CF_OPOSX, 1011 },
        { CF_POSY, 222 },  { CF_OPOSY, 1022 },
        { CF_POSZ, 333 },  { CF_OPOSZ, 1033 },
        { CF_ANG,  444 },  { CF_OANG,  1044 },
        { CF_HORIZ, 555 }, { CF_OHORIZ, 1055 },
        { CF_HORIZOFF, 0 }, { CF_OHORIZOFF, 0 },
        { CF_PYOFF, 0 }, { CF_OPYOFF, 0 },
        { CF_ROTSCRNANG, 0 }, { CF_OROTSCRNANG, 0 },
        { CF_CURSECTNUM, 66 }, { CF_POSZV, 0 },
        { CF_LOOK_ANG, 0 },
      };
      for (int i = 0; i < CF_COUNT; i++) {
        v = -12345;
        CHECK(camera_field_value(want[i].field, c, &v) && v == want[i].want);
      }
      v = -12345;
      CHECK(!camera_field_value(CF_COUNT, c, &v) && v == -12345);
      v = -12345;
      CHECK(!camera_field_value(-1, c, &v) && v == -12345);
      CHECK(!camera_field_value(CF_POSX, c, 0));
    }

    // The camera field RVAs from a player index: the same bounds and arithmetic as player_view_field_rvas (RVA_PS + idx
    // * PS_STRIDE + offset), but pure.
    {
      unsigned r[CF_COUNT];
      for (int f = 0; f < CF_COUNT; f++) r[f] = 0xDEADBEEFu;
      CHECK(camera_field_rvas(0, r));
      for (int f = 0; f < CF_COUNT; f++)
        CHECK(r[f] == rva::RVA_PS + camera_field_offset(f));
      CHECK(camera_field_rvas(3, r));
      for (int f = 0; f < CF_COUNT; f++)
        CHECK(r[f] == rva::RVA_PS + 3u * rva::PS_STRIDE + camera_field_offset(f));
      // The last legal slot works and the first illegal one does not, and a
      // refusal leaves the caller's array untouched.
      CHECK(camera_field_rvas(MAX_PLAYER_INDEX, r));
      CHECK(r[CF_POSX] == rva::RVA_PS + (unsigned)MAX_PLAYER_INDEX * rva::PS_STRIDE + 532u);
      const unsigned keep = r[CF_POSX];
      CHECK(!camera_field_rvas(MAX_PLAYER_INDEX + 1, r));
      CHECK(r[CF_POSX] == keep);
      CHECK(!camera_field_rvas(-1, r));
      CHECK(r[CF_POSX] == keep);
      CHECK(!camera_field_rvas(0, 0));

      // camera_ps_rva is THE BOUND camera_bracket_arm CANNOT APPLY ITSELF: the bracket writes at `psRva + offset` into
      // a buffer whose size it is never told, so a garbage player index is a 19-field write into arbitrary memory.
      // Every psRva in the DLL comes from here.
      {
        unsigned ps = 0xDEADBEEFu;
        CHECK(camera_ps_rva(0, &ps));
        CHECK(ps == rva::RVA_PS);
        CHECK(camera_ps_rva(1, &ps));
        CHECK(ps == rva::RVA_PS + rva::PS_STRIDE);          // stride, not sizeof anything
        CHECK(camera_ps_rva(MAX_PLAYER_INDEX, &ps));
        CHECK(ps == rva::RVA_PS + (unsigned)MAX_PLAYER_INDEX * rva::PS_STRIDE);
        unsigned r2[CF_COUNT];
        CHECK(camera_field_rvas(3, r2));
        CHECK(camera_ps_rva(3, &ps));
        CHECK(r2[CF_POSX] == ps + rva::PS_POSX);
        CHECK(r2[CF_OROTSCRNANG] == ps + rva::PS_OROTSCRNANG);
        // REFUSALS LEAVE *out ALONE, so a caller that ignores the bool does
        // not get a plausible-looking address out of a rejected index.
        ps = 0x11111111u;
        CHECK(!camera_ps_rva(MAX_PLAYER_INDEX + 1, &ps));
        CHECK(ps == 0x11111111u);
        CHECK(!camera_ps_rva(-1, &ps));
        CHECK(ps == 0x11111111u);
        CHECK(!camera_ps_rva(-1000, &ps));
        CHECK(ps == 0x11111111u);
        CHECK(!camera_ps_rva(0, 0));
      }
    }

    // The CameraBracket on a synthetic two-player buffer: every pair's two members start different, and different from
    // the substituted value, or a bracket writing only one member would hide.
    {
      unsigned char buf[2 * rva::PS_STRIDE];
      const unsigned psRva = rva::PS_STRIDE;    // the SECOND player struct
      for (unsigned i = 0; i < sizeof(buf); i++) buf[i] = (unsigned char)(i * 13 + 7);

      // 19 distinct starting values, none 0, none equal to any substituted
      // value, and negatives on the signed 4- and 2-byte fields so
      // read_field_bytes' sign extension is exercised on the round trip.
      const long start[CF_COUNT] = {
        1001, 1002,      // posx, oposx
        2001, 2002,      // posy, oposy
        -3001, -3002,    // posz, oposz
        1500, 1497,      // ang, oang
        -42, -37,        // horiz, ohoriz
        21, -22,         // horizoff (short), ohorizoff (long)
        640, 641,        // pyoff, opyoff
        300, 301,        // rotscrnang, orotscrnang
        77, -4096,       // cursectnum, poszv
        -123,            // look_ang
      };
      for (int f = 0; f < CF_COUNT; f++)
        write_field_bytes(buf, psRva + camera_field_offset(f), camera_field_width(f), start[f]);
      unsigned char pristine[sizeof(buf)];
      memcpy(pristine, buf, sizeof(buf));

      CameraFields cam;
      cam.posx = 9001; cam.posy = 9002; cam.posz = 9003;
      cam.ang = 512;   cam.horiz = 150; cam.cursectnum = 12;
      // The o-members are the PREVIOUS pose, deliberately different from
      // their pair partner: a pair really interpolates, so a fixture where
      // they agree cannot tell a collapsing arm from a correct one.
      cam.oposx = 8001; cam.oposy = 8002; cam.oposz = 8003;
      cam.oang = 500;   cam.ohoriz = 130;

      // A fresh bracket is disarmed, from poisoned storage: a constructor
      // that forgot to clear `armed` fails deterministically here rather
      // than depending on what was on the stack.
      {
        unsigned char storage[sizeof(CameraBracket)];
        memset(storage, 0xFF, sizeof(storage));
        CameraBracket* b = new (storage) CameraBracket();
        CHECK(!b->save.armed);
        CHECK(b->base == 0);
        CHECK(b->psRva == 0);
        CHECK(!camera_bracket_release(b));
        b->~CameraBracket();
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
      }

      {
        CameraBracket b;
        CHECK(!b.save.armed);
        CHECK(camera_bracket_arm(&b, buf, psRva, cam));
        CHECK(b.save.armed);
        CHECK(b.base == buf);
        CHECK(b.psRva == psRva);
        for (int f = 0; f < CF_COUNT; f++) CHECK(b.save.v[f] == start[f]);

        // Both members of every pair checked against their own distinct
        // value: since no start value equals any substituted one, a member
        // left unwritten or collapsed onto its pair partner fails here.
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSX, 4) == 9001);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OPOSX, 4) == 8001);
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSY, 4) == 9002);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OPOSY, 4) == 8002);
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSZ, 4) == 9003);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OPOSZ, 4) == 8003);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_ANG, 2) == 512);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_OANG, 2) == 500);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_HORIZ, 4) == 150);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_OHORIZ, 4) == 130);
        CHECK(read_field_bytes(buf, psRva + rva::PS_HORIZOFF, 2) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_OHORIZOFF, 4) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_PYOFF, 4) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OPYOFF, 4) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_ROTSCRNANG, 2) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OROTSCRNANG, 2) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_CURSECTNUM, 2) == 12);
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSZV, 4) == 0);
        CHECK(read_field_bytes(buf, psRva + rva::PS_OFFSET_LOOK_ANG, 2) == 0);
        // The FIRST player struct is untouched: an offset applied to the wrong
        // base would show up here and nowhere else.
        CHECK(memcmp(pristine, buf, rva::PS_STRIDE) == 0);

        // Never re-armed over a live save -- that save is the only copy of the
        // player's real position.
        CameraFields other;
        other.posx = 1; other.posy = 2; other.posz = 3;
        other.ang = 4; other.horiz = 5; other.cursectnum = 6;
        other.oposx = 11; other.oposy = 12; other.oposz = 13;
        other.oang = 14; other.ohoriz = 15;
        CHECK(!camera_bracket_arm(&b, buf, psRva, other));
        CHECK(b.save.v[CF_POSX] == 1001);
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSX, 4) == 9001);

        CHECK(camera_bracket_release(&b));
        CHECK(!b.save.armed);
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
        CHECK(!camera_bracket_release(&b));
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
      }
      CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);

      // The destructor is the restore: armed inside a scope, never released
      // explicitly, so no early return out of a render detour can skip it.
      {
        CameraBracket b;
        CHECK(camera_bracket_arm(&b, buf, psRva, cam));
        CHECK(memcmp(pristine, buf, sizeof(buf)) != 0);
      }
      CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);

      // Nested: the inner one saves what it finds (the outer's substitution)
      // and puts it back, so the outer still holds the real view on unwind.
      {
        CameraBracket outer;
        CHECK(camera_bracket_arm(&outer, buf, psRva, cam));
        {
          CameraFields inner_cam;
          inner_cam.posx = 4001; inner_cam.posy = 4002; inner_cam.posz = 4003;
          inner_cam.ang = 1024;  inner_cam.horiz = 60;  inner_cam.cursectnum = 33;
          inner_cam.oposx = 3901; inner_cam.oposy = 3902; inner_cam.oposz = 3903;
          inner_cam.oang = 1000;  inner_cam.ohoriz = 58;
          CameraBracket inner;
          CHECK(camera_bracket_arm(&inner, buf, psRva, inner_cam));
          CHECK(inner.save.v[CF_POSX] == 9001);      // the OUTER's substitution
          CHECK(inner.save.v[CF_OPOSX] == 8001);     // the OUTER's oposx, NOT its posx
          CHECK(inner.save.v[CF_CURSECTNUM] == 12);
          CHECK(read_field_bytes(buf, psRva + rva::PS_POSX, 4) == 4001);
        }
        CHECK(read_field_bytes(buf, psRva + rva::PS_POSX, 4) == 9001);
        CHECK(read_field_bytes(buf, psRva + rva::PS_CURSECTNUM, 2) == 12);
        CHECK(outer.save.v[CF_POSX] == 1001);
        CHECK(memcmp(pristine, buf, sizeof(buf)) != 0);
      }
      CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);

      {
        CameraBracket b;
        CHECK(!camera_bracket_arm(&b, 0, psRva, cam));
        CHECK(!b.save.armed);
        CHECK(!camera_bracket_arm(0, buf, psRva, cam));
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
      }
      CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
      CHECK(!camera_bracket_release(0));

      {
        CameraSave s;
        s.armed = false;
        CHECK(!camera_save_fields(0, psRva, &s));
        CHECK(!camera_save_fields(buf, psRva, 0));
        CHECK(!camera_substitute_fields(0, psRva, cam));
        CHECK(!camera_restore_fields(0, psRva, &s));
        CHECK(!camera_restore_fields(buf, psRva, 0));
        CHECK(camera_save_fields(buf, psRva, &s));
        CHECK(s.armed);
        // A restore after TWO substitutions still puts back the saved view,
        // not the most recent substitution: the save is a value, not a
        // recomputation.
        CHECK(camera_substitute_fields(buf, psRva, cam));
        CHECK(camera_substitute_fields(buf, psRva, cam));
        CHECK(camera_restore_fields(buf, psRva, &s));
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
        CHECK(!s.armed);
        CHECK(!camera_restore_fields(buf, psRva, &s));
        CHECK(memcmp(pristine, buf, sizeof(buf)) == 0);
      }

      // A player whose pairs already agree and whose zeroed fields are
      // already zero: writes are invisible here, so this only proves the
      // round trip still holds, never that the writes happen.
      {
        for (int f = 0; f < CF_COUNT; f++)
          write_field_bytes(buf, psRva + camera_field_offset(f), camera_field_width(f), 0);
        write_field_bytes(buf, psRva + rva::PS_POSX,  4, 55);
        write_field_bytes(buf, psRva + rva::PS_OPOSX, 4, 55);
        unsigned char flat[sizeof(buf)];
        memcpy(flat, buf, sizeof(buf));
        CameraBracket b;
        CHECK(camera_bracket_arm(&b, buf, psRva, cam));
        CHECK(b.save.v[CF_POSX] == 55 && b.save.v[CF_OPOSX] == 55);
        CHECK(b.save.v[CF_POSZV] == 0);
        CHECK(camera_bracket_release(&b));
        CHECK(memcmp(flat, buf, sizeof(buf)) == 0);
      }
    }

    // The PatchBracket on a synthetic code buffer big enough for the real site RVAs, so the bracket runs with
    // rva::ACC_* itself: a wrong constant in that table fails here.
    {
      const unsigned CODE_SIZE = 0x000F6000u;      // past 0x000F5E47 + 5
      // Page-aligned so a site's page is exactly `rva / 4096` -- exact
      // literals below instead of depending on where malloc landed.
      unsigned char* code = (unsigned char*)_aligned_malloc(CODE_SIZE, ACC_TEST_PAGE_SIZE);
      CHECK(code != 0);
      CHECK(code == 0 || acc_page_of(code) == code);
      if (code) {
        for (unsigned i = 0; i < CODE_SIZE; i++) code[i] = (unsigned char)(i * 5 + 1);
        const rva::AccSite all[6] = {
          rva::ACC_PISTOL[0],   rva::ACC_PISTOL[1],
          rva::ACC_HITSCAN[0],  rva::ACC_HITSCAN[1],
          rva::ACC_EXPANDER[0], rva::ACC_EXPANDER[1],
        };
        for (int i = 0; i < 6; i++)
          acc_expected_bytes(all[i].rva, code + all[i].rva);

        // Every write the bracket makes is checked from here on: without it
        // a release that unprotects a page before restoring its bytes would
        // pass unnoticed.
        acc_write_watch_fn() = acc_test_write_watch;

        // acc_sites_overlap: the duplicate rule, so a repeated site cannot save the patch as its own "original".
        {
          rva::AccSite two[2];
          two[0].rva = 1000; two[0].centre = 0x10;
          two[1].rva = 1000; two[1].centre = 0x80;
          CHECK(acc_sites_overlap(two, 2));    // the same site twice
          two[1].rva = 1004; CHECK(acc_sites_overlap(two, 2));   // shares the last byte
          two[1].rva = 1005; CHECK(!acc_sites_overlap(two, 2));  // exactly adjacent, disjoint
          two[1].rva = 996;  CHECK(acc_sites_overlap(two, 2));   // overlapping from below
          two[1].rva = 995;  CHECK(!acc_sites_overlap(two, 2));
          two[1].rva = 0;    CHECK(!acc_sites_overlap(two, 2));
          CHECK(!acc_sites_overlap(two, 1));
          CHECK(!acc_sites_overlap(two, 0));
          CHECK(!acc_sites_overlap(0, 2));
          CHECK(!acc_sites_overlap(rva::ACC_PISTOL, 2));
          CHECK(!acc_sites_overlap(rva::ACC_HITSCAN, 2));
          CHECK(!acc_sites_overlap(rva::ACC_EXPANDER, 2));
          CHECK(!acc_sites_overlap(all, 6));
          // A third entry is compared against BOTH earlier ones, not just its
          // neighbour -- an adjacent-pairs-only check would miss this.
          rva::AccSite three[3];
          three[0].rva = 1000; three[1].rva = 2000; three[2].rva = 1002;
          three[0].centre = three[1].centre = three[2].centre = 0x10;
          CHECK(acc_sites_overlap(three, 3));
        }

        CHECK(ACC_PAGE_SIZE == ACC_TEST_PAGE_SIZE);
        CHECK(ACC_MAX_PAGES == 2 * ACC_MAX_SITES);   // a site may straddle
        CHECK(acc_page_of(code) == code);
        CHECK(acc_page_of(code + 1) == code);
        CHECK(acc_page_of(code + ACC_PAGE_SIZE - 1u) == code);
        CHECK(acc_page_of(code + ACC_PAGE_SIZE) == code + ACC_PAGE_SIZE);
        CHECK(acc_page_of(0) == 0);
        {
          unsigned char* pg[ACC_MAX_PAGES];
          // Both sites of every pair share one page: a bracket that
          // restored only the first site's page would corrupt the second.
          CHECK(acc_collect_pages(code, rva::ACC_PISTOL, 2, pg) == 1);
          CHECK(pg[0] == code + 0x000F4000u);
          CHECK(acc_collect_pages(code, rva::ACC_HITSCAN, 2, pg) == 1);
          CHECK(pg[0] == code + 0x000F4000u);
          CHECK(acc_collect_pages(code, rva::ACC_EXPANDER, 2, pg) == 1);
          CHECK(pg[0] == code + 0x000F5000u);
          // All six sites are two pages, not six.
          CHECK(acc_collect_pages(code, all, 6, pg) == 2);
          CHECK(pg[0] == code + 0x000F4000u);
          CHECK(pg[1] == code + 0x000F5000u);
          rva::AccSite straddle[1];
          straddle[0].rva = ACC_PAGE_SIZE - 2u; straddle[0].centre = 0x10;
          CHECK(acc_collect_pages(code, straddle, 1, pg) == 2);
          CHECK(pg[0] == code);
          CHECK(pg[1] == code + ACC_PAGE_SIZE);
          straddle[0].rva = ACC_PAGE_SIZE - (unsigned)ACC_SITE_BYTES;
          CHECK(acc_collect_pages(code, straddle, 1, pg) == 1);
          CHECK(pg[0] == code);
          CHECK(acc_collect_pages(0, all, 6, pg) == -1);
          CHECK(acc_collect_pages(code, 0, 6, pg) == -1);
          CHECK(acc_collect_pages(code, all, 6, 0) == -1);
          CHECK(acc_collect_pages(code, all, 0, pg) == -1);
          CHECK(acc_collect_pages(code, all, -1, pg) == -1);
          CHECK(acc_collect_pages(code, all, ACC_MAX_SITES + 1, pg) == -1);
        }

        unsigned char* pristine = (unsigned char*)malloc(CODE_SIZE);
        CHECK(pristine != 0);
        if (pristine) {
          memcpy(pristine, code, CODE_SIZE);

          // No hook means no arm: a write the bracket cannot make the page writable for would land in read-only code.
          // The suite installs a stub, never the real VirtualProtect.
          {
            acc_protect_fn() = 0;
            PatchBracket b;
            CHECK(!patch_bracket_arm(&b, code, all, 2));
            CHECK(!b.armed);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_ok;

          {
            unsigned char storage[sizeof(PatchBracket)];
            memset(storage, 0xFF, sizeof(storage));
            PatchBracket* b = new (storage) PatchBracket();
            CHECK(!b->armed);
            CHECK(b->base == 0);
            CHECK(b->count == 0);
            CHECK(!patch_bracket_release(b));
            b->~PatchBracket();
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          {
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, rva::ACC_HITSCAN, 2));
            CHECK(b.armed);
            CHECK(b.base == code);
            CHECK(b.count == 2);
            for (int i = 0; i < 2; i++) {
              CHECK(b.save[i].rva == rva::ACC_HITSCAN[i].rva);
              unsigned char want[ACC_SITE_BYTES];
              acc_expected_bytes(rva::ACC_HITSCAN[i].rva, want);
              CHECK(memcmp(b.save[i].orig, want, ACC_SITE_BYTES) == 0);
              unsigned char patch[ACC_SITE_BYTES];
              acc_patch_bytes(rva::ACC_HITSCAN[i].centre, patch);
              CHECK(memcmp(code + rva::ACC_HITSCAN[i].rva, patch, ACC_SITE_BYTES) == 0);
            }
            int changed = 0;
            for (unsigned i = 0; i < CODE_SIZE; i++) if (code[i] != pristine[i]) changed++;
            CHECK(changed == 2 * ACC_SITE_BYTES);
            CHECK(memcmp(code + rva::ACC_PISTOL[0].rva,
                         pristine + rva::ACC_PISTOL[0].rva, ACC_SITE_BYTES) == 0);
            CHECK(memcmp(code + rva::ACC_EXPANDER[1].rva,
                         pristine + rva::ACC_EXPANDER[1].rva, ACC_SITE_BYTES) == 0);

            CHECK(!patch_bracket_arm(&b, code, rva::ACC_PISTOL, 2));
            CHECK(memcmp(code + rva::ACC_PISTOL[0].rva,
                         pristine + rva::ACC_PISTOL[0].rva, ACC_SITE_BYTES) == 0);

            CHECK(patch_bracket_release(&b));
            CHECK(!b.armed);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
            CHECK(!patch_bracket_release(&b));
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

          {
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, rva::ACC_PISTOL, 2));
            CHECK(memcmp(pristine, code, CODE_SIZE) != 0);
          }
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

          {
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, all, 6));
            int changed = 0;
            for (unsigned i = 0; i < CODE_SIZE; i++) if (code[i] != pristine[i]) changed++;
            CHECK(changed == 6 * ACC_SITE_BYTES);
            CHECK(patch_bracket_release(&b));
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // The fingerprint: one wrong byte at any site refuses all of them
          // and touches nothing. Driven at every byte of every site in turn.
          for (int s = 0; s < 6; s++) {
            for (int byteIdx = 0; byteIdx < ACC_SITE_BYTES; byteIdx++) {
              const unsigned at = all[s].rva + (unsigned)byteIdx;
              const unsigned char keep = code[at];
              code[at] = (unsigned char)(keep ^ 0xFF);
              {
                PatchBracket b;
                CHECK(!patch_bracket_arm(&b, code, all, 6));
                CHECK(!b.armed);
                int diff = 0;
                for (unsigned i = 0; i < CODE_SIZE; i++) if (code[i] != pristine[i]) diff++;
                CHECK(diff == 1);
              }
              CHECK(code[at] == (unsigned char)(keep ^ 0xFF));
              code[at] = keep;
            }
          }
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

          {
            PatchBracket b;
            CHECK(!patch_bracket_arm(&b, 0, all, 2));
            CHECK(!patch_bracket_arm(&b, code, 0, 2));
            CHECK(!patch_bracket_arm(&b, code, all, 0));
            CHECK(!patch_bracket_arm(&b, code, all, -1));
            CHECK(!patch_bracket_arm(&b, code, all, ACC_MAX_SITES + 1));
            CHECK(!patch_bracket_arm(0, code, all, 2));
            CHECK(!b.armed);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }
          CHECK(!patch_bracket_release(0));

          // Stubbed, never the real VirtualProtect; the stub records what it was asked for so the calls are pinned
          // rather than assumed.
          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, rva::ACC_EXPANDER, 2));
            // One call per page, not per site: both expander sites are on
            // page 0xF5, VirtualProtect's only granularity.
            CHECK(acc_test_protect_calls() == 1);
            CHECK(acc_test_protect_last_size() == ACC_TEST_PAGE_SIZE);
            CHECK(acc_test_protect_last_prot() == ACC_PROT_RWX);
            CHECK(patch_bracket_release(&b));
            CHECK(acc_test_protect_calls() == 2);
            CHECK(acc_test_protect_last_prot() == ACC_TEST_OLD_PROT);
            CHECK(acc_test_pages_writable() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // One page per pair, and the order on the way out: both pistol sites are on page 0xF4, so arming changes it
          // once and records one old protection, and release puts all ten bytes back before the page stops being
          // writable.
          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, rva::ACC_PISTOL, 2));
            CHECK(b.pageCount == 1);                          // one page, not one per site
            CHECK(b.pages[0].addr == code + 0x000F4000u);
            // The old protection is the page's, not the second call's: a
            // per-site record would "restore" .text to writable permanently.
            CHECK(b.pages[0].oldProt == ACC_TEST_OLD_PROT);
            CHECK(acc_test_protect_calls() == 1);
            CHECK(acc_test_write_calls() == 2);
            CHECK(acc_test_write_violations() == 0);
            CHECK(acc_test_pages_writable() == 1);
            CHECK(acc_test_ev_count() == 3);
            CHECK(acc_test_ev_kind(0) == ACC_EV_PROTECT);
            CHECK(acc_test_ev_prot(0) == ACC_PROT_RWX);
            CHECK(acc_test_ev_kind(1) == ACC_EV_WRITE);
            CHECK(acc_test_ev_kind(2) == ACC_EV_WRITE);

            const int evAtRelease = acc_test_ev_count();
            CHECK(patch_bracket_release(&b));
            CHECK(acc_test_write_violations() == 0);
            CHECK(acc_test_protect_calls() == 2);             // one restore, for the one page
            CHECK(acc_test_protect_last_prot() == ACC_TEST_OLD_PROT);
            CHECK(acc_test_pages_writable() == 0);
            CHECK(acc_test_page_prot(code + rva::ACC_PISTOL[0].rva) == ACC_TEST_OLD_PROT);
            CHECK(acc_test_page_prot(code + rva::ACC_PISTOL[1].rva) == ACC_TEST_OLD_PROT);
            // Release order read off the event log: every write, then every
            // protect. Restoring protection first fails even if bytes match.
            CHECK(acc_test_ev_count() == evAtRelease + 3);
            int firstProt = -1, lastWrite = -1;
            for (int e = evAtRelease; e < acc_test_ev_count(); e++) {
              if (acc_test_ev_kind(e) == ACC_EV_PROTECT && firstProt < 0) firstProt = e;
              if (acc_test_ev_kind(e) == ACC_EV_WRITE) lastWrite = e;
            }
            CHECK(firstProt >= 0);
            CHECK(lastWrite >= 0);
            CHECK(lastWrite < firstProt);
            CHECK(!acc_test_ev_overflow());
            CHECK(!acc_test_page_overflow());
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, all, 6));
            CHECK(b.pageCount == 2);
            CHECK(b.pages[0].addr != b.pages[1].addr);
            CHECK(b.pages[0].oldProt == ACC_TEST_OLD_PROT);
            CHECK(b.pages[1].oldProt == ACC_TEST_OLD_PROT);
            CHECK(acc_test_protect_calls() == 2);
            CHECK(acc_test_write_calls() == 6);
            CHECK(acc_test_write_violations() == 0);
            CHECK(acc_test_pages_writable() == 2);
            CHECK(patch_bracket_release(&b));
            CHECK(acc_test_protect_calls() == 4);
            CHECK(acc_test_write_calls() == 12);
            CHECK(acc_test_write_violations() == 0);
            CHECK(acc_test_pages_writable() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // A repeated site is refused: chaingun and shotgun share ACC_HITSCAN, so "both weapons on" lists each site
          // twice, and a loop that saves while writing would record the patch as its "original" and leak the patched
          // bytes on release.
          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            const rva::AccSite dup[4] = {
              rva::ACC_HITSCAN[0], rva::ACC_HITSCAN[1],
              rva::ACC_HITSCAN[0], rva::ACC_HITSCAN[1],
            };
            for (int i = 0; i < 4; i++) CHECK(acc_site_matches(code, dup[i].rva));
            {
              PatchBracket b;
              CHECK(!patch_bracket_arm(&b, code, dup, 4));
              CHECK(!b.armed);
              CHECK(b.pageCount == 0);
              CHECK(acc_test_protect_calls() == 0);   // nothing unprotected
              CHECK(acc_test_write_calls() == 0);     // nothing written
              CHECK(acc_test_pages_writable() == 0);
              CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
            }
            CHECK(acc_test_write_calls() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // The same two sites WITHOUT the duplicates are still accepted, so
          // the refusal above is the duplicate and not the list length.
          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            PatchBracket b;
            CHECK(patch_bracket_arm(&b, code, rva::ACC_HITSCAN, 2));
            CHECK(b.armed);
            CHECK(patch_bracket_release(&b));
            CHECK(acc_test_write_violations() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_ok;
            rva::AccSite ov[2];
            ov[0] = rva::ACC_HITSCAN[0];
            ov[1] = rva::ACC_HITSCAN[0];
            ov[1].rva += 2u;
            PatchBracket b;
            CHECK(!patch_bracket_arm(&b, code, ov, 2));
            CHECK(!b.armed);
            CHECK(acc_test_protect_calls() == 0);
            CHECK(acc_test_write_calls() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // A protection call that fails refuses the whole bracket and writes
          // nothing, even at sites whose protect already succeeded.
          {
            acc_test_protect_reset();
            acc_protect_fn() = acc_test_protect_fail_second;
            PatchBracket b;
            CHECK(!patch_bracket_arm(&b, code, all, 6));
            CHECK(!b.armed);
            CHECK(b.pageCount == 0);
            CHECK(acc_test_write_calls() == 0);
            CHECK(acc_test_write_violations() == 0);
            // The undo, not just the refusal: page 1 was writable before
            // page 2's call failed. Three calls: page 1 up, page 2 fails, page 1 back.
            CHECK(acc_test_protect_calls() == 3);
            CHECK(acc_test_pages_writable() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }
          acc_protect_fn() = 0;
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          CHECK(acc_test_write_viol_total() == 0);
          acc_write_watch_fn() = 0;

          free(pristine);
        }
        _aligned_free(code);
      }
    }
  }



// The general CodePatchBracket on the real displayrooms clamp table, reusing the PatchBracket block's
// acc_protect_fn/acc_write_watch_fn page model (the same two seams).
void tb_0059_wave_3_task_a1_general() {
    CHECK(CODE_PATCH_MAX_LEN == 8);
    CHECK(CODE_PATCH_MAX_SITES == 4);
    CHECK(CODE_PATCH_MAX_PAGES == 8);
    CHECK(DISPLAYROOMS_CLAMP_COUNT == 4);

    // Pinned against the named RVA constants and exact recorded bytes: a drifted table would still "work" but patch
    // wrong bytes.
    CHECK(DISPLAYROOMS_CLAMP_SITES[0].rva == rva::CLAMP_BRIDGE_RVA);
    CHECK(DISPLAYROOMS_CLAMP_SITES[1].rva == rva::CLAMP_SECTOR_RVA);
    CHECK(DISPLAYROOMS_CLAMP_SITES[2].rva == rva::CLAMP_HORIZ_HI_RVA);
    CHECK(DISPLAYROOMS_CLAMP_SITES[3].rva == rva::CLAMP_HORIZ_LO_RVA);
    CHECK(rva::CLAMP_BRIDGE_RVA   == 0x000C1455u);
    CHECK(rva::CLAMP_SECTOR_RVA   == 0x000C149Du);
    CHECK(rva::CLAMP_HORIZ_HI_RVA == 0x000C14F8u);
    CHECK(rva::CLAMP_HORIZ_LO_RVA == 0x000C150Cu);
    for (int i = 0; i < 4; i++) CHECK(DISPLAYROOMS_CLAMP_SITES[i].len == 2);
    const unsigned char wantExp[4][2] = {
      {0x75,0x40}, {0x7C,0x50}, {0x7E,0x0B}, {0x7D,0x09}
    };
    const unsigned char wantRep[4][2] = {
      {0xEB,0x40}, {0xEB,0x50}, {0xEB,0x0B}, {0xEB,0x09}
    };
    for (int i = 0; i < 4; i++) {
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].expected[0]    == wantExp[i][0]);
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].expected[1]    == wantExp[i][1]);
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].replacement[0] == wantRep[i][0]);
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].replacement[1] == wantRep[i][1]);
      // Every replacement's opcode is EB (JMP rel8), same length as the Jcc
      // it replaces; the displacement byte stays untouched.
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].replacement[0] == 0xEB);
      CHECK(DISPLAYROOMS_CLAMP_SITES[i].replacement[1] == DISPLAYROOMS_CLAMP_SITES[i].expected[1]);
    }

    const unsigned CODE_SIZE = 0x000C2000u;   // past CLAMP_HORIZ_LO_RVA + 2
    unsigned char* code = (unsigned char*)_aligned_malloc(CODE_SIZE, ACC_TEST_PAGE_SIZE);
    CHECK(code != 0);
    CHECK(code == 0 || acc_page_of(code) == code);
    if (code) {
      for (unsigned i = 0; i < CODE_SIZE; i++) code[i] = (unsigned char)(i * 7 + 3);
      for (int i = 0; i < 4; i++) {
        code[DISPLAYROOMS_CLAMP_SITES[i].rva + 0] = DISPLAYROOMS_CLAMP_SITES[i].expected[0];
        code[DISPLAYROOMS_CLAMP_SITES[i].rva + 1] = DISPLAYROOMS_CLAMP_SITES[i].expected[1];
      }
      acc_write_watch_fn() = acc_test_write_watch;

      {
        CodePatch two[2] = {};
        two[0].rva = 1000; two[0].len = 2;
        two[1].rva = 1000; two[1].len = 2;
        CHECK(code_patch_sites_overlap(two, 2));         // identical
        two[1].rva = 1001; CHECK(code_patch_sites_overlap(two, 2));    // shares one byte
        two[1].rva = 1002; CHECK(!code_patch_sites_overlap(two, 2));   // exactly adjacent
        two[1].rva = 999;  CHECK(code_patch_sites_overlap(two, 2));    // overlapping from below
        two[1].rva = 998;  CHECK(!code_patch_sites_overlap(two, 2));
        CHECK(!code_patch_sites_overlap(two, 1));
        CHECK(!code_patch_sites_overlap(0, 2));
        CHECK(!code_patch_sites_overlap(DISPLAYROOMS_CLAMP_SITES, 4));   // the real table never overlaps
      }

      {
        unsigned char* pg[CODE_PATCH_MAX_PAGES];
        CHECK(code_patch_collect_pages(code, DISPLAYROOMS_CLAMP_SITES, 4, pg) == 1);
        CHECK(pg[0] == code + 0x000C1000u);
        CHECK(code_patch_collect_pages(0, DISPLAYROOMS_CLAMP_SITES, 4, pg) == -1);
        CHECK(code_patch_collect_pages(code, 0, 4, pg) == -1);
        CHECK(code_patch_collect_pages(code, DISPLAYROOMS_CLAMP_SITES, 4, 0) == -1);
        CHECK(code_patch_collect_pages(code, DISPLAYROOMS_CLAMP_SITES, 0, pg) == -1);
        CHECK(code_patch_collect_pages(code, DISPLAYROOMS_CLAMP_SITES, -1, pg) == -1);
        CHECK(code_patch_collect_pages(code, DISPLAYROOMS_CLAMP_SITES, CODE_PATCH_MAX_SITES + 1, pg) == -1);
        CodePatch straddle[1] = {};
        straddle[0].rva = ACC_PAGE_SIZE - 1u; straddle[0].len = 2;
        CHECK(code_patch_collect_pages(code, straddle, 1, pg) == 2);
        CHECK(pg[0] == code);
        CHECK(pg[1] == code + ACC_PAGE_SIZE);
      }

      unsigned char* pristine = (unsigned char*)malloc(CODE_SIZE);
      CHECK(pristine != 0);
      if (pristine) {
        memcpy(pristine, code, CODE_SIZE);

        {
          acc_protect_fn() = 0;
          CodePatchBracket b;
          CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 4));
          CHECK(!b.armed);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
        }
        acc_test_protect_reset();
        acc_protect_fn() = acc_test_protect_ok;

        {
          unsigned char storage[sizeof(CodePatchBracket)];
          memset(storage, 0xFF, sizeof(storage));
          CodePatchBracket* b = new (storage) CodePatchBracket();
          CHECK(!b->armed);
          CHECK(b->base == 0);
          CHECK(b->count == 0);
          CHECK(!code_patch_bracket_release(b));
          b->~CodePatchBracket();
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
        }

        {
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_ok;
          CodePatchBracket b;
          CHECK(code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 4));
          CHECK(b.armed);
          CHECK(b.base == code);
          CHECK(b.count == 4);
          CHECK(b.pageCount == 1);         // ONE PAGE, not four
          CHECK(b.pages[0].addr == code + 0x000C1000u);
          for (int i = 0; i < 4; i++) {
            CHECK(b.save[i].rva == DISPLAYROOMS_CLAMP_SITES[i].rva);
            CHECK(b.save[i].len == 2);
            CHECK(memcmp(b.save[i].orig, DISPLAYROOMS_CLAMP_SITES[i].expected, 2) == 0);
            CHECK(memcmp(code + DISPLAYROOMS_CLAMP_SITES[i].rva,
                         DISPLAYROOMS_CLAMP_SITES[i].replacement, 2) == 0);
          }
          // Only four bytes differ, not eight: every site keeps its own
          // displacement byte, only the opcode byte changes.
          int changed = 0;
          for (unsigned i = 0; i < CODE_SIZE; i++) if (code[i] != pristine[i]) changed++;
          CHECK(changed == 4);

          CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 4));

          // Order read off the event log: one protect then every write in;
          // every write then one protect out. A release that unprotects
          // first would fail acc_test_write_violations() below.
          CHECK(acc_test_protect_calls() == 1);
          CHECK(acc_test_write_calls() == 4);
          CHECK(acc_test_write_violations() == 0);
          CHECK(acc_test_pages_writable() == 1);
          CHECK(acc_test_ev_count() == 5);
          CHECK(acc_test_ev_kind(0) == ACC_EV_PROTECT);
          for (int e = 1; e < 5; e++) CHECK(acc_test_ev_kind(e) == ACC_EV_WRITE);

          const int evAtRelease = acc_test_ev_count();
          CHECK(code_patch_bracket_release(&b));
          CHECK(!b.armed);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          CHECK(!code_patch_bracket_release(&b));
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          CHECK(acc_test_write_violations() == 0);
          CHECK(acc_test_protect_calls() == 2);
          CHECK(acc_test_pages_writable() == 0);
          CHECK(acc_test_ev_count() == evAtRelease + 5);
          int firstProt = -1, lastWrite = -1;
          for (int e = evAtRelease; e < acc_test_ev_count(); e++) {
            if (acc_test_ev_kind(e) == ACC_EV_PROTECT && firstProt < 0) firstProt = e;
            if (acc_test_ev_kind(e) == ACC_EV_WRITE) lastWrite = e;
          }
          CHECK(firstProt >= 0 && lastWrite >= 0);
          CHECK(lastWrite < firstProt);   // every write before the one protect
        }
        CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

        {
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_ok;
          CodePatchBracket b;
          CHECK(code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 4));
          CHECK(memcmp(pristine, code, CODE_SIZE) != 0);
        }
        CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

        for (int s = 0; s < 4; s++) {
          for (int byteIdx = 0; byteIdx < 2; byteIdx++) {
            const unsigned at = DISPLAYROOMS_CLAMP_SITES[s].rva + (unsigned)byteIdx;
            const unsigned char keep = code[at];
            code[at] = (unsigned char)(keep ^ 0xFF);
            {
              acc_test_protect_reset();
              acc_protect_fn() = acc_test_protect_ok;
              CodePatchBracket b;
              CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 4));
              CHECK(!b.armed);
              CHECK(acc_test_protect_calls() == 0);   // refused before any page touched
              int diff = 0;
              for (unsigned i = 0; i < CODE_SIZE; i++) if (code[i] != pristine[i]) diff++;
              CHECK(diff == 1);   // only the corrupted byte itself
            }
            code[at] = keep;
          }
        }
        CHECK(memcmp(pristine, code, CODE_SIZE) == 0);

        {
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_ok;
          CodePatchBracket b;
          CHECK(!code_patch_bracket_arm(&b, 0, DISPLAYROOMS_CLAMP_SITES, 4));
          CHECK(!code_patch_bracket_arm(&b, code, 0, 4));
          CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, 0));
          CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, -1));
          CHECK(!code_patch_bracket_arm(&b, code, DISPLAYROOMS_CLAMP_SITES, CODE_PATCH_MAX_SITES + 1));
          CHECK(!code_patch_bracket_arm(0, code, DISPLAYROOMS_CLAMP_SITES, 4));
          CHECK(!b.armed);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
        }
        CHECK(!code_patch_bracket_release(0));

        {
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_ok;
          CodePatch dup[2] = { DISPLAYROOMS_CLAMP_SITES[0], DISPLAYROOMS_CLAMP_SITES[0] };
          CodePatchBracket b;
          CHECK(!code_patch_bracket_arm(&b, code, dup, 2));
          CHECK(!b.armed);
          CHECK(acc_test_protect_calls() == 0);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
        }

        // Undo on mid-arm failure, on a synthetic two-page pair: the four
        // real sites all share one page, so this needs two placed far apart.
        {
          acc_test_protect_reset();
          acc_protect_fn() = acc_test_protect_fail_second;
          CodePatch twoPages[2] = {};
          twoPages[0].rva = 0x1000; twoPages[0].len = 2;
          twoPages[0].expected[0] = code[0x1000]; twoPages[0].expected[1] = code[0x1001];
          twoPages[0].replacement[0] = 0xEB; twoPages[0].replacement[1] = twoPages[0].expected[1];
          twoPages[1].rva = 0x5000; twoPages[1].len = 2;
          twoPages[1].expected[0] = code[0x5000]; twoPages[1].expected[1] = code[0x5001];
          twoPages[1].replacement[0] = 0xEB; twoPages[1].replacement[1] = twoPages[1].expected[1];
          CodePatchBracket b;
          CHECK(!code_patch_bracket_arm(&b, code, twoPages, 2));
          CHECK(!b.armed);
          CHECK(b.pageCount == 0);
          CHECK(acc_test_write_calls() == 0);
          CHECK(acc_test_protect_calls() == 3);   // page 1 up, page 2 fails, page 1 back
          CHECK(acc_test_pages_writable() == 0);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
        }
        acc_protect_fn() = 0;
        CHECK(acc_test_write_viol_total() == 0);
        acc_write_watch_fn() = 0;
        free(pristine);
      }
      _aligned_free(code);
    }
  }



// The free camera. camera.cpp, input.cpp and esp.cpp cannot link into test_host (they hook displayrooms and touch live
// engine memory); every decision they make lives in freecam.h/freecam.cpp, input.h, esp_layout.h, canvas.h or common.h
// and is pinned here.
void tb_0060_phase_4_task_3_free() {
    static short st[2048];
    for (int i = 0; i < 2048; i++)
      st[i] = (short)(16384.0 * sin((double)i * 2.0 * 3.14159265358979323846 / 2048.0) + 0.5);
    CHECK(st[0] == 0);
    CHECK(st[256] == 11585);
    CHECK(st[512] == 16384);
    // Index 1024, not the ESP diagnostic's 1536: this construction's +0.5
    // truncation rounds one short on the negative quarter (1536 -> -16383,
    // not -16384), which nothing here reads at full scale.
    CHECK(st[1024] == 0);

    CHECK(FREECAM_SPEED_MIN == 1);
    CHECK(FREECAM_SPEED_MAX == 500);
    CHECK(FREECAM_SPEED_DEFAULT == 100);   // a running player; the top is for crossing big maps
    CHECK(freecam_speed(500, false, false) == 5 * FREECAM_BASE_SPEED);
    CHECK(FREECAM_BASE_SPEED == 128);                    // a quarter metre a tic
    CHECK(FREECAM_BASE_SPEED == BUILD_UNITS_PER_METRE / 4);
    CHECK(FREECAM_Z_PER_XY == 16);                       // Build's z:xy ratio
    CHECK(FREECAM_ANG_UNITS == 2048);
    // The pitch envelope is the camera's own, not processinput's -99..299
    // player clamp: symmetric, in Build angle units, ~87.9 degrees each way.
    CHECK(FREECAM_PITCH_MAX == 500);

    // freecam_pitch_to_horiz: pitch 0 => horiz exactly 100 (level); 256 (45 deg) => 228 +-1; 500 (~87.9 deg) =>
    // 3500..3700.
    CHECK(freecam_pitch_to_horiz(0, st) == 100);
    {
      const int h256 = freecam_pitch_to_horiz(256, st);
      CHECK(h256 >= 227 && h256 <= 229);
    }
    {
      const int h500 = freecam_pitch_to_horiz(500, st);
      CHECK(h500 >= 3500 && h500 <= 3700);
    }
    // Symmetric within a unit, not bit-exact: this sintable's +0.5 truncation
    // rounds its negative quarter one short, so mirrored angles can differ
    // by a unit.
    {
      const int mirrored = 100 - (freecam_pitch_to_horiz(256, st) - 100);
      const int got = freecam_pitch_to_horiz(-256, st);
      CHECK(got >= mirrored - 1 && got <= mirrored + 1);
    }
    // Monotonic across the whole clamp range: freecam_horiz_to_pitch's
    // search below relies on this to find a close match.
    {
      bool monotonic = true;
      int last = freecam_pitch_to_horiz(-FREECAM_PITCH_MAX, st);
      for (int p = -FREECAM_PITCH_MAX + 1; p <= FREECAM_PITCH_MAX; p++) {
        const int h = freecam_pitch_to_horiz(p, st);
        if (h < last) monotonic = false;
        last = h;
      }
      CHECK(monotonic);
    }
    CHECK(freecam_pitch_to_horiz(500, nullptr) == 100);

    // freecam_horiz_to_pitch, the inverse: the player's horiz envelope (-99..299, AIM_HORIZ_MIN/MAX in target.h)
    // round-trips through both conversions within a couple of units; it is what freecam_enter converts.
    CHECK(freecam_horiz_to_pitch(100, st) == 0);   // level round-trips to level
    for (int h = AIM_HORIZ_MIN; h <= AIM_HORIZ_MAX; h += 5) {
      const int p = freecam_horiz_to_pitch(h, st);
      CHECK(p >= -FREECAM_PITCH_MAX && p <= FREECAM_PITCH_MAX);
      const int back = freecam_pitch_to_horiz(p, st);
      CHECK((back - h >= -2) && (back - h <= 2));   // within a couple of units
    }
    CHECK(freecam_horiz_to_pitch(9999, nullptr) == 0);
    // Beyond the player's envelope the search stays inside the camera's clamp and round-trips reasonably; it probes the
    // reachable maximum itself, not a value already past it, which would pass by saturation.
    {
      const int horizAtMax = freecam_pitch_to_horiz(FREECAM_PITCH_MAX, st);
      const int pHigh = freecam_horiz_to_pitch(horizAtMax - 5, st);
      CHECK(pHigh >= 490 && pHigh <= 500);
      const int horizAtMin = freecam_pitch_to_horiz(-FREECAM_PITCH_MAX, st);
      const int pLow = freecam_horiz_to_pitch(horizAtMin + 5, st);
      CHECK(pLow <= -490 && pLow >= -500);
    }

    CHECK(freecam_speed_clamped(50) == 50);
    CHECK(freecam_speed_clamped(0) == FREECAM_SPEED_MIN);
    CHECK(freecam_speed_clamped(-9999) == FREECAM_SPEED_MIN);
    CHECK(freecam_speed_clamped(501) == FREECAM_SPEED_MAX);
    // The cursor is snapped back once it strays this far from the anchor: left to the game's once-a-frame snap, it
    // reaches the screen edge on a slow frame and the OS stops reporting motion.
    CHECK(FREECAM_RECENTRE_PX == 192);
    CHECK(freecam_screen_centre(3440) == 1720);
    CHECK(freecam_screen_centre(1440) == 720);
    CHECK(freecam_screen_centre(64) == 0);     // too small to be a real screen
    CHECK(freecam_screen_centre(0) == 0);
    CHECK(freecam_screen_centre(-8) == 0);
    // A cursor at the bottom edge of a 1440-tall screen is well past the limit from its centre, the case that went
    // unnoticed while the test measured against a target that follows the cursor.
    CHECK(freecam_should_recentre(true, 1720, 720, 1700, 1433, FREECAM_RECENTRE_PX));
    CHECK(!freecam_should_recentre(true, 1720, 720, 1700, 800, FREECAM_RECENTRE_PX));
    CHECK(!freecam_should_recentre(true, 1000, 500, 1100, 560, 192));   // inside
    CHECK(freecam_should_recentre(true, 1000, 500, 1200, 500, 192));    // right
    CHECK(freecam_should_recentre(true, 1000, 500, 700, 500, 192));     // left
    CHECK(freecam_should_recentre(true, 1000, 500, 1000, 100, 192));    // up
    CHECK(freecam_should_recentre(true, 1000, 500, 1000, 900, 192));    // down
    CHECK(!freecam_should_recentre(false, 1000, 500, 3000, 500, 192));  // no anchor yet
    CHECK(!freecam_should_recentre(true, 1000, 500, 3000, 500, 0));     // disabled
    CHECK(!freecam_should_recentre(true, 1000, 500, 1192, 500, 192));   // exactly at the limit
    CHECK(freecam_speed_clamped(101) == 101);
    CHECK(freecam_speed_clamped(999999) == FREECAM_SPEED_MAX);
    CHECK(freecam_speed_clamped(FREECAM_SPEED_MIN) == FREECAM_SPEED_MIN);
    CHECK(freecam_speed_clamped(FREECAM_SPEED_MAX) == FREECAM_SPEED_MAX);

    CHECK(freecam_speed(100, false, false) == FREECAM_BASE_SPEED);
    CHECK(freecam_speed(50, false, false) == FREECAM_BASE_SPEED / 2);
    CHECK(freecam_speed(25, false, false) == FREECAM_BASE_SPEED / 4);
    // Fast doubles and slow halves, each checked against the same
    // unmodified speed rather than a literal, so a swapped pair cannot
    // pass by both being wrong together.
    CHECK(freecam_speed(100, true,  false) == 2 * freecam_speed(100, false, false));
    CHECK(freecam_speed(100, false, true)  == freecam_speed(100, false, false) / 2);
    CHECK(freecam_speed(100, true,  false) >  freecam_speed(100, false, false));
    CHECK(freecam_speed(100, false, true)  <  freecam_speed(100, false, false));
    CHECK(freecam_speed(60,  true,  false) == 2 * freecam_speed(60,  false, false));
    CHECK(freecam_speed(60,  false, true)  == freecam_speed(60,  false, false) / 2);
    // Held together they cancel: the reading that cannot leave the camera stopped.
    CHECK(freecam_speed(100, true, true) == freecam_speed(100, false, false));
    // Never zero, at any percentage or modifier: a frozen camera looks broken.
    {
      bool everZero = false, everNegative = false;
      for (int pc = FREECAM_SPEED_MIN; pc <= FREECAM_SPEED_MAX; pc++)
        for (int f = 0; f < 2; f++)
          for (int sl = 0; sl < 2; sl++) {
            const int v = freecam_speed(pc, f != 0, sl != 0);
            if (v == 0) everZero = true;
            if (v < 0) everNegative = true;
          }
      CHECK(!everZero);
      CHECK(!everNegative);
    }
    // Clamped inside the speed too, so a value that reached g_cfg by some
    // route other than config_load still lands in range.
    CHECK(freecam_speed(0, false, false) == freecam_speed(FREECAM_SPEED_MIN, false, false));
    CHECK(freecam_speed(9999, false, false) == freecam_speed(FREECAM_SPEED_MAX, false, false));

    // The camera's turn rate derives from the modern mouse aim's setting, in milli angle units per mouse count (the old
    // percent is retired); the relationships the percent block pinned are restated on the new scale. Derivation,
    // sub-unit carry and config retirement are tb_0115's.
    {
      CHECK(FREECAM_SENS_MILLI_UNIT == 1000);   // one Build angle unit a count
      CHECK(FREECAM_SENS_MILLI_MIN == 1);       // 0 would freeze the camera
      CHECK(FREECAM_SENS_MILLI_MAX == 10000);
      // The default is the identity on both axes: what the camera did before
      // the setting existed, and still does when modern mouse aim is off.
      CHECK(freecam_sens_default().yawMilli == FREECAM_SENS_MILLI_UNIT);
      CHECK(freecam_sens_default().pitchMilli == FREECAM_SENS_MILLI_UNIT);
      CHECK(freecam_sens_milli_clamped(1000) == 1000);
      CHECK(freecam_sens_milli_clamped(FREECAM_SENS_MILLI_MIN) == FREECAM_SENS_MILLI_MIN);
      CHECK(freecam_sens_milli_clamped(FREECAM_SENS_MILLI_MAX) == FREECAM_SENS_MILLI_MAX);
      CHECK(freecam_sens_milli_clamped(0) == FREECAM_SENS_MILLI_MIN);
      CHECK(freecam_sens_milli_clamped(-99999) == FREECAM_SENS_MILLI_MIN);
      CHECK(freecam_sens_milli_clamped(FREECAM_SENS_MILLI_MAX + 1) == FREECAM_SENS_MILLI_MAX);
      CHECK(freecam_sens_milli_clamped(999999) == FREECAM_SENS_MILLI_MAX);
      // `pct` bridges the retired percent to the milli scale, so every
      // fixture below keeps its meaning: 100% is 1000 milli, the identity.
      CHECK(pct(100).yawMilli == FREECAM_SENS_MILLI_UNIT);
      CHECK(pct(100).pitchMilli == FREECAM_SENS_MILLI_UNIT);
      CHECK(pct(25).yawMilli == 250 && pct(25).pitchMilli == 250);
      CHECK(pct(400).yawMilli == 4000 && pct(400).pitchMilli == 4000);
    }
    {
      short table[FREECAM_ANG_UNITS];
      for (int i = 0; i < FREECAM_ANG_UNITS; i++)
        table[i] = (short)(16384.0 * sin(6.283185307179586 * i / FREECAM_ANG_UNITS));

      FreecamState s = { 1000, 2000, -3000, 0, 0, true, 0, 0, 0, 0, 0 };
      FreecamInput in = { 40, 20, false, false, false, false, false, false, false, false };

      const FreecamState at100 = freecam_step(s, in, 100, pct(100), table);
      const FreecamState at400 = freecam_step(s, in, 100, pct(400), table);
      const FreecamState at25  = freecam_step(s, in, 100, pct(25),  table);
      CHECK(at100.ang == 40);
      CHECK(at400.ang == 160);
      CHECK(at25.ang == 10);
      // A higher rate turns further, stated as a relationship: a scale that
      // only ever divided passes one literal above and fails this.
      CHECK(at400.ang > at100.ang && at100.ang > at25.ang);
      // Pitch must move too -- a scale applied to dx alone is the mutation this pins.
      CHECK(at100.pitch == -20);
      CHECK(at400.pitch == -80);
      CHECK(at25.pitch == -5);
      FreecamInput back = { -40, -20, false, false, false, false, false, false, false, false };
      const FreecamState neg400 = freecam_step(s, back, 100, pct(400), table);
      CHECK(neg400.ang == FREECAM_ANG_UNITS - 160);   // wrapped, not negative
      CHECK(neg400.pitch == 80);
      // The rate is not the speed: a speed change must not move the view and
      // a rate change must not move the camera.
      const FreecamState slow = freecam_step(s, in, 25, pct(100), table);
      CHECK(slow.ang == at100.ang && slow.pitch == at100.pitch);
      const FreecamState sens = freecam_step(s, in, 100, pct(400), table);
      CHECK(sens.x == at400.x && sens.y == at400.y);   // same speed, same travel
      const FreecamSens yawOnly = { 4000, 1000 };
      const FreecamState onlyY = freecam_step(s, in, 100, yawOnly, table);
      CHECK(onlyY.ang == 160);
      CHECK(onlyY.pitch == -20);
      const FreecamSens pitchOnly = { 1000, 4000 };
      const FreecamState onlyP = freecam_step(s, in, 100, pitchOnly, table);
      CHECK(onlyP.ang == 40);
      CHECK(onlyP.pitch == -80);
      const FreecamState nulled = freecam_step(s, in, 100, pct(400), 0);
      CHECK(nulled.ang == s.ang && nulled.pitch == s.pitch);
      CHECK(nulled.x == s.x && nulled.y == s.y && nulled.z == s.z);
      CHECK(nulled.yawRem == s.yawRem && nulled.pitchRem == s.pitchRem);
    }
    {
      // The large-flick collapse measures against the delta actually
      // applied, not the raw one: at 4x rate dx=300 becomes 1200 (past half
      // a turn, collapses); at 0.25x it becomes 75 (must not collapse).
      short table[FREECAM_ANG_UNITS];
      for (int i = 0; i < FREECAM_ANG_UNITS; i++)
        table[i] = (short)(16384.0 * sin(6.283185307179586 * i / FREECAM_ANG_UNITS));
      FreecamState s = { 0, 0, 0, 0, 0, true, 0, 0, 0, 0, 0 };
      FreecamInput flick = { 300, 0, false, false, false, false, false, false, false, false };
      const FreecamState big = freecam_step(s, flick, 100, pct(400), table);
      CHECK(big.ang == 1200);
      CHECK(big.pang == big.ang);              // collapsed: nothing honest to interpolate from
      const FreecamState small = freecam_step(s, flick, 100, pct(25), table);
      CHECK(small.ang == 75);
      CHECK(small.pang == 0);                  // NOT collapsed: the real previous angle survives
    }

    // freecam_step: one zeroed input and one fixed state reused by every case, so a case states only the ONE thing it
    // is about.
    FreecamInput none = { 0, 0, false, false, false, false, false, false, false, false };
    FreecamState origin = { 1000, 2000, 3000, 0, 0, true };

    // A ZERO INPUT LEAVES THE STATE UNCHANGED, active included: a step that quietly switched the camera off would look
    // like a failed toggle.
    {
      const FreecamState o = freecam_step(origin, none, 100, pct(100), st);
      CHECK(o.x == origin.x);
      CHECK(o.y == origin.y);
      CHECK(o.z == origin.z);
      CHECK(o.ang == origin.ang);
      CHECK(o.pitch == origin.pitch);
      CHECK(o.active == origin.active);
    }

    // FORWARD MOVES ALONG ang BY THE SPEED. At ang 0 the camera faces +x (cos = sintable[512] = 16384, sin =
    // sintable[0] = 0), so a full-speed tic is exactly FREECAM_BASE_SPEED along x and nothing along y.
    {
      FreecamInput in = none; in.fwd = true;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.x == origin.x + FREECAM_BASE_SPEED);
      CHECK(o.y == origin.y);
      CHECK(o.z == origin.z);        // forward is FLAT: no height change
      CHECK(o.ang == origin.ang);
      CHECK(o.pitch == origin.pitch);
    }
    {
      FreecamInput f = none; f.fwd = true;
      FreecamInput b = none; b.back = true;
      const FreecamState of = freecam_step(origin, f, 100, pct(100), st);
      const FreecamState ob = freecam_step(origin, b, 100, pct(100), st);
      CHECK(of.x - origin.x == -(ob.x - origin.x));
      CHECK(of.y - origin.y == -(ob.y - origin.y));
      CHECK(ob.x == origin.x - FREECAM_BASE_SPEED);
    }
    {
      FreecamInput in = none; in.fwd = true; in.back = true;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.x == origin.x);
      CHECK(o.y == origin.y);
    }

    {
      FreecamInput f = none; f.fwd = true;
      FreecamInput r = none; r.right = true;
      FreecamInput l = none; l.left = true;
      const FreecamState of  = freecam_step(origin, f, 100, pct(100), st);
      const FreecamState orr = freecam_step(origin, r, 100, pct(100), st);
      const FreecamState ol  = freecam_step(origin, l, 100, pct(100), st);
      CHECK(of.y - origin.y == 0);            // forward: no lateral component
      CHECK(orr.x - origin.x == 0);           // right: no forward component
      CHECK(ol.x - origin.x == 0);            // left: no forward component
      CHECK(orr.y - origin.y == FREECAM_BASE_SPEED);
      CHECK(ol.y - origin.y == -FREECAM_BASE_SPEED);
      CHECK(orr.y - origin.y == -(ol.y - origin.y));
      CHECK(orr.y - origin.y == of.x - origin.x);
    }
    // Right is ang + 512, Build's handedness: facing +x (ang 0), +y is the
    // right of the screen. Strafing right from ang 0 lands where walking
    // forward from ang 512 lands.
    {
      FreecamInput r = none; r.right = true;
      FreecamInput f = none; f.fwd = true;
      FreecamState turned = origin; turned.ang = 512;
      const FreecamState orr = freecam_step(origin, r, 100, pct(100), st);
      const FreecamState off = freecam_step(turned, f, 100, pct(100), st);
      CHECK(orr.x - origin.x == off.x - turned.x);
      CHECK(orr.y - origin.y == off.y - turned.y);
    }

    // Up/down change z only; up is -z since Build's z grows downward.
    {
      FreecamInput u = none; u.up = true;
      FreecamInput d = none; d.down = true;
      const FreecamState ou = freecam_step(origin, u, 100, pct(100), st);
      const FreecamState od = freecam_step(origin, d, 100, pct(100), st);
      CHECK(ou.x == origin.x && ou.y == origin.y);
      CHECK(od.x == origin.x && od.y == origin.y);
      CHECK(ou.z < origin.z);      // up is a FALLING z
      CHECK(od.z > origin.z);
      // Scaled by the z:xy ratio, so a vertical tic covers the same real
      // distance a forward one does.
      CHECK(origin.z - ou.z == FREECAM_BASE_SPEED * FREECAM_Z_PER_XY);
      CHECK(od.z - origin.z == FREECAM_BASE_SPEED * FREECAM_Z_PER_XY);
    }
    {
      FreecamInput in = none; in.up = true; in.down = true;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.z == origin.z);
    }

    // ang 256: sin and cos are both non-zero and equal (11585 each). A step
    // that dropped one term, or read the table at the wrong offset, is
    // invisible at ang 0 and obvious here.
    {
      FreecamState diag = origin; diag.ang = 256;
      FreecamInput f = none; f.fwd = true;
      const FreecamState o = freecam_step(diag, f, 100, pct(100), st);
      const int dx = o.x - diag.x, dy = o.y - diag.y;
      CHECK(dx != 0);
      CHECK(dy != 0);
      CHECK(dx == dy);                          // 45 degrees: equal components
      CHECK(dx == (FREECAM_BASE_SPEED * 11585) >> 14);
      CHECK(dx == 90);
      CHECK(dx < FREECAM_BASE_SPEED);
      FreecamInput r = none; r.right = true;
      const FreecamState orr = freecam_step(diag, r, 100, pct(100), st);
      CHECK(orr.x - diag.x < 0);
      CHECK(orr.y - diag.y > 0);
      CHECK(orr.y - diag.y == dy);
    }

    {
      FreecamInput in = none; in.dx = 100;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.ang == 100);
      CHECK(o.pitch == origin.pitch);   // yaw alone: the pitch is untouched
      CHECK(o.x == origin.x && o.y == origin.y && o.z == origin.z);
    }
    {
      FreecamState near2047 = origin; near2047.ang = 2040;
      FreecamInput in = none; in.dx = 10;
      const FreecamState o = freecam_step(near2047, in, 100, pct(100), st);
      CHECK(o.ang == 2);            // wrapped, not 2050
      CHECK(o.ang >= 0 && o.ang < FREECAM_ANG_UNITS);
    }
    {
      FreecamState low = origin; low.ang = 5;
      FreecamInput in = none; in.dx = -10;
      const FreecamState o = freecam_step(low, in, 100, pct(100), st);
      CHECK(o.ang == 2043);         // wrapped the other way, never negative
      CHECK(o.ang >= 0 && o.ang < FREECAM_ANG_UNITS);
    }
    {
      bool inRange = true;
      FreecamState s = origin;
      FreecamInput in = none; in.dx = 37;
      for (int i = 0; i < 500; i++) {
        s = freecam_step(s, in, 100, pct(100), st);
        if (s.ang < 0 || s.ang >= FREECAM_ANG_UNITS) inRange = false;
      }
      CHECK(inRange);
      FreecamInput big = none; big.dx = 1000000;
      const FreecamState o = freecam_step(origin, big, 100, pct(100), st);
      CHECK(o.ang >= 0 && o.ang < FREECAM_ANG_UNITS);
      FreecamInput bigNeg = none; bigNeg.dx = -1000000;
      const FreecamState o2 = freecam_step(origin, bigNeg, 100, pct(100), st);
      CHECK(o2.ang >= 0 && o2.ang < FREECAM_ANG_UNITS);
    }
    {
      FreecamInput in = none; in.dx = 512; in.fwd = true;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.ang == 512);
      CHECK(o.x == origin.x);                                // not along the OLD ang
      CHECK(o.y == origin.y + FREECAM_BASE_SPEED);
    }

    // SDL's y grows downward, so a positive dy (mouse pushed forward) looks
    // down, a falling pitch.
    {
      FreecamInput in = none; in.dy = 20;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.pitch == origin.pitch - 20);
      CHECK(o.ang == origin.ang);      // pitch alone: the yaw is untouched
    }
    {
      FreecamInput in = none; in.dy = -20;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.pitch == origin.pitch + 20);
    }
    {
      FreecamInput in = none; in.dy = -100000;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.pitch == FREECAM_PITCH_MAX);
      CHECK(o.pitch == 500);
      const FreecamState o2 = freecam_step(o, in, 100, pct(100), st);
      CHECK(o2.pitch == FREECAM_PITCH_MAX);   // does not creep past on a second tic
    }
    {
      FreecamInput in = none; in.dy = 100000;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), st);
      CHECK(o.pitch == -FREECAM_PITCH_MAX);
      CHECK(o.pitch == -500);
      const FreecamState o2 = freecam_step(o, in, 100, pct(100), st);
      CHECK(o2.pitch == -FREECAM_PITCH_MAX);
    }
    {
      bool inRange = true;
      FreecamState s = origin;
      FreecamInput dn = none; dn.dy = 13;
      FreecamInput up = none; up.dy = -13;
      for (int i = 0; i < 200; i++) {
        s = freecam_step(s, dn, 100, pct(100), st);
        if (s.pitch < -FREECAM_PITCH_MAX || s.pitch > FREECAM_PITCH_MAX) inRange = false;
      }
      for (int i = 0; i < 400; i++) {
        s = freecam_step(s, up, 100, pct(100), st);
        if (s.pitch < -FREECAM_PITCH_MAX || s.pitch > FREECAM_PITCH_MAX) inRange = false;
      }
      CHECK(inRange);
    }
    {
      FreecamInput f = none; f.fwd = true;
      const FreecamState full = freecam_step(origin, f, 100, pct(100), st);
      const FreecamState half = freecam_step(origin, f, 50, pct(100), st);
      CHECK(full.x - origin.x == FREECAM_BASE_SPEED);
      CHECK(half.x - origin.x == FREECAM_BASE_SPEED / 2);
      FreecamInput ff = f; ff.fast = true;
      FreecamInput fs = f; fs.slow = true;
      const FreecamState fast = freecam_step(origin, ff, 100, pct(100), st);
      const FreecamState slow = freecam_step(origin, fs, 100, pct(100), st);
      CHECK(fast.x - origin.x == 2 * (full.x - origin.x));
      CHECK(slow.x - origin.x == (full.x - origin.x) / 2);
      FreecamInput uf = none; uf.up = true; uf.fast = true;
      FreecamInput u  = none; u.up = true;
      CHECK(origin.z - freecam_step(origin, uf, 100, pct(100), st).z
            == 2 * (origin.z - freecam_step(origin, u, 100, pct(100), st).z));
    }
    // A null sintable refuses to move rather than indexing it: the table is
    // read from live engine memory, so this is a crash guard.
    {
      FreecamInput in = none; in.fwd = true; in.dx = 50; in.dy = 50; in.up = true;
      const FreecamState o = freecam_step(origin, in, 100, pct(100), nullptr);
      CHECK(o.x == origin.x && o.y == origin.y && o.z == origin.z);
      CHECK(o.ang == origin.ang && o.pitch == origin.pitch);
    }

    {
      FreecamInput f = none; f.fwd = true; f.dx = 37; f.dy = -11;
      const FreecamState o = freecam_step(origin, f, 100, pct(100), st);
      CHECK(o.px == origin.x);
      CHECK(o.py == origin.y);
      CHECK(o.pz == origin.z);
      CHECK(o.pang == origin.ang);
      CHECK(o.ppitch == origin.pitch);
      CHECK(o.x != o.px);
      CHECK(o.y != o.py || o.x != o.px);   // at least one axis moved
      CHECK(o.ang != o.pang);
      CHECK(o.pitch != o.ppitch);

      const FreecamState o2 = freecam_step(o, f, 100, pct(100), st);
      CHECK(o2.px == o.x);
      CHECK(o2.py == o.y);
      CHECK(o2.pang == o.ang);
      CHECK(o2.ppitch == o.pitch);

      const FreecamState held = freecam_step(origin, none, 100, pct(100), st);
      CHECK(held.x == origin.x && held.px == origin.x);
      CHECK(held.pitch == origin.pitch && held.ppitch == origin.pitch);
    }
    // A fast flick collapses the ang pair: displayrooms resolves the interpolation pair as `((ang - oang + 1024) &
    // 2047) - 1024`, the short way round, so a flick past half a turn would sweep the view backwards for one tic; the
    // pair collapses (`pang = ang`) whenever the two disagree.
    {
      FreecamInput flick = none; flick.dx = 1500;
      const FreecamState big = freecam_step(origin, flick, 100, pct(100), st);
      CHECK(big.pang == big.ang);
      CHECK(big.ang == ((origin.ang + 1500) & (FREECAM_ANG_UNITS - 1)));

      FreecamInput turn = none; turn.dx = 300;
      const FreecamState small = freecam_step(origin, turn, 100, pct(100), st);
      CHECK(small.pang == origin.ang);
      CHECK(small.pang != small.ang);
      CHECK(small.ang == ((origin.ang + 300) & (FREECAM_ANG_UNITS - 1)));

      // The wrap-resolved delta always lands in [-1024, 1023]: 1023 is the
      // last value that agrees (no collapse), 1024 the first that collapses.
      FreecamInput atBoundary = none; atBoundary.dx = 1023;
      FreecamInput overBoundary = none; overBoundary.dx = 1024;
      const FreecamState atB = freecam_step(origin, atBoundary, 100, pct(100), st);
      const FreecamState overB = freecam_step(origin, overBoundary, 100, pct(100), st);
      CHECK(atB.pang == origin.ang);      // still the previous value: not collapsed
      CHECK(atB.pang != atB.ang);
      CHECK(overB.pang == overB.ang);     // collapsed

      FreecamInput negFlick = none; negFlick.dx = -1500;
      const FreecamState negBig = freecam_step(origin, negFlick, 100, pct(100), st);
      CHECK(negBig.pang == negBig.ang);
    }
    {
      PlayerPos p;
      p.posx = 777; p.posy = -888; p.posz = 999;
      p.oposx = 1; p.oposy = 2; p.oposz = 3;
      p.cursectnum = 4; p.valid = true;
      const FreecamState s = freecam_enter(p, 300, 120, st);
      CHECK(s.px == s.x && s.px == 777);
      CHECK(s.py == s.y && s.py == -888);
      CHECK(s.pz == s.z && s.pz == 999);
      CHECK(s.pang == s.ang && s.pang == 300);
      // 120 is the player's engine-native horiz, converted through
      // freecam_horiz_to_pitch itself rather than a second independent guess.
      CHECK(s.ppitch == s.pitch && s.ppitch == freecam_horiz_to_pitch(120, st));
    }

    // Forward/back tilt with the camera's own pitch: strafe and the world-relative Space/C keys must not tilt; only
    // forward/back does, by how far off level the pitch is.
    {
      FreecamInput f = none; f.fwd = true;
      const FreecamState o = freecam_step(origin, f, 100, pct(100), st);
      CHECK(o.z == origin.z);
      CHECK(o.x - origin.x == FREECAM_BASE_SPEED);

      // Looking up and walking forward raises the camera (z falls, Build's z
      // grows downward). pitch > 0 is up, per FreecamState's own comment.
      FreecamState up = origin; up.pitch = 200;   // looking up
      const FreecamState ou = freecam_step(up, f, 100, pct(100), st);
      CHECK(ou.z < up.z);
      FreecamState down = origin; down.pitch = -200;   // looking down
      const FreecamState od = freecam_step(down, f, 100, pct(100), st);
      CHECK(od.z > down.z);

      FreecamInput b = none; b.back = true;
      const FreecamState ob = freecam_step(up, b, 100, pct(100), st);
      CHECK(ob.z > up.z);

      FreecamInput r = none; r.right = true;
      CHECK(freecam_step(up,   r, 100, pct(100), st).z == up.z);
      CHECK(freecam_step(down, r, 100, pct(100), st).z == down.z);

      FreecamInput u = none; u.up = true;
      CHECK(up.z   - freecam_step(up,   u, 100, pct(100), st).z
            == origin.z - freecam_step(origin, u, 100, pct(100), st).z);

      // pitch 256 => horiz 228 (+-1) and |dz|/16 == the xy step: at pitch
      // 256 sin==cos (11585), so fwdXY and fwdZ/16 reduce to the same ratio
      // by construction.
      FreecamState pitch45 = origin; pitch45.pitch = 256;
      const int h256 = freecam_pitch_to_horiz(256, st);
      CHECK(h256 >= 227 && h256 <= 229);         // pinned horiz
      const FreecamState op = freecam_step(pitch45, f, 100, pct(100), st);
      const int dz = op.z - pitch45.z;
      const int dx = op.x - pitch45.x;
      CHECK(dz != 0);
      CHECK(dx != 0);
      CHECK(dz < 0);              // looking up + forward: z falls
      CHECK((-dz) / 16 == dx);    // the pinned 45-degree identity
      CHECK(dx < FREECAM_BASE_SPEED);

      // The sign has to be right in both directions: a formula with dz's
      // sign flipped would still pass every `!= 0` check above but fail these.
      CHECK(freecam_step(down, f, 100, pct(100), st).z > down.z);   // down + forward: z rises
      CHECK(freecam_step(up,   b, 100, pct(100), st).z > up.z);     // up + back: z rises too
    }

    // The pitch term uses the post-step pitch: a mouse push that changes pitch this tic affects this tic's
    // climb/descent, like yaw's "travel along the new heading". Start level (a pre-step read would give zero tilt) with
    // dy pushing pitch off level: dz must be non-zero.
    {
      FreecamInput f = none; f.fwd = true; f.dy = -20;   // dy<0 looks UP (pitch rises)
      const FreecamState o = freecam_step(origin, f, 100, pct(100), st);
      CHECK(o.pitch == 20);           // the pitch really did move this tic
      CHECK(o.z != origin.z);         // ... and the SAME tic's forward step tilted with it
      CHECK(o.z < origin.z);          // looking up + forward: z falls (climbs)

      FreecamInput f2 = none; f2.fwd = true; f2.dy = 20;   // dy>0 looks DOWN
      const FreecamState o2 = freecam_step(origin, f2, 100, pct(100), st);
      CHECK(o2.pitch == -20);
      CHECK(o2.z != origin.z);
      CHECK(o2.z > origin.z);         // looking down + forward: z rises (descends)
    }

    // No internal z clamp: the camera has no ceiling/floor clamp (removed from camera_tic, along with the four
    // code-patched clamp sites on the render side). Pinned as a long vertical sweep with no plateau: z changes by
    // exactly FREECAM_BASE_SPEED * FREECAM_Z_PER_XY every tic.
    {
      FreecamState s = origin;
      FreecamInput u = none; u.up = true;
      const int PLAUSIBLE_ROOM_SPAN = 40960;   // five Build metres, a typical ceiling height
      int steps = 0;
      while (origin.z - s.z < PLAUSIBLE_ROOM_SPAN * 2 && steps < 10000) {
        const FreecamState next = freecam_step(s, u, 100, pct(100), st);
        CHECK(next.z == s.z - FREECAM_BASE_SPEED * FREECAM_Z_PER_XY);   // never held, ever
        s = next;
        steps++;
      }
      CHECK(steps > 0);
      CHECK(origin.z - s.z >= PLAUSIBLE_ROOM_SPAN * 2);
    }

    // freecam_enter_rule, every term flipped on its own. The permissive direction is the failure mode: a camera that
    // toggles under the panel, on a dead player or an unreadable player state substitutes a view nothing owns. The exit
    // rule and dead-man's switch have their own block below.
    CHECK( freecam_enter_rule(true,  true,  false, false, true));   // all clear
    CHECK(!freecam_enter_rule(false, true,  false, false, true));   // feature off
    CHECK(!freecam_enter_rule(true,  false, false, false, true));   // no key edge
    CHECK(!freecam_enter_rule(true,  true,  true,  false, true));   // panel open
    CHECK(!freecam_enter_rule(true,  true,  false, true,  true));   // player dead
    CHECK(!freecam_enter_rule(true,  true,  false, false, false));  // unreadable player
    CHECK(!freecam_enter_rule(false, false, false, false, false));
    // Swept: true for exactly the one combination above, so a dropped term
    // shows up as a second true rather than a silent widening.
    {
      int trueCount = 0;
      bool onlyTheRightOne = true;
      for (int on = 0; on < 2; on++)
      for (int ke = 0; ke < 2; ke++)
      for (int mo = 0; mo < 2; mo++)
      for (int de = 0; de < 2; de++)
      for (int va = 0; va < 2; va++) {
        const bool r = freecam_enter_rule(on != 0, ke != 0, mo != 0, de != 0, va != 0);
        if (r) {
          trueCount++;
          if (!(on && ke && !mo && !de && va)) onlyTheRightOne = false;
        }
      }
      CHECK(trueCount == 1);
      CHECK(onlyTheRightOne);
    }

    {
      PlayerPos p;
      p.posx = 12345; p.posy = -6789; p.posz = -40960;
      p.oposx = 1; p.oposy = 2; p.oposz = 3;   // deliberately different:
      p.cursectnum = 7;                        // the camera takes the current eye
      p.valid = true;
      const FreecamState s = freecam_enter(p, 700, 137, st);
      CHECK(s.x == 12345);
      CHECK(s.y == -6789);
      CHECK(s.z == -40960);       // posz IS the eye: no height offset added
      CHECK(s.ang == 700);
      // 137 is the player's engine-native horiz, converted through the same
      // freecam_horiz_to_pitch the entry itself calls.
      CHECK(s.pitch == freecam_horiz_to_pitch(137, st));
      CHECK(s.active);            // entering means active, or nothing arms
      // Takes the current position, not the previous tic's o-pair, which
      // would put the camera one tic behind the player.
      CHECK(s.x != p.oposx);
      CHECK(s.y != p.oposy);
      CHECK(s.z != p.oposz);
    }
    {
      // A view the engine could not have produced is held to the camera's own envelope (+-FREECAM_PITCH_MAX), not the
      // player's -99..299: 9999/-9999 saturate the search at the boundary and freecam_enter's clamp holds it there.
      PlayerPos p;
      p.posx = 0; p.posy = 0; p.posz = 0;
      p.oposx = 0; p.oposy = 0; p.oposz = 0;
      p.cursectnum = 0; p.valid = true;
      CHECK(freecam_enter(p, 0, 9999, st).pitch == FREECAM_PITCH_MAX);
      CHECK(freecam_enter(p, 0, -9999, st).pitch == -FREECAM_PITCH_MAX);
      CHECK(freecam_enter(p, 5000, 100, st).ang >= 0);
      CHECK(freecam_enter(p, 5000, 100, st).ang < FREECAM_ANG_UNITS);
      CHECK(freecam_enter(p, -5, 100, st).ang >= 0);
      CHECK(freecam_enter(p, -5, 100, st).ang < FREECAM_ANG_UNITS);
    }
    // Entering and then stepping with nothing pressed leaves the camera on the
    // player's eye, which is what makes switching on invisible.
    {
      PlayerPos p;
      p.posx = 500; p.posy = 600; p.posz = 700;
      p.oposx = 0; p.oposy = 0; p.oposz = 0;
      p.cursectnum = 3; p.valid = true;
      const FreecamState s = freecam_step(freecam_enter(p, 128, 100, st), none, 100, pct(100), st);
      CHECK(s.x == 500 && s.y == 600 && s.z == 700);
      CHECK(s.ang == 128 && s.pitch == 0);   // horiz 100 (level) converts to pitch 0
    }

    // The camera's two displayrooms preconditions, read from displayrooms' decompile: if either fails, the substituted
    // view would be computed then thrown away.
    CHECK( camera_preconditions_ok(-1, 0));   // normal play
    CHECK(!camera_preconditions_ok(0, 0));    // a viewscreen owns the view
    CHECK(!camera_preconditions_ok(37, 0));
    CHECK(!camera_preconditions_ok(-1, 1));   // third person owns the view
    CHECK(!camera_preconditions_ok(-1, -1));  // ... any nonzero, not just 1
    CHECK(!camera_preconditions_ok(5, 5));
    CHECK( camera_preconditions_ok(-32768, 0));   // sign-extended short: still < 0
    {
      int trueCount = 0;
      for (int n = -3; n <= 3; n++)
        for (int o = -3; o <= 3; o++)
          if (camera_preconditions_ok(n, o)) trueCount++;
      CHECK(trueCount == 3);   // newowner in {-3,-2,-1} with over_shoulder 0
    }

    // The camera is a third owner of the lockout the panel and settling window share: it must engage, never engage
    // while idle, and lose to the panel.
    CHECK(view_freeze_action(false, true,  0) == VFA_FREECAM);
    CHECK(view_freeze_action(false, true,  0) != VFA_NONE);
    CHECK(view_freeze_action(false, true,  5) == VFA_FREECAM);   // outranks settling
    CHECK(view_freeze_action(true,  true,  0) == VFA_FULL);      // panel outranks it
    CHECK(view_freeze_action(true,  true,  5) == VFA_FULL);
    CHECK(view_freeze_action(false, false, 0) == VFA_NONE);      // still idle when idle
    {
      // Swept: the freeze is active for exactly the states it should be, and
      // VFA_NONE happens only with nothing on and no window left.
      bool agree = true;
      for (int po = 0; po < 2; po++)
      for (int fc = 0; fc < 2; fc++)
      for (int sr = -1; sr <= 2; sr++) {
        const ViewFreezeAction a = view_freeze_action(po != 0, fc != 0, sr);
        const bool active = (a != VFA_NONE);
        if (active != (po != 0 || fc != 0 || sr > 0)) agree = false;
        if (po && a != VFA_FULL) agree = false;
        if (!po && fc && a != VFA_FREECAM) agree = false;
      }
      CHECK(agree);
    }

    CHECK(!freeze_wanted(false, false));
    CHECK( freeze_wanted(true,  false));
    CHECK( freeze_wanted(false, true));
    CHECK( freeze_wanted(true,  true));
    // Fires once per real change, so the panel's close cannot "restore"
    // gates the camera had zeroed.
    CHECK(freeze_transition(true,  false) == FT_ENGAGE);
    CHECK(freeze_transition(true,  true)  == FT_NONE);
    CHECK(freeze_transition(false, true)  == FT_RELEASE);
    CHECK(freeze_transition(false, false) == FT_NONE);
    {
      // Camera on, panel opened, panel closed, camera off: exactly one
      // engage and one release, lockout held throughout the middle.
      bool engaged = false;
      int engages = 0, releases = 0;
      bool heldThroughout = true;
      const bool panel[]   = { false, true,  false, false };
      const bool freecam[] = { true,  true,  true,  false };
      for (int i = 0; i < 4; i++) {
        const FreezeTransition t = freeze_transition(freeze_wanted(panel[i], freecam[i]), engaged);
        if (t == FT_ENGAGE) { engages++; engaged = true; }
        if (t == FT_RELEASE) { releases++; engaged = false; }
        if (i < 3 && !engaged) heldThroughout = false;
      }
      CHECK(engages == 1);
      CHECK(releases == 1);
      CHECK(heldThroughout);
      CHECK(!engaged);   // and it really does let go at the end
    }

    // The camera's key map (input.h): input.cpp cannot link into test_host, and a transposed pair (strafe left on the
    // forward key) is invisible to review.
    CHECK(freecam_key_slot(SDL_SCANCODE_W) == FKS_FWD);
    CHECK(freecam_key_slot(SDL_SCANCODE_S) == FKS_BACK);
    CHECK(freecam_key_slot(SDL_SCANCODE_A) == FKS_LEFT);
    CHECK(freecam_key_slot(SDL_SCANCODE_D) == FKS_RIGHT);
    CHECK(freecam_key_slot(SDL_SCANCODE_SPACE) == FKS_UP);
    CHECK(freecam_key_slot(SDL_SCANCODE_C) == FKS_DOWN);
    CHECK(freecam_key_slot(SDL_SCANCODE_LSHIFT) == FKS_FAST);
    CHECK(freecam_key_slot(SDL_SCANCODE_LCTRL) == FKS_SLOW);
    // The scancodes are SDL2's own, pinned to their literals so a renumbering
    // cannot ride in on the names.
    CHECK(SDL_SCANCODE_W == 26);
    CHECK(SDL_SCANCODE_A == 4);
    CHECK(SDL_SCANCODE_S == 22);
    CHECK(SDL_SCANCODE_D == 7);
    CHECK(SDL_SCANCODE_C == 6);
    CHECK(SDL_SCANCODE_SPACE == 44);
    CHECK(SDL_SCANCODE_LCTRL == 224);
    CHECK(SDL_SCANCODE_LSHIFT == 225);
    // A key the camera does not use is dropped, never folded onto slot 0:
    // the poll hook sees every key, including ones the game itself acts on.
    CHECK(freecam_key_slot(0) == -1);
    CHECK(freecam_key_slot(41) == -1);    // Escape
    CHECK(freecam_key_slot(73) == -1);    // Insert, the panel's own default
    CHECK(freecam_key_slot(63) == -1);    // F6, the camera's own TOGGLE key
    CHECK(freecam_key_slot(-5) == -1);
    CHECK(freecam_key_slot(99999) == -1);
    {
      int hits[FKS_COUNT];
      for (int i = 0; i < FKS_COUNT; i++) hits[i] = 0;
      bool inRange = true;
      for (int sc = -100; sc < 600; sc++) {
        const int slot = freecam_key_slot(sc);
        if (slot == -1) continue;
        if (slot < 0 || slot >= FKS_COUNT) { inRange = false; continue; }
        hits[slot]++;
      }
      CHECK(inRange);
      bool exactlyOnce = true;
      for (int i = 0; i < FKS_COUNT; i++) if (hits[i] != 1) exactlyOnce = false;
      CHECK(exactlyOnce);
      CHECK(FKS_COUNT == 8);
    }

    // The mouse accumulator's saturation guard (input.h) is not a sensitivity limit: domovethings gates processinput on
    // Duke's pause, so a game paused with the camera up accumulates every motion event and drains none; unbounded that
    // is signed overflow, bounded it is one snap on unpause.
    CHECK(FREECAM_MOTION_LIMIT == 100000);
    CHECK(freecam_accum(0, 0) == 0);
    CHECK(freecam_accum(0, 7) == 7);
    CHECK(freecam_accum(7, -9) == -2);
    CHECK(freecam_accum(100, -100) == 0);
    CHECK(freecam_accum(FREECAM_MOTION_LIMIT, 1) == FREECAM_MOTION_LIMIT);
    CHECK(freecam_accum(FREECAM_MOTION_LIMIT, 999999) == FREECAM_MOTION_LIMIT);
    CHECK(freecam_accum(-FREECAM_MOTION_LIMIT, -1) == -FREECAM_MOTION_LIMIT);
    CHECK(freecam_accum(-FREECAM_MOTION_LIMIT, -999999) == -FREECAM_MOTION_LIMIT);
    CHECK(freecam_accum(FREECAM_MOTION_LIMIT, -1) == FREECAM_MOTION_LIMIT - 1);
    {
      const int big = 2147483647;
      bool inRange = true;
      const int accs[] = { 0, big, -big - 1, FREECAM_MOTION_LIMIT, -FREECAM_MOTION_LIMIT };
      const int dels[] = { 0, big, -big - 1, 1, -1, 12345 };
      for (int a = 0; a < 5; a++)
        for (int d = 0; d < 6; d++) {
          const int v = freecam_accum(accs[a], dels[d]);
          if (v > FREECAM_MOTION_LIMIT || v < -FREECAM_MOTION_LIMIT) inRange = false;
        }
      CHECK(inRange);
    }

    // The player marker is gated on the live camera and the player's switch, never the config flag alone, or it would
    // sit on the player's body while they still look out of it.
    CHECK(!freecam_marker_on(false, 0));
    CHECK(!freecam_marker_on(false, 1));
    CHECK(!freecam_marker_on(true,  0));
    CHECK( freecam_marker_on(true,  1));
    CHECK( freecam_marker_on(true,  7));   // a hand-edited ini still counts
    // Must open the GL bracket on its own, or it never reaches the screen
    // with the panel shut -- every moment the camera is flying.
    CHECK(!esp_any_category_on(0, 0, 0, 0, 0, 0, 0, 0));
    CHECK( esp_any_category_on(0, 0, 0, 0, 1, 0, 0, 0));
    CHECK( esp_any_category_on(0, 0, 0, 0, freecam_marker_on(true, 1) ? 1 : 0, 0, 0, 0));
    CHECK(!esp_any_category_on(0, 0, 0, 0, freecam_marker_on(false, 1) ? 1 : 0, 0, 0, 0));
    CHECK( esp_any_category_on(1, 0, 0, 0, 1, 0, 0, 0));
    {
      bool agree = true;
      for (int a = 0; a < 2; a++)
      for (int b = 0; b < 2; b++)
      for (int c = 0; c < 2; c++)
      for (int d = 0; d < 2; d++)
      for (int e = 0; e < 2; e++)
        if (esp_any_category_on(a, b, c, d, e, 0, 0, 0) != (a || b || c || d || e)) agree = false;
      CHECK(agree);
    }
    // The diamond's geometry: an odd number of rows so the projected pixel is
    // a real centre row, widest in the middle, a single pixel at both points,
    // and symmetric about that centre.
    CHECK(PLAYER_MARKER_HALF == 6);
    CHECK(player_marker_rows() == 13);
    CHECK(player_marker_rows() % 2 == 1);
    CHECK(player_marker_row_halfwidth(0) == 0);
    CHECK(player_marker_row_halfwidth(PLAYER_MARKER_HALF) == PLAYER_MARKER_HALF);
    CHECK(player_marker_row_halfwidth(player_marker_rows() - 1) == 0);
    CHECK(player_marker_row_w(0) == 1);
    CHECK(player_marker_row_w(PLAYER_MARKER_HALF) == 2 * PLAYER_MARKER_HALF + 1);
    {
      bool symmetric = true, widestInMiddle = true, positive = true;
      for (int r = 0; r < player_marker_rows(); r++) {
        const int w = player_marker_row_halfwidth(r);
        if (w != player_marker_row_halfwidth(player_marker_rows() - 1 - r)) symmetric = false;
        if (w > PLAYER_MARKER_HALF) widestInMiddle = false;
        if (player_marker_row_w(r) <= 0) positive = false;
      }
      CHECK(symmetric);
      CHECK(widestInMiddle);
      CHECK(positive);   // canvas_rect must never be handed a negative width
      CHECK(player_marker_row_halfwidth(-5) == 0);
      CHECK(player_marker_row_halfwidth(999) == 0);
      CHECK(player_marker_row_w(999) == 1);
    }
    CHECK(player_marker_row_x(100, PLAYER_MARKER_HALF) == 100 - PLAYER_MARKER_HALF);
    CHECK(player_marker_row_y(100, PLAYER_MARKER_HALF) == 100);
    CHECK(player_marker_row_y(100, 0) == 100 - PLAYER_MARKER_HALF);
    CHECK(player_marker_row_y(100, player_marker_rows() - 1) == 100 + PLAYER_MARKER_HALF);
    CHECK(player_marker_row_x(100, 0) == 100);
    CHECK(player_marker_label_y(100) == 100 - PLAYER_MARKER_HALF - ESP_LABEL_GAP);
    CHECK(player_marker_label_y(100) < player_marker_row_y(100, 0));
    {
      bool sane = true;
      const int big = 2147483647;
      for (int r = 0; r < player_marker_rows(); r++) {
        if (player_marker_row_x(big, r) > big) sane = false;
        if (player_marker_row_y(-big - 1, r) < -big - 1) sane = false;
      }
      CHECK(sane);
    }
    CHECK(strcmp(PLAYER_MARKER_LABEL, "You") == 0);
    CHECK(strcmp(PLAYER_MARKER_LABEL, AIM_MARKER_LABEL) != 0);
    // The colour differs from all four of the others, because on this overlay
    // colour is what tells markers apart.
    CHECK(canvas_player_colour() == C_PLAYER);
    CHECK(canvas_player_colour() != canvas_esp_colour(true));
    CHECK(canvas_player_colour() != canvas_esp_colour(false));
    CHECK(canvas_player_colour() != canvas_esp_secret_colour());
    CHECK(canvas_player_colour() != canvas_aim_colour());
    CHECK((canvas_player_colour() & 0xFF000000u) == 0xFF000000u);   // fully opaque

    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0) == 1);   // the camera alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0) == 1);   // any nonzero counts once
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0) == 7);
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0) == 6);   // and it really is the seventh
    {
      bool agree = true, sawSeven = false;
      for (int fv = 0; fv < 2; fv++)
      for (int en = 0; en < 2; en++)
      for (int it = 0; it < 2; it++)
      for (int se = 0; se < 2; se++)
      for (int ao = 0; ao < 2; ao++)
      for (int so = 0; so < 2; so++)
      for (int fc = 0; fc < 2; fc++) {
        const int n = menu_live_count(fv, en, it, se, 0, 0, ao, so, fc, 0, 0, 0, 0, 0, 0, 0);
        if (n != fv + en + it + se + ao + so + fc) agree = false;
        if (n == 7) sawSeven = true;
      }
      CHECK(agree);
      CHECK(sawSeven);
    }

    // The toggle key's readout (menu.h): F6 is still a legal hand-edited binding; the default moved to V.
    CHECK(strcmp(freecam_key_label(0x75), "F6") == 0);
    CHECK(strcmp(freecam_key_label(0x56), "V") == 0);       // the new default
    CHECK(strcmp(freecam_key_label(0x2D), "Insert") == 0);
    CHECK(strcmp(freecam_key_label(0x7B), "F12") == 0);
    CHECK(strcmp(freecam_key_label(0x41), "A") == 0);       // 'A' is bindable
    CHECK(strcmp(freecam_key_label(0x10), "Unbound") == 0);  // VK_SHIFT: no SDL mapping
    // THE TWO HALVES PINNED AGAINST EACH OTHER: "Unbound" for exactly the codes vk_to_sdl_scancode rejects, swept over
    // every byte, so a key added to one and forgotten in the other fails.
    {
      bool agree = true, sawNamed = false, sawUnbound = false;
      for (int vk = 0; vk <= 255; vk++) {
        int sc = -1;
        const bool mapped = vk_to_sdl_scancode(vk, &sc);
        const bool named = strcmp(freecam_key_label(vk), "Unbound") != 0;
        if (mapped != named) agree = false;
        if (named) sawNamed = true; else sawUnbound = true;
      }
      CHECK(agree);
      CHECK(sawNamed);
      CHECK(sawUnbound);
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      // EVERY FEATURE DEFAULTS OFF, both camera switches included; the key and speed are settings, so they carry values
      // that make the feature useful once switched on.
      CHECK(g_cfg.freecamon == 0);
      CHECK(g_cfg.freecamshowplayer == 0);
      // Default moved from F6 (0x75) to V (0x56): stock Megaton binds F6 to
      // Quick_Save, so a fresh install's F6 quietly saved on every toggle.
      CHECK(g_cfg.freecamkey == 0x56);                       // 'V'
      CHECK(g_cfg.freecamkey != 0x75);                        // not the old F6 default
      CHECK(g_cfg.freecamspeed == FREECAM_SPEED_DEFAULT);
      CHECK(g_cfg.freecamspeed == 100);
      {
        int sc = -1;
        CHECK(vk_to_sdl_scancode(g_cfg.freecamkey, &sc));
        CHECK(sc == 25);   // SDL_SCANCODE_V
      }
    }
    {
      const char* t = "test_freecam_task3.ini";
      config_load(tb_path("Z:|does|not|exist.ini"));
      g_cfg.freecamon = 1;
      g_cfg.freecamkey = 0x74;          // F5
      g_cfg.freecamspeed = 37;
      g_cfg.freecamshowplayer = 1;
      CHECK(config_save(t));
      // Poisoned, so a no-op reload fails instead of passing by leaving
      // values where they already were.
      g_cfg.freecamon = 0;
      g_cfg.freecamkey = 0x2D;
      g_cfg.freecamspeed = 99;
      g_cfg.freecamshowplayer = 0;
      config_load(t);
      CHECK(g_cfg.freecamon == 1);
      CHECK(g_cfg.freecamkey == 0x74);
      CHECK(g_cfg.freecamspeed == 37);
      CHECK(g_cfg.freecamshowplayer == 1);
      remove(t);
    }
    {
      // The clamps, from a hand-edited ini: speed goes through the same
      // function the integration reads it back through.
      const char* t = "test_freecam_clamp.ini";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\n", f);
        fputs("freecamon=7\n", f);
        fputs("freecamkey=99999\n", f);
        fputs("freecamspeed=100000\n", f);
        fputs("freecamshowplayer=-3\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.freecamon == 1);            // any truthy value reads as 1
        CHECK(g_cfg.freecamshowplayer == 1);
        CHECK(g_cfg.freecamkey == 254);         // the same 1..254 menukey uses
        CHECK(g_cfg.freecamspeed == FREECAM_SPEED_MAX);
        remove(t);
      }
      f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\n", f);
        fputs("freecamkey=-5\n", f);
        fputs("freecamspeed=-5\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.freecamkey == 1);
        CHECK(g_cfg.freecamspeed == FREECAM_SPEED_MIN);
        remove(t);
      }
      config_load(tb_path("Z:|does|not|exist.ini"));
    }

    {
      const int savedTab = menu_active_tab();
      const int savedOn = g_cfg.freecamon, savedShow = g_cfg.freecamshowplayer;
      const int savedSpeed = g_cfg.freecamspeed, savedKey = g_cfg.freecamkey;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

      g_cfg.freecamon = 0;
      g_cfg.freecamshowplayer = 0;
      g_cfg.freecamspeed = 100;
      g_cfg.freecamkey = 0x75;
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_count() > 0);

      CHECK(canvas_record_find_text("Freecam arrives in phase 4.") == nullptr);
      CHECK(canvas_record_find_text("Custom field of view") != nullptr);
      const CanvasOp* rCam   = canvas_record_find_text("Free camera");
      const CanvasOp* rSpeed = canvas_record_find_text("Speed");
      const CanvasOp* rShow  = canvas_record_find_text("Show player");
      const CanvasOp* gCam   = canvas_record_find_text("FREE CAMERA");
      CHECK(rCam && rSpeed && rShow && gCam);
      CHECK(canvas_record_find_text("Toggle key") == nullptr);
      if (rCam && rSpeed && rShow && gCam) {
        CHECK(gCam->colour == C_BONEFNT);
        CHECK(gCam->font == FONT_SMALL);
        CHECK(gCam->y < rCam->y);
        CHECK(rCam->y < rSpeed->y);
        CHECK(rSpeed->y < rShow->y);
        const CanvasOp* rFov = canvas_record_find_text("Custom field of view");
        CHECK(rFov && rFov->y < gCam->y);
      }
      // The key readout moves with the setting, read off the Settings tab
      // where the camera's key row lives. The other hotkey rows are pinned
      // to their own names first, so a stray F2/F6 there cannot answer for this field.
      const int savedMenuK = g_cfg.menukey, savedTpK = g_cfg.freecamteleportkey;
      const int savedAimK = g_cfg.aimbind;
      g_cfg.menukey = 0x2D;                    // Insert
      g_cfg.freecamteleportkey = 0x54;         // T
      g_cfg.aimbind = VK_BIND_XBUTTON2;        // Mouse 5
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("F6") != nullptr);
      g_cfg.freecamkey = 0x71;   // F2
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("F2") != nullptr);
      CHECK(canvas_record_find_text("F6") == nullptr);
      g_cfg.freecamkey = 0x10;   // VK_SHIFT: no SDL mapping at all (0x41 is 'A' now)
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Unbound") != nullptr);
      g_cfg.freecamkey = 0x75;
      g_cfg.menukey = savedMenuK; g_cfg.freecamteleportkey = savedTpK;
      g_cfg.aimbind = savedAimK;
      menu_set_tab(TAB_VIEW);

      // Nothing runs off the panel's right edge at either screen size, with
      // every switch on.
      g_cfg.freecamon = 1;
      g_cfg.freecamshowplayer = 1;
      g_cfg.freecamspeed = 100;
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      }
      g_cfg.freecamon = 1;
      CHECK(slider_row_reaches_ends(TAB_VIEW, "Speed", 3440, 1440,
                                    FREECAM_SPEED_MIN, FREECAM_SPEED_MAX,
                                    &g_cfg.freecamspeed, 50));

      g_cfg.freecamon = savedOn;
      g_cfg.freecamshowplayer = savedShow;
      g_cfg.freecamspeed = savedSpeed;
      g_cfg.freecamkey = savedKey;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // The View tab's "Mouse sensitivity" row is gone: the camera's turn rate derives from modern mouse aim, so the row
    // and its config field are deleted. Pinned as an absence, with "Speed" checked beside it so an empty draw cannot
    // pass.
    {
      const int savedTab = menu_active_tab();
      const int savedOn = g_cfg.freecamon;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      g_cfg.freecamon = 1;
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_find_text("Speed") != 0);
      CHECK(canvas_record_find_text("Mouse sensitivity") == 0);
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      }
      g_cfg.freecamon = savedOn;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // Disable culling. nocull.cpp cannot link into test_host (it redirects a live call site and reads gotsector from
    // engine memory), so every decision is pure and lives in nocull.h.
    {
      // The call-site fingerprint, pinned like SHOOT_BODY_RVA and DISPLAYROOMS_BODY_RVA: decode the redirect's rel32
      // and require it to land on the named body constant.
      CHECK(rva::NOCULL_CALL_SITE_RVA == 0x0007AEE9u);
      CHECK(rva::POLYMOST_SCANSECTOR_BODY_RVA == 0x00081390u);
      CHECK(rva::NOCULL_CALL_SITE_BYTES[0] == 0xE8);   // CALL rel32, not a JMP thunk
      {
        unsigned rel32;
        memcpy(&rel32, rva::NOCULL_CALL_SITE_BYTES + 1, 4);
        CHECK(thunk_decode_target(rva::NOCULL_CALL_SITE_RVA, rel32) == rva::POLYMOST_SCANSECTOR_BODY_RVA);
      }
      CHECK(rva::GOTSECTOR == 0x01890FE0u);

      CHECK(rva::NUMSCANS == 0x01AD8980u);
      CHECK(rva::NUMBUNCHES == 0x019D2620u);
      CHECK(rva::MAXWALLSB == 6144);
      CHECK(rva::INPREPAREMIRROR == 0x0032B4A6u);

      // --- nocull_should_scan: four terms (cfgOn, cameraActive, bracketArmed,
      // inMirror), exhaustive so a dropped term widens the truth set visibly.
      {
        bool agree = true;
        for (int co = 0; co < 2; co++)
        for (int ca = 0; ca < 2; ca++)
        for (int ba = 0; ba < 2; ba++)
        for (int im = 0; im < 2; im++) {
          const bool want = (co != 0) && (ca != 0) && (ba != 0) && (im == 0);
          if (nocull_should_scan(co != 0, ca != 0, ba != 0, im != 0) != want) agree = false;
        }
        CHECK(agree);
      }
      CHECK( nocull_should_scan(true, true, true, false));    // all clear: sweep runs
      CHECK(!nocull_should_scan(false, true, true, false));   // option off
      CHECK(!nocull_should_scan(true, false, true, false));   // camera not active (render-clock)
      // "active" alone is not enough: the render bracket must have armed this frame (an unverified engine, a bad index
      // or a displayrooms precondition can leave cam.active true while hooked_displayrooms refused to arm).
      CHECK(!nocull_should_scan(true, true, false, false));   // active but refused: no sweep
      CHECK( nocull_should_scan(true, true, true,  false));   // armed: sweep
      // The visible-mirror pass runs polymost_drawrooms (and this hook) a
      // second time on the same frame; the sweep must not also double.
      CHECK(!nocull_should_scan(true, true, true, true));     // mirror pass: no sweep

      // nocull_budget_allows reserves room for the engine's own post-hook scanning: free = MAXWALLSB - max(entry);
      // budget = free / 2; usedSinceEntry = max(now) - max(entry); refuse once usedSinceEntry + wallnum +
      // NOCULL_CALL_ALLOWANCE > budget. The absolute floor (MAXWALLSB - NOCULL_FLOOR_MARGIN) is checked first.
      {
        const int M = rva::MAXWALLSB;   // 6144
        const int A = NOCULL_CALL_ALLOWANCE;   // 256

        // entry counters 0 -> free 6144, budget (half) 3072. At the first
        // iteration (now == entry, usedSinceEntry == 0) the line is exactly
        // `budget - allowance` for wallnum.
        CHECK( nocull_budget_allows(0, 0, 0, 0, 3072 - A));       // exactly at the line
        CHECK(!nocull_budget_allows(0, 0, 0, 0, 3072 - A + 1));   // one over: refused

        CHECK( nocull_budget_allows(5000, 5000, 5000, 5000, 572 - A));
        CHECK(!nocull_budget_allows(5000, 5000, 5000, 5000, 572 - A + 1));

        CHECK( nocull_budget_allows(0, 0, 1000, 1000, 3072 - A - 1000));
        CHECK(!nocull_budget_allows(0, 0, 1000, 1000, 3072 - A - 1000 + 1));

        CHECK( nocull_budget_allows(0, 0, 0, 2000, 3072 - A - 2000));
        CHECK(!nocull_budget_allows(0, 0, 0, 2000, 3072 - A - 2000 + 1));

        // The absolute floor triggers regardless of the half rule: entryMax 5200 leaves budget 472, but the live
        // counter is past MAXWALLSB - 1024 = 5120.
        CHECK(!nocull_budget_allows(5200, 5200, 5200, 5200, 0));
        CHECK( nocull_budget_allows(5200, 5200, 5120, 5120, 0));

        CHECK(nocull_budget_allows(100, 100, 150, 150, 50));

        // Fails closed: any negative input (a wrapped counter, or a wallnum
        // this DLL could not read) refuses outright.
        CHECK(!nocull_budget_allows(-1, 0, 0, 0, 0));
        CHECK(!nocull_budget_allows(0, -1, 0, 0, 0));
        CHECK(!nocull_budget_allows(0, 0, -1, 0, 0));
        CHECK(!nocull_budget_allows(0, 0, 0, -1, 0));
        CHECK(!nocull_budget_allows(0, 0, 0, 0, -1));
      }

      unsigned char got[512];
      memset(got, 0, sizeof(got));
      CHECK(nocull_next_unscanned(got, 10, -1) == 0);    // the sweep starts at 0
      CHECK(nocull_next_unscanned(got, 10, 0) == 1);
      CHECK(nocull_next_unscanned(got, 10, 8) == 9);
      CHECK(nocull_next_unscanned(got, 10, 9) == -1);    // nothing past numsectors
      got[0] |= (1 << 3);                                 // sector 3 marked
      CHECK(nocull_next_unscanned(got, 10, 2) == 4);      // 3 is skipped: already marked
      got[0] = 0;
      CHECK(nocull_next_unscanned(0, 10, -1) == -1);      // null bitset
      CHECK(nocull_next_unscanned(got, 0, -1) == -1);     // numsectors 0
      CHECK(nocull_next_unscanned(got, -1, -1) == -1);    // numsectors negative
      CHECK(nocull_next_unscanned(got, 10, -99) == 0);    // any negative `from` starts at 0
      {
        int from = -1, count = 0, last = -1;
        bool ascending = true;
        for (;;) {
          const int s = nocull_next_unscanned(got, 20, from);
          if (s < 0) break;
          if (s <= last) ascending = false;
          last = s;
          count++;
          from = s;
          CHECK(count < 100);   // guards this loop itself against a broken fixture
        }
        CHECK(count == 20);
        CHECK(ascending);
      }

      CHECK(nocull_available());              // starts available
      CHECK(!nocull_row_unavailable());
      nocull_mark_unavailable();
      CHECK(!nocull_available());
      CHECK(nocull_row_unavailable());
      nocull_reset_available();               // the suite's own way back
      CHECK(nocull_available());

      // End to end, the loop nocull.cpp's hook is written over: the seed is always scanned (the real hook's original
      // call is unconditional, before nocull_should_scan), modelled by calling the stand-in once before either gate is
      // tested.
      {
        nocull_test_reset();
        const int seed = 5;
        nocull_test_original(seed);
        if (nocull_should_scan(false, true, true, false)) nocull_test_original(999);
        CHECK(nocull_test_call_count() == 1);
        CHECK(nocull_test_call_at(0) == seed);
      }
      {
        nocull_test_reset();
        const int seed = 5;
        nocull_test_original(seed);
        if (nocull_should_scan(true, false, true, false)) nocull_test_original(999);
        CHECK(nocull_test_call_count() == 1);
      }
      {
        nocull_test_reset();
        const int seed = 5;
        nocull_test_original(seed);
        if (nocull_should_scan(true, true, false, false)) nocull_test_original(999);
        CHECK(nocull_test_call_count() == 1);
      }
      {
        nocull_test_reset();
        const int seed = 5;
        nocull_test_original(seed);
        if (nocull_should_scan(true, true, true, true)) nocull_test_original(999);
        CHECK(nocull_test_call_count() == 1);
      }
      {
        nocull_test_reset();
        const int seed = 5, numsectors = 10;
        nocull_test_original(seed);
        if (nocull_should_scan(true, true, true, false)) {
          int from = -1;
          for (;;) {
            const int s = nocull_next_unscanned(nocull_test_got(), numsectors, from);
            if (s < 0) break;
            if (!nocull_budget_allows(0, 0, 0, 0, 0)) break;
            nocull_test_original(s);
            from = s;
          }
        }
        CHECK(nocull_test_call_count() == numsectors);
        int seedCount = 0;
        for (int i = 0; i < nocull_test_call_count(); i++)
          if (nocull_test_call_at(i) == seed) seedCount++;
        CHECK(seedCount == 1);
        bool ascending = true;
        int last = -1;
        for (int i = 1; i < nocull_test_call_count(); i++) {
          const int v = nocull_test_call_at(i);
          if (v <= last) ascending = false;
          last = v;
        }
        CHECK(ascending);
      }
      {
        nocull_test_reset();
        nocull_test_original(0);
        if (nocull_should_scan(true, true, true, false)) {
          CHECK(nocull_next_unscanned(nocull_test_got(), 0, -1) == -1);
          CHECK(nocull_next_unscanned(nocull_test_got(), -5, -1) == -1);
        }
        CHECK(nocull_test_call_count() == 1);
      }

      // The reserve stops the sweep in place, on a synthetic engine: the entry snapshot is taken once (both zero) and
      // numscans/numbunches grow by each visited sector's wallnum, re-read live like the real loop. Budget at entry 0
      // is 3072; sector 4 is oversized to cross it, and the sweep stops there rather than skipping past.
      {
        nocull_test_reset();
        const int seed = 0, numsectors = 8;
        const int wallnumOf[8] = { 0, 10, 10, 10, 2800, 10, 10, 10 };
        nocull_test_original(seed);   // the unconditional seed call
        const int entryScans = 0, entryBunches = 0;
        int numscans = 0, numbunches = 0;
        if (nocull_should_scan(true, true, true, false)) {
          int from = -1;
          for (;;) {
            const int s = nocull_next_unscanned(nocull_test_got(), numsectors, from);
            if (s < 0) break;
            if (!nocull_budget_allows(entryScans, entryBunches, numscans, numbunches, wallnumOf[s])) break;
            nocull_test_original(s);
            numscans += wallnumOf[s];
            numbunches += wallnumOf[s];
            from = s;
          }
        }
        // Seed(0) + sectors 1,2,3 (30 spent); sector 4's wallnum (2800)
        // crosses the entry-0 budget (30+2800+256 = 3086 > 3072), so the
        // sweep stops there. Sectors 5,6,7 are never reached.
        CHECK(nocull_test_call_count() == 4);
        CHECK(nocull_test_call_at(0) == 0);
        CHECK(nocull_test_call_at(1) == 1);
        CHECK(nocull_test_call_at(2) == 2);
        CHECK(nocull_test_call_at(3) == 3);
        for (int i = 0; i < nocull_test_call_count(); i++) CHECK(nocull_test_call_at(i) != 4);
        for (int i = 0; i < nocull_test_call_count(); i++) CHECK(nocull_test_call_at(i) != 5);
      }
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.freecamnocull == 0);        // every feature defaults off
    }
    {
      const char* t = "test_freecam_nocull.ini";
      config_load(tb_path("Z:|does|not|exist.ini"));
      g_cfg.freecamnocull = 1;
      CHECK(config_save(t));
      g_cfg.freecamnocull = 0;                // poisoned before the reload
      config_load(t);
      CHECK(g_cfg.freecamnocull == 1);
      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));
    }
    {
      const char* t = "test_freecam_nocull_clamp.ini";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\nfreecamnocull=7\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.freecamnocull == 1);      // any truthy value reads as exactly 1
        remove(t);
      }
      config_load(tb_path("Z:|does|not|exist.ini"));
    }

    {
      const int savedTab = menu_active_tab();
      const int savedOn = g_cfg.freecamon, savedNocull = g_cfg.freecamnocull;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      nocull_reset_available();

      g_cfg.freecamon = 1;
      g_cfg.freecamnocull = 0;
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Disable culling") != nullptr);
      CHECK(canvas_record_find_text("Disable culling (Unavailable)") == nullptr);
      // The row's hint is a tooltip, read off the row that owns it rather
      // than a plain recording, before the latch below renames the label.
      CHECK(streq(tooltip_for_row("Disable culling", 3440, 1440),
                  "Draws every sector in view while flying. Slow on large maps."));

      nocull_mark_unavailable();
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Disable culling (Unavailable)") != nullptr);
      // The switch stays live even while unavailable: a click still writes g_cfg (the latch is session-only and must
      // not overwrite a saved setting). row_switch_at's clickable region is menu.cpp-private, so sweep x across the
      // panel at the row's y until the click lands.
      {
        const CanvasOp* row = canvas_record_find_text("Disable culling (Unavailable)");
        CHECK(row != nullptr);
        if (row) {
          const UiRect panel = menu_panel_rect(3440, 1440);
          g_cfg.freecamnocull = 0;
          bool flipped = false;
          // The tab is re-set before every click: this row sits inside the
          // tab rail's vertical span, so the first few x of a full-width
          // sweep would otherwise land on the rail and switch tabs away.
          for (int x = panel.x; x < panel.x + panel.w && !flipped; x++) {
            menu_set_tab(TAB_VIEW);
            g_ui.mx = x; g_ui.my = row->y; g_ui.down = false; g_ui.pressed = true;
            menu_draw(3440, 1440);
            if (g_cfg.freecamnocull != 0) flipped = true;
          }
          CHECK(flipped);
          g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
        }
      }
      nocull_reset_available();

      // Nothing runs off the panel's right edge with the row's longest
      // label showing, at either screen size.
      nocull_mark_unavailable();
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      }
      nocull_reset_available();

      g_cfg.freecamon = savedOn;
      g_cfg.freecamnocull = savedNocull;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // The snapshot seqlock the camera publishes through: the same helpers aim.cpp uses (target.h), a tic-clock write of
    // a FreecamState and a render-clock read that must refuse a copy taken across one.
    {
      unsigned seq = 0;
      FreecamState pub = { 0, 0, 0, 0, 100, false };
      const FreecamState want = { 111, 222, 333, 444, 55, true };

      const unsigned before = seq;
      snap_begin(&seq);
      CHECK(seq % 2 == 1);                 // odd: a reader starting now refuses
      CHECK(!snap_read_ok(before, seq));
      pub = want;
      snap_end(&seq);
      CHECK(seq % 2 == 0);                 // even: stable again
      CHECK(seq != before);                // ... and one full step higher
      CHECK(!snap_read_ok(before, seq));   // a copy spanning the write is refused

      const unsigned after = seq;
      const FreecamState got = pub;
      CHECK(snap_read_ok(after, seq));     // a clean copy is accepted
      CHECK(got.x == want.x && got.y == want.y && got.z == want.z);
      CHECK(got.ang == want.ang && got.pitch == want.pitch);
      CHECK(got.active == want.active);
    }
  }




// Getting out of the camera, and into it without one tic of the player's own input leaking through. camera.cpp and
// input.cpp cannot link into test_host, so what is pinned is the decision each makes.
void tb_0061_phase_4_task_3_fix() {
    // Exit does not share entry's preconditions. One symmetric rule once served both directions: every term was a
    // reason not to start the camera and none a reason to stop it, so some sequences left it on with the input lockout
    // held until restart.
    CHECK( freecam_exit_rule(true,  true,  false));   // flying, key pressed, no panel
    CHECK(!freecam_exit_rule(false, true,  false));   // nothing to stop
    CHECK(!freecam_exit_rule(true,  false, false));   // no press edge
    CHECK(!freecam_exit_rule(true,  true,  true));    // the panel owns input
    {
      int trueCount = 0;
      bool onlyTheRightOne = true;
      for (int ac = 0; ac < 2; ac++)
      for (int ke = 0; ke < 2; ke++)
      for (int mo = 0; mo < 2; mo++) {
        if (freecam_exit_rule(ac != 0, ke != 0, mo != 0)) {
          trueCount++;
          if (!(ac && ke && !mo)) onlyTheRightOne = false;
        }
      }
      CHECK(trueCount == 1);
      CHECK(onlyTheRightOne);
    }
    // The two rules are not the same function: the state that must let you out is exactly the one the old shared rule
    // refused. The panel's switch: fly, open the panel, click "Free camera" off, close, press the key: `on` is 0, so
    // entry refuses; exit must not.
    CHECK(!freecam_enter_rule(false, true, false, false, true));
    CHECK( freecam_exit_rule(true, true, false));
    // Dying while flying. Entry refuses a dead player; exit must not care,
    // since a dead player in a stuck camera cannot respawn either.
    CHECK(!freecam_enter_rule(true, true, false, true, true));
    CHECK( freecam_exit_rule(true, true, false));
    CHECK(!freecam_enter_rule(true, true, false, false, false));
    CHECK( freecam_exit_rule(true, true, false));

    // The dead-man's switch needs no key: the key-driven exit closes only if the player still has a key that reaches
    // the camera, while this half closes the death/unreadable cases where the drain path stalls.
    CHECK(!freecam_force_off_rule(false, true,  false, true));   // nothing flying
    CHECK(!freecam_force_off_rule(true,  true,  false, true));   // flying, all well
    CHECK( freecam_force_off_rule(true,  false, false, true));   // feature switched off
    CHECK( freecam_force_off_rule(true,  true,  true,  true));   // player died
    CHECK( freecam_force_off_rule(true,  true,  false, false));  // player unreadable
    // Never while the camera is not up: a switch firing on an inactive
    // camera would release a lockout the panel might hold.
    {
      bool everFiredIdle = false;
      for (int on = 0; on < 2; on++)
      for (int de = 0; de < 2; de++)
      for (int va = 0; va < 2; va++)
        if (freecam_force_off_rule(false, on != 0, de != 0, va != 0)) everFiredIdle = true;
      CHECK(!everFiredIdle);
    }
    {
      bool agree = true;
      for (int ac = 0; ac < 2; ac++)
      for (int on = 0; on < 2; on++)
      for (int de = 0; de < 2; de++)
      for (int va = 0; va < 2; va++) {
        const bool want = (ac != 0) && (on == 0 || de != 0 || va == 0);
        if (freecam_force_off_rule(ac != 0, on != 0, de != 0, va != 0) != want) agree = false;
      }
      CHECK(agree);
    }
    CHECK(!freecam_force_off_rule(true, true, false, true));
    CHECK(!freecam_exit_rule(true, false, false));

    // The whole stranding sequence walked as a state machine through all three rules plus the lockout transition, so
    // "unplayable until restart" is a regression test. Each step is (freecamon, keyEdge, menuOpen, dead, valid).
    {
      struct Step { bool on, key, panel, dead, valid; };
      const Step seq[] = {
        { true,  true,  false, false, true },   // 0: press the key -> flying
        { true,  false, true,  false, true },   // 1: panel opened
        { false, false, true,  false, true },   // 2: "Free camera" switched off
        { false, false, false, false, true },   // 3: panel closed
        { false, true,  false, false, true },   // 4: press the key again
      };
      bool active = false;
      bool engaged = false;
      int ticsFlying = 0;
      for (int i = 0; i < 5; i++) {
        const Step& s2 = seq[i];
        if (freecam_force_off_rule(active, s2.on, s2.dead, s2.valid)) {
          active = false;
        } else if (active) {
          if (freecam_exit_rule(active, s2.key, s2.panel)) active = false;
        } else {
          if (freecam_enter_rule(s2.on, s2.key, s2.panel, s2.dead, s2.valid)) active = true;
        }
        const FreezeTransition t = freeze_transition(freeze_wanted(s2.panel, active), engaged);
        if (t == FT_ENGAGE) engaged = true;
        if (t == FT_RELEASE) engaged = false;
        if (active) ticsFlying++;
      }
      CHECK(ticsFlying > 0);   // it really did fly
      CHECK(!active);          // ... and it came down
      CHECK(!engaged);         // ... and the input lockout was handed back
    }
    {
      // Dying while flying, with nobody pressing anything at all: the camera
      // must come down and the lockout must release on the death tic itself.
      bool active = true, engaged = true;
      const FreezeTransition t0 = freeze_transition(freeze_wanted(false, active), engaged);
      CHECK(t0 == FT_NONE);    // still held while alive
      CHECK(freecam_force_off_rule(active, /*on*/true, /*dead*/true, /*valid*/true));
      active = false;
      const FreezeTransition t1 = freeze_transition(freeze_wanted(false, active), engaged);
      CHECK(t1 == FT_RELEASE);
      engaged = false;
      CHECK(!engaged);
    }
    {
      // The local player becoming unreadable mid-flight: same outcome, and
      // it needs neither a key nor camera_tic to run.
      bool active = true, engaged = true;
      CHECK(freecam_force_off_rule(active, /*on*/true, /*dead*/false, /*valid*/false));
      active = false;
      CHECK(freeze_transition(freeze_wanted(false, active), engaged) == FT_RELEASE);
    }

    // A force-off tic must not leave a bankable press edge: camera_toggle_tic's force-off arm once returned above the
    // edge read, so a press latched on the dead-man's-switch tic was banked and the camera came straight back up the
    // next tic. Pins the rules in camera_toggle_tic's order (camera.cpp is DLL-only).
    {
      bool active = true;         // flying
      bool edgePending = true;    // ... and the toggle key went down THIS tic
      bool cameOff = false;

      // Tic 1: the local player cannot be read, so the switch fires. The order
      // is camera_toggle_tic's: the switch first, above the index test and
      // above the edge read, and the edge discarded on the way out.
      if (freecam_force_off_rule(active, /*on*/true, /*dead*/false, /*valid*/false)) {
        active = false;
        cameOff = true;
        edgePending = false;      // the discard the finding asked for
      } else {
        const bool edge = edgePending; edgePending = false;
        if (active) { if (freecam_exit_rule(active, edge, false)) active = false; }
        else if (freecam_enter_rule(true, edge, false, false, true)) active = true;
      }
      CHECK(cameOff);             // the switch really did fire, so this is not vacuous
      CHECK(!active);
      CHECK(!edgePending);        // nothing left banked for the next tic

      // Tic 2: everything well again and nobody pressing anything. With the
      // edge banked this is where the camera used to come back up.
      if (freecam_force_off_rule(active, true, false, true)) {
        active = false;
        edgePending = false;
      } else {
        const bool edge = edgePending; edgePending = false;
        if (active) { if (freecam_exit_rule(active, edge, false)) active = false; }
        else if (freecam_enter_rule(true, edge, false, false, true)) active = true;
      }
      CHECK(!active);             // the camera stayed off

      // ... and the same sequence with the press still banked does re-enter,
      // so the check above measures something real.
      CHECK(freecam_enter_rule(true, /*keyEdge*/true, false, false, true));
    }

    // Every (panel, camera) transition in every order: the lockout is engaged exactly when somebody wants it, saved
    // once, released once, and never released while the other owner still wants it.
    {
      bool agree = true;
      for (int p0 = 0; p0 < 2; p0++)
      for (int c0 = 0; c0 < 2; c0++)
      for (int p1 = 0; p1 < 2; p1++)
      for (int c1 = 0; c1 < 2; c1++)
      for (int p2 = 0; p2 < 2; p2++)
      for (int c2 = 0; c2 < 2; c2++) {
        const bool panel[] = { p0 != 0, p1 != 0, p2 != 0 };
        const bool cam[]   = { c0 != 0, c1 != 0, c2 != 0 };
        bool engaged = false;
        int engages = 0, releases = 0;
        for (int i = 0; i < 3; i++) {
          const bool wanted = freeze_wanted(panel[i], cam[i]);
          const FreezeTransition t = freeze_transition(wanted, engaged);
          if (t == FT_ENGAGE) { engages++; engaged = true; }
          if (t == FT_RELEASE) { releases++; engaged = false; }
          if (engaged != wanted) agree = false;
          if (t == FT_RELEASE && wanted) agree = false;
        }
        if (engages > 2 || releases > 2) agree = false;
      }
      CHECK(agree);
    }

    // The activation tic acts on cleared input: getinput fills `loc` and faketimerhandler accumulates it the instant
    // that returns, so a lockout engaged any later lets one tic of real input through on every activation.
    CHECK(CAMERA_TOGGLE_PHASE == TIC_GETINPUT);
    CHECK(lockout_engaged_before_loc_read(TIC_GETINPUT));
    CHECK(!lockout_engaged_before_loc_read(TIC_PROCESSINPUT));
    CHECK(lockout_engaged_before_loc_read(CAMERA_TOGGLE_PHASE));
    CHECK(TIC_GETINPUT < TIC_PROCESSINPUT);

    // The warp echo filter: the game mouse-looks by warping the cursor back to a fixed anchor every frame, posting an
    // SDL_MOUSEMOTION whose delta is the exact negative of the movement that provoked it; the warp runs and its echo is
    // dropped. An event AT the recorded target is the echo.
    CHECK( freecam_motion_is_warp_echo(true, 960, 540, 960, 540));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, 961, 540));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, 960, 541));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, 0, 0));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, -960, -540));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, 960, 0));
    CHECK(!freecam_motion_is_warp_echo(true, 960, 540, 0, 540));
    // No warp recorded yet means no filtering: dropping on a stale (0,0)
    // would eat real motion in the top-left corner of the window.
    CHECK(!freecam_motion_is_warp_echo(false, 0, 0, 0, 0));
    CHECK(!freecam_motion_is_warp_echo(false, 960, 540, 960, 540));
    {
      bool haveWarp = false;
      int wx = 0, wy = 0;
      CHECK(!freecam_motion_is_warp_echo(haveWarp, wx, wy, 960, 540));
      haveWarp = true; wx = 960; wy = 540;                    // first warp
      CHECK( freecam_motion_is_warp_echo(haveWarp, wx, wy, 960, 540));
      CHECK(!freecam_motion_is_warp_echo(haveWarp, wx, wy, 100, 200));
      wx = 100; wy = 200;                                     // second warp
      CHECK( freecam_motion_is_warp_echo(haveWarp, wx, wy, 100, 200));
      CHECK(!freecam_motion_is_warp_echo(haveWarp, wx, wy, 960, 540));
    }
    {
      // A real movement followed by the warp that undoes it must leave the
      // camera turned by the real movement, not zero: the naive accumulator
      // (both events summed) reads 0.
      const int anchor = 960;
      const int realTo = 1010;          // the player moved 50 right
      int naive = 0, filtered = 0;
      if (!freecam_motion_is_warp_echo(true, anchor, 540, realTo, 540)) filtered += 50;
      naive += 50;
      if (!freecam_motion_is_warp_echo(true, anchor, 540, anchor, 540)) filtered += -50;
      naive += -50;
      CHECK(naive == 0);        // this is what suppression existed to avoid
      CHECK(filtered == 50);    // ... and this is what the filter gives instead
    }
    {
      // Compares the absolute position, not the relative delta: a filter
      // written against `rel` would have to pick +50 or -50 to match, and
      // either choice discards a real movement of that size.
      const int anchor = 960;
      CHECK( freecam_motion_is_warp_echo(true, anchor, 540, anchor, 540));
      CHECK(!freecam_motion_is_warp_echo(true, anchor, 540, anchor + 50, 540));
      CHECK(!freecam_motion_is_warp_echo(true, anchor, 540, anchor - 50, 540));
    }

    // Back is the exact negative of forward off the axes too: a `>> 14` floor made 45-degree forward +90 per axis and
    // back -91, which a check at ang 0 (a multiple of the scale) misses.
    {
      static short st2[2048];
      for (int i = 0; i < 2048; i++)
        st2[i] = (short)(16384.0 * sin((double)i * 2.0 * 3.14159265358979323846 / 2048.0) + 0.5);
      const FreecamInput zero = { 0, 0, false, false, false, false, false, false, false, false };
      FreecamInput f = zero; f.fwd = true;
      FreecamInput b = zero; b.back = true;
      FreecamInput r = zero; r.right = true;
      FreecamInput l = zero; l.left = true;
      bool symmetric = true, everNonZero = false;
      for (int a = 0; a < FREECAM_ANG_UNITS; a += 7) {
        FreecamState s2 = { 1000, 2000, 3000, a, 0, true };   // 0 == level pitch
        const FreecamState of = freecam_step(s2, f, 100, pct(100), st2);
        const FreecamState ob = freecam_step(s2, b, 100, pct(100), st2);
        const FreecamState orr = freecam_step(s2, r, 100, pct(100), st2);
        const FreecamState ol = freecam_step(s2, l, 100, pct(100), st2);
        if (of.x - s2.x != -(ob.x - s2.x)) symmetric = false;
        if (of.y - s2.y != -(ob.y - s2.y)) symmetric = false;
        if (orr.x - s2.x != -(ol.x - s2.x)) symmetric = false;
        if (orr.y - s2.y != -(ol.y - s2.y)) symmetric = false;
        if (of.x != s2.x || of.y != s2.y) everNonZero = true;
      }
      CHECK(symmetric);
      CHECK(everNonZero);   // the sweep really did move
      FreecamState diag = { 1000, 2000, 3000, 256, 0, true };   // 0 == level pitch
      const FreecamState od = freecam_step(diag, f, 100, pct(100), st2);
      const FreecamState odb = freecam_step(diag, b, 100, pct(100), st2);
      CHECK(od.x - diag.x == 90);
      CHECK(odb.x - diag.x == -90);
      CHECK(od.y - diag.y == 90);
      CHECK(odb.y - diag.y == -90);
    }

    {
      const int savedTab = menu_active_tab();
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Shift is faster, ctrl is slower.") == nullptr);
      CHECK(canvas_record_find_text("Shift is faster, Ctrl is slower.") == nullptr);
      CHECK(canvas_record_find_text("W A S D flies, space and C climb, Shift fast, Ctrl slow.") == nullptr);
      menu_set_tab(savedTab);
    }


    // The two heights, pinned to the literals the binary was read for: processinput's clamp VA 0x004fb67a/0x004fbf80
    // (standing eye height, 40 << 8) and VA 0x004fb8a5 (ceiling clearance), as bare literals so 0x2000 (aim()'s
    // different offset from a sprite's z) can never substitute for them.
    CHECK(PHEIGHT == 0x2800);
    CHECK(PHEIGHT == 10240);
    CHECK(PHEIGHT_MARGIN == 0x400);
    CHECK(PHEIGHT_MARGIN == 1024);
    CHECK(PHEIGHT != 0x2000);      // the value the plan text had wrong
    CHECK(PHEIGHT > PHEIGHT_MARGIN);

    {
      int sc = -1;
      CHECK(vk_to_sdl_scancode(0x54, &sc));   // 'T'
      CHECK(sc == 23);                        // SDL_SCANCODE_T
      CHECK(strcmp(freecam_key_label(0x54), "T") == 0);
      // Every letter is in the table: any standard key may be bound, a key
      // the game also uses is the player's choice, warned by the "Also
      // bound in the game" note under the row.
      int letters = 0;
      for (int vk = 0x41; vk <= 0x5A; vk++) {
        int s2 = -1;
        if (vk_to_sdl_scancode(vk, &s2)) letters++;
      }
      CHECK(letters == 26);
      CHECK(strcmp(freecam_key_label(0x56), "V") == 0);
      {
        int sv = -1;
        CHECK(vk_to_sdl_scancode(0x56, &sv));   // 'V'
        CHECK(sv == 25);                        // SDL_SCANCODE_V
      }
    }

    {
      int toggleSc = -1, teleportSc = -1;
      CHECK(vk_to_sdl_scancode(0x56, &toggleSc));
      CHECK(vk_to_sdl_scancode(0x54, &teleportSc));
      CHECK(!freecam_keys_collide(toggleSc, teleportSc));

      CHECK(freecam_keys_collide(23, 23));
      CHECK(freecam_keys_collide(25, 25));
      CHECK(!freecam_keys_collide(23, 25));
      CHECK(!freecam_keys_collide(0, 1));
      CHECK(!freecam_keys_collide(-1, -1));
      CHECK(!freecam_keys_collide(-1, 23));
      CHECK(!freecam_keys_collide(23, -1));
      {
        bool agree = true;
        for (int a = -1; a <= 3; a++)
          for (int b = -1; b <= 3; b++) {
            const bool want = a >= 0 && a == b;
            if (freecam_keys_collide(a, b) != want) agree = false;
          }
        CHECK(agree);
      }
    }

    {
      CHECK( freecam_edge_discarded_by_pause(/*paused=*/true));
      CHECK(!freecam_edge_discarded_by_pause(/*paused=*/false));

      // Driven end to end as a simulation of hooked_getinput's sequence (input.cpp cannot link into test_host); bare
      // bools stand in for the input_freecam_key_edge()/teleport_edge() latches: an edge banked while paused must be
      // gone on resume, not fired late.
      {
        bool teleportEdgeLatched = true;   // T pressed while...
        bool toggleEdgeLatched = true;     // ...V pressed on the same paused tic
        bool paused = true;                // ...Duke's own pause is up
        bool teleportFired = false, toggleActed = false;

        if (freecam_edge_discarded_by_pause(paused)) {
          teleportEdgeLatched = false;
          toggleEdgeLatched = false;
        } else {
          toggleActed = true;   // would only run if the discard did not fire
        }
        CHECK(!teleportEdgeLatched);
        CHECK(!toggleEdgeLatched);
        CHECK(!toggleActed);

        paused = false;
        if (!freecam_edge_discarded_by_pause(paused) && teleportEdgeLatched) teleportFired = true;
        CHECK(!teleportFired);

        bool edgeNoDiscard = true;
        bool pausedNoDiscard = true;
        pausedNoDiscard = false;   // resumes with the edge STILL latched
        const bool firedLateWithoutTheFix = !pausedNoDiscard && edgeNoDiscard;
        CHECK(firedLateWithoutTheFix);
      }
    }

    {
      CHECK( freecam_teleport_rule(true,  true,  false));  // flying, pressed, no panel
      CHECK(!freecam_teleport_rule(false, true,  false));  // no camera to teleport to
      CHECK(!freecam_teleport_rule(true,  false, false));  // no press edge
      CHECK(!freecam_teleport_rule(true,  true,  true));   // the panel owns input
      // Truth table written out rather than reusing the expression under
      // test, which would agree with any mutation of it.
      bool agree = true;
      for (int ac = 0; ac < 2; ac++)
      for (int ke = 0; ke < 2; ke++)
      for (int mo = 0; mo < 2; mo++) {
        const bool want = (ac != 0) && (ke != 0) && (mo == 0);
        if (freecam_teleport_rule(ac != 0, ke != 0, mo != 0) != want) agree = false;
      }
      CHECK(agree);
    }

    // freecam_hitscan_direction, the ray's vx/vy/vz: vx/vy from the yaw alone (the same sintable indices as
    // freecam_step's heading vector), vz from the pitch alone, no horizoff term (the camera has none).
    {
      static short st[2048];
      for (int i = 0; i < 2048; i++)
        st[i] = (short)(16384.0 * sin((double)i * 2.0 * 3.14159265358979323846 / 2048.0) + 0.5);

      CHECK(TELEPORT_CLEARANCE == 256);

      const HitscanDir d0 = freecam_hitscan_direction(0, 100, st);
      CHECK(d0.vx == 16384);
      CHECK(d0.vy == 0);
      CHECK(d0.vz == 0);

      const HitscanDir d512 = freecam_hitscan_direction(512, 100, st);
      CHECK(d512.vx == 0);
      CHECK(d512.vy == 16384);

      // Pitch alone: vz = ((100 - horiz) << 5) << 6, the engine's own sign (negative looking up), a different formula
      // from freecam_step's.
      const HitscanDir up = freecam_hitscan_direction(0, 150, st);
      CHECK(up.vz == ((100 - 150) << 5) << 6);
      CHECK(up.vz < 0);
      const HitscanDir down = freecam_hitscan_direction(0, 60, st);
      CHECK(down.vz == ((100 - 60) << 5) << 6);
      CHECK(down.vz > 0);
      CHECK(freecam_hitscan_direction(0, 100, st).vz == 0);   // level: exactly 0

      const HitscanDir none2 = freecam_hitscan_direction(500, 150, nullptr);
      CHECK(none2.vx == 0 && none2.vy == 0 && none2.vz == 0);
    }

    {
      int x = 0, y = 0;
      freecam_teleport_step_back(1000, 2000, 16384, 0, false, &x, &y);
      CHECK(x == 1000 && y == 2000);

      // A wall/sprite hit steps back TELEPORT_CLEARANCE ALONG THE RAY, the NEGATIVE of the direction vector: straight
      // along +x steps back along -x only.
      freecam_teleport_step_back(1000, 2000, 16384, 0, true, &x, &y);
      CHECK(x == 1000 - TELEPORT_CLEARANCE);
      CHECK(y == 2000);

      // The negative direction too: a flipped sign would step forward here
      // instead of back, landing at 1256 rather than 744.
      freecam_teleport_step_back(1000, 2000, -16384, 0, true, &x, &y);
      CHECK(x == 1000 + TELEPORT_CLEARANCE);
      CHECK(y == 2000);

      freecam_teleport_step_back(500, 500, 16384, -16384, true, &x, &y);
      CHECK(x == 500 - TELEPORT_CLEARANCE);
      CHECK(y == 500 + TELEPORT_CLEARANCE);

      // The step is a FULL TELEPORT_CLEARANCE, not merely nonzero.
      freecam_teleport_step_back(0, 0, 16384, 0, true, &x, &y);
      CHECK(x == -TELEPORT_CLEARANCE);
      CHECK(x != 0);

      freecam_teleport_step_back(1, 2, 16384, 0, true, nullptr, &y);
      freecam_teleport_step_back(1, 2, 16384, 0, true, &x, nullptr);
    }

    {
      {
        const TeleportAim a = freecam_teleport_aim(-1, -1, -1, 100, 200, 300, 16384, 0);
        CHECK(!a.ok);
        CHECK(strcmp(a.reason, "Nothing to teleport to") == 0);
        CHECK(strcmp(a.reason, TELEPORT_REASON_NOTHING) == 0);
        CHECK(a.reason[0] != '\0');   // has_notice's own gate: a refusal always says something
      }
      {
        bool allRefuse = true;
        for (int s2 = -5; s2 < 0; s2++)
          if (freecam_teleport_aim(s2, -1, -1, 1, 1, 1, 16384, 0).ok) allRefuse = false;
        CHECK(allRefuse);
      }
      {
        const TeleportAim a = freecam_teleport_aim(5, 3, -1, 1000, 2000, -500, 16384, 0);
        CHECK(a.ok);
        CHECK(a.x == 1000 - TELEPORT_CLEARANCE);
        CHECK(a.y == 2000);
        CHECK(a.z == -500);   // z is never stepped, only x,y
      }
      {
        const TeleportAim a = freecam_teleport_aim(5, -1, 9, 1000, 2000, -500, 16384, 0);
        CHECK(a.ok);
        CHECK(a.x == 1000 - TELEPORT_CLEARANCE);
      }
      {
        const TeleportAim a = freecam_teleport_aim(5, -1, -1, 1000, 2000, -500, 16384, 0);
        CHECK(a.ok);
        CHECK(a.x == 1000);
        CHECK(a.y == 2000);
      }
      CHECK(freecam_teleport_aim(0, -1, -1, 1, 1, 1, 16384, 0).ok);
    }

    {
      const TeleportPlan p = freecam_teleport_plan(1234, 5678, -1, 0, -40960, PHEIGHT);
      CHECK(!p.ok);
      CHECK(strcmp(p.reason, "Camera is outside the map") == 0);
      CHECK(strcmp(p.reason, TELEPORT_REASON_NO_SECTOR) == 0);
      CHECK(freecam_teleport_has_notice(p));
      CHECK(p.x == 1234 && p.y == 5678);
      CHECK(p.sect < 0);
      {
        bool allRefuse = true;
        for (int s2 = -5; s2 < 0; s2++)
          if (freecam_teleport_plan(1234, 5678, s2, 0, -40960, PHEIGHT).ok) allRefuse = false;
        CHECK(allRefuse);
      }
      CHECK(freecam_teleport_plan(1234, 5678, 0, 0, -40960, PHEIGHT).ok);
    }

    // freecam_teleport_plan in a normal room: floor 0, ceiling -40960 is five Build metres, far more headroom than
    // PHEIGHT + PHEIGHT_MARGIN, so the player stands exactly at florz - eyeHeight, x/y untouched.
    {
      const int FLOR = 0, CEIL = -40960;
      const TeleportPlan p = freecam_teleport_plan(111, 222, 7, FLOR, CEIL, PHEIGHT);
      CHECK(p.ok);
      CHECK(p.x == 111 && p.y == 222);
      CHECK(p.z == FLOR - PHEIGHT);
      CHECK(p.z == -10240);
      CHECK(p.sect == 7);
      CHECK(strcmp(p.reason, "") == 0);
      CHECK(!freecam_teleport_has_notice(p));

      // eyeHeight is really read, not PHEIGHT taken from inside; a tall room (50000 z units) keeps all three heights
      // clear of the "not enough room" refusal.
      CHECK(freecam_teleport_plan(0, 0, 3, 50000, 0, 100).z == 49900);
      CHECK(freecam_teleport_plan(0, 0, 3, 50000, 0, 200).z == 49800);
      CHECK(freecam_teleport_plan(0, 0, 3, 50000, 0, PHEIGHT).z == 50000 - PHEIGHT);
    }

    // Not enough room to stand is a refusal: a room shorter than eyeHeight + PHEIGHT_MARGIN refuses rather than
    // clamping the player into a crawlspace; a destination picked by looking at it has no honest silent substitute.
    {
      const int FLOR = 0, CEIL = -8000;   // 8000 z units, under PHEIGHT + margin
      CHECK(FLOR - CEIL < PHEIGHT + PHEIGHT_MARGIN);   // the space really is too tight
      const TeleportPlan p = freecam_teleport_plan(42, 43, 5, FLOR, CEIL, PHEIGHT);
      CHECK(!p.ok);
      CHECK(strcmp(p.reason, "Not enough room to stand") == 0);
      CHECK(strcmp(p.reason, TELEPORT_REASON_TIGHT) == 0);
      CHECK(freecam_teleport_has_notice(p));
      CHECK(p.sect == 5);   // still reported on a refusal, for the log line

      // The boundary between "fits" and "does not": a room exactly
      // PHEIGHT + PHEIGHT_MARGIN tall is the last one that fits.
      const TeleportPlan fits  = freecam_teleport_plan(0, 0, 5, 0, -(PHEIGHT + PHEIGHT_MARGIN), PHEIGHT);
      const TeleportPlan tight = freecam_teleport_plan(0, 0, 5, 0, -(PHEIGHT + PHEIGHT_MARGIN - 1), PHEIGHT);
      CHECK(fits.ok);
      CHECK(!freecam_teleport_has_notice(fits));
      CHECK(!tight.ok);
      CHECK(freecam_teleport_has_notice(tight));

      // Swept across many room heights: refuses below the threshold, fits at
      // or above it, never the other way round.
      {
        bool agree = true;
        for (int gap = 0; gap <= 2 * (PHEIGHT + PHEIGHT_MARGIN); gap += 97) {
          const TeleportPlan pp = freecam_teleport_plan(0, 0, 5, 0, -gap, PHEIGHT);
          const bool wantOk = gap >= PHEIGHT + PHEIGHT_MARGIN;
          if (pp.ok != wantOk) agree = false;
        }
        CHECK(agree);
      }

      CHECK(strcmp(TELEPORT_REASON_NOTHING, TELEPORT_REASON_NO_SECTOR) != 0);
      CHECK(strcmp(TELEPORT_REASON_TIGHT, TELEPORT_REASON_NO_SECTOR) != 0);
      CHECK(strcmp(TELEPORT_REASON_TIGHT, TELEPORT_REASON_NOTHING) != 0);
      CHECK(strcmp(TELEPORT_REASON_NO_WRITE, TELEPORT_REASON_NO_SECTOR) != 0);
      CHECK(strcmp(TELEPORT_REASON_NO_WRITE, TELEPORT_REASON_TIGHT) != 0);
      CHECK(strcmp(TELEPORT_REASON_NO_WRITE, TELEPORT_REASON_NOTHING) != 0);
      CHECK(strcmp(TELEPORT_REASON_NONE, "") == 0);
    }


    // What the teleport writes, field by field: engine.cpp cannot link into test_host, so this pins, as
    // view_write_source does for the view writers, which fields the writer touches and what each receives.
    {
      // The eight, named one at a time. Both members of every pair: a
      // half-written pair is the jitter signature, checked individually
      // rather than through a loop that could agree with a mutation.
      long v = 0;
      const int X = 100, Y = 200, Z = 300, S = 4;
      CHECK(teleport_write_value(CF_POSX,  X, Y, Z, S, &v) && v == X);
      CHECK(teleport_write_value(CF_OPOSX, X, Y, Z, S, &v) && v == X);
      CHECK(teleport_write_value(CF_POSY,  X, Y, Z, S, &v) && v == Y);
      CHECK(teleport_write_value(CF_OPOSY, X, Y, Z, S, &v) && v == Y);
      CHECK(teleport_write_value(CF_POSZ,  X, Y, Z, S, &v) && v == Z);
      CHECK(teleport_write_value(CF_OPOSZ, X, Y, Z, S, &v) && v == Z);
      CHECK(teleport_write_value(CF_CURSECTNUM, X, Y, Z, S, &v) && v == S);
      CHECK(teleport_write_value(CF_POSZV, X, Y, Z, S, &v) && v == 0);
      {
        int touched = 0;
        for (int f = 0; f < CF_COUNT; f++) {
          long got = 0;
          if (teleport_write_value(f, X, Y, Z, S, &got)) touched++;
        }
        CHECK(touched == 8);
      }
      // The view is not touched: the player keeps their view; half-writing
      // the camera's own yaw/pitch pair is the same jitter bug the other way.
      CHECK(!teleport_write_value(CF_ANG,        X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_OANG,       X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_HORIZ,      X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_OHORIZ,     X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_HORIZOFF,   X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_OHORIZOFF,  X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_PYOFF,      X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_OPYOFF,     X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_ROTSCRNANG, X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_OROTSCRNANG,X, Y, Z, S, &v));
      CHECK(!teleport_write_value(CF_LOOK_ANG,   X, Y, Z, S, &v));
      // A field outside the enum leaves *out alone rather than writing 0,
      // keeping "not my field" apart from CF_POSZV's legitimate 0.
      v = 0x5A5A;
      CHECK(!teleport_write_value(CF_COUNT, X, Y, Z, S, &v));
      CHECK(v == 0x5A5A);
      CHECK(!teleport_write_value(CF_POSX, X, Y, Z, S, nullptr));
      // Every field lands at its own offset/width, from the same two tables
      // the render bracket uses. A shared offset would write one field
      // twice and leave the other stale.
      {
        bool distinct = true, widthsOk = true;
        for (int a = 0; a < CF_COUNT; a++) {
          long va = 0;
          if (!teleport_write_value(a, X, Y, Z, S, &va)) continue;
          if (camera_field_width(a) <= 0) widthsOk = false;
          for (int b = a + 1; b < CF_COUNT; b++) {
            long vb = 0;
            if (!teleport_write_value(b, X, Y, Z, S, &vb)) continue;
            if (camera_field_offset(a) == camera_field_offset(b)) distinct = false;
          }
        }
        CHECK(distinct);
        CHECK(widthsOk);
      }
      CHECK(camera_field_offset(CF_POSX) != camera_field_offset(CF_OPOSX));
      CHECK(camera_field_offset(CF_POSY) != camera_field_offset(CF_OPOSY));
      CHECK(camera_field_offset(CF_POSZ) != camera_field_offset(CF_OPOSZ));
      CHECK(camera_field_offset(CF_OPOSZ) == rva::PS_OPOSZ);
      CHECK(camera_field_offset(CF_POSZV) == rva::PS_POSZV);
      CHECK(camera_field_width(CF_CURSECTNUM) == 2);   // a short, not a long
      CHECK(camera_field_width(CF_OPOSZ) == 4);
    }

    // UI copy: the reasons read as sentences (sentence case, no em dash, no full stop: the panel's copy rules, applied
    // to the four strings a player sees).
    {
      const char* reasons[4] = { TELEPORT_REASON_NOTHING, TELEPORT_REASON_NO_SECTOR,
                                 TELEPORT_REASON_TIGHT, TELEPORT_REASON_NO_WRITE };
      bool ok = true;
      for (int i = 0; i < 4; i++) {
        const char* r = reasons[i];
        if (r[0] < 'A' || r[0] > 'Z') ok = false;             // starts capitalised
        for (const char* c = r; *c; c++) {
          if ((unsigned char)*c > 126) ok = false;            // no em dash, no smart quotes
          if (*c == '.') ok = false;                          // one line, no full stop
        }
        bool hasLower = false;
        for (const char* c = r + 1; *c; c++) if (*c >= 'a' && *c <= 'z') hasLower = true;
        if (!hasLower) ok = false;
      }
      CHECK(ok);
    }

    {
      CHECK(FREECAM_NOTICE_MS == 2000);   // "about two seconds", pinned
      CHECK(!freecam_notice_visible(false, 0, 0));
      CHECK(!freecam_notice_visible(false, 0, 1000));
      CHECK(!freecam_notice_visible(false, 5000, 5001));
      CHECK( freecam_notice_visible(true, 10000, 10000));
      CHECK( freecam_notice_visible(true, 10000, 11999));
      CHECK(!freecam_notice_visible(true, 10000, 12000));
      CHECK(!freecam_notice_visible(true, 10000, 60000));
      // Across GetTickCount's 49.7-day wrap, the reason the subtraction is
      // unsigned: a signed comparison would hide the line for 49.7 days.
      {
        const unsigned setMs = 0xFFFFFF00u;   // 256 ms before the wrap
        CHECK( freecam_notice_visible(true, setMs, 0xFFFFFFFFu));
        CHECK( freecam_notice_visible(true, setMs, 100u));      // 356 ms later
        CHECK(!freecam_notice_visible(true, setMs, 2000u));     // 2256 ms later
      }
    }

    {
      // The sixth term alone: a live notice must open the bracket even
      // with every category off and no marker.
      CHECK( esp_any_category_on(0, 0, 0, 0, 0, 1, 0, 0));
      CHECK(!esp_any_category_on(0, 0, 0, 0, 0, 0, 0, 0));
      bool agree = true;
      for (int a = 0; a < 2; a++)
      for (int b = 0; b < 2; b++)
      for (int c = 0; c < 2; c++)
      for (int d = 0; d < 2; d++)
      for (int e2 = 0; e2 < 2; e2++)
      for (int f2 = 0; f2 < 2; f2++)
        if (esp_any_category_on(a, b, c, d, e2, f2, 0, 0) != (a || b || c || d || e2 || f2))
          agree = false;
      CHECK(agree);
    }

    {
      // The panel's warning yellow, shared with secret markers on purpose:
      // it already means "look here, something is not as usual".
      CHECK(canvas_notice_colour() == C_HAZARD);
      CHECK(canvas_notice_colour() == canvas_esp_secret_colour());
      CHECK(canvas_notice_colour() != canvas_esp_colour(true));
      CHECK(canvas_notice_colour() != canvas_esp_colour(false));
      CHECK(canvas_notice_colour() != canvas_player_colour());
      CHECK(canvas_notice_colour() != canvas_aim_colour());
    }

    {
      // Centred on the screen, not the text: a 400-wide line on a 3440-wide
      // display starts at 1520, not 200.
      CHECK(esp_notice_x(3440, 400) == 1520);
      CHECK(esp_notice_x(1024, 400) == 312);
      CHECK(esp_notice_x(200, 400) == 0);
      CHECK(esp_notice_x(0, 400) == 0);
      bool neverNegative = true;
      for (int w = 0; w <= 4000; w += 37)
        for (int t = 0; t <= 1200; t += 113)
          if (esp_notice_x(w, t) < 0) neverNegative = false;
      CHECK(neverNegative);
      CHECK(esp_notice_y(1440) == 1440 / ESP_NOTICE_Y_DIVISOR);
      CHECK(esp_notice_y(768) == 768 / ESP_NOTICE_Y_DIVISOR);
      CHECK(esp_notice_y(1440) > esp_notice_y(768));   // it really scales
      CHECK(esp_notice_y(1440) < 1440 / 2);            // ... and stays out of the middle
      CHECK(esp_notice_y(0) == 0);
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.freecamteleportkey == 0x54);   // 'T'
      // A bound key is not a feature: it does nothing until freecamon is on
      // and the camera has been switched on.
      CHECK(g_cfg.freecamon == 0);
      {
        int sc = -1;
        CHECK(vk_to_sdl_scancode(g_cfg.freecamteleportkey, &sc));
        CHECK(sc == 23);
      }
      // ... a different key from the toggle, or one press would do both.
      CHECK(g_cfg.freecamteleportkey != g_cfg.freecamkey);
    }
    {
      const char* t = "test_freecam_task4.ini";
      config_load(tb_path("Z:|does|not|exist.ini"));
      g_cfg.freecamteleportkey = 0x72;   // F3
      CHECK(config_save(t));
      g_cfg.freecamteleportkey = 0x2D;   // poisoned, so a no-op reload fails
      config_load(t);
      CHECK(g_cfg.freecamteleportkey == 0x72);
      remove(t);
    }
    {
      const char* t = "test_freecam_tpclamp.ini";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\n", f);
        fputs("freecamteleportkey=99999\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.freecamteleportkey == 254);   // the same 1..254 the others use
        remove(t);
      }
      f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\n", f);
        fputs("freecamteleportkey=-5\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.freecamteleportkey == 1);
        remove(t);
      }
      config_load(tb_path("Z:|does|not|exist.ini"));
    }

    // The "Teleport key" row sits in the Settings tab's Hotkeys group beside "Free camera key": read and drawn
    // together, the readout moves with the setting, "Unbound" for an unmappable code.
    {
      const int savedTab = menu_active_tab();
      const int savedOn = g_cfg.freecamon, savedKey = g_cfg.freecamkey;
      const int savedTp = g_cfg.freecamteleportkey;
      const int savedMenuK = g_cfg.menukey, savedAimK = g_cfg.aimbind;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      g_cfg.freecamon = 1;
      g_cfg.freecamkey = 0x75;            // F6
      g_cfg.freecamteleportkey = 0x54;    // T
      g_cfg.menukey = 0x2D;               // Insert, so no other field says F8
      g_cfg.aimbind = VK_BIND_XBUTTON2;   // Mouse 5, likewise
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const CanvasOp* rToggle = canvas_record_find_text("Free camera key");
      const CanvasOp* rTele   = canvas_record_find_text("Teleport key");
      CHECK(rToggle != nullptr);
      CHECK(rTele != nullptr);
      if (rToggle && rTele) CHECK(rToggle->y < rTele->y);
      const CanvasOp* rAimBtn = canvas_record_find_text("Aimbot button");
      if (rTele && rAimBtn) CHECK(rTele->y < rAimBtn->y);
      CHECK(canvas_record_find_text("T") != nullptr);
      g_cfg.freecamteleportkey = 0x77;    // F8
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("F8") != nullptr);
      // "Unbound" is the honest answer for a code with no SDL mapping (a
      // modifier, not a letter -- 'A' reads "A").
      g_cfg.freecamteleportkey = 0x10;    // VK_SHIFT: no mapping
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Unbound") != nullptr);
      g_cfg.freecamteleportkey = 0x54;
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      }
      // The two hints this row once had are on no tab; read off the View tab, where a hint put back by mistake would
      // land.
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Teleport moves you to the camera and switches it off.") == nullptr);
      // Dropped, not reworded: "Teleport key" is still a labelled capture row in the Hotkeys group, so the binding
      // stays discoverable.
      CHECK(canvas_record_find_text("Teleport moves you to where you are looking.") == nullptr);
      g_cfg.freecamon = savedOn;
      g_cfg.freecamkey = savedKey;
      g_cfg.freecamteleportkey = savedTp;
      g_cfg.menukey = savedMenuK; g_cfg.aimbind = savedAimK;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }
  }


void tb_0065_config_side_four_rows_conf() {
    Config c; memset(&c, 0, sizeof(c));
    config_bind_set(c, BIND_ROW_MENU,     0x2D);
    config_bind_set(c, BIND_ROW_AIM,      VK_BIND_XBUTTON2);
    config_bind_set(c, BIND_ROW_CAMERA,   0x56);
    config_bind_set(c, BIND_ROW_TELEPORT, 0x54);
    CHECK(config_bind_get(c, BIND_ROW_MENU) == 0x2D     && c.menukey == 0x2D);
    CHECK(config_bind_get(c, BIND_ROW_AIM) == VK_BIND_XBUTTON2 && c.aimbind == VK_BIND_XBUTTON2);
    CHECK(config_bind_get(c, BIND_ROW_CAMERA) == 0x56   && c.freecamkey == 0x56);
    CHECK(config_bind_get(c, BIND_ROW_TELEPORT) == 0x54 && c.freecamteleportkey == 0x54);
    CHECK(config_bind_get(c, -1) == 0);
    CHECK(config_bind_get(c, BIND_ROW_COUNT) == 0);
    config_bind_set(c, -1, 0x71);
    config_bind_set(c, BIND_ROW_COUNT, 0x71);
    CHECK(c.menukey == 0x2D && c.aimbind == VK_BIND_XBUTTON2);
  }




// The slider's geometry must not move with the dragged value, and its range must be the range it can apply.
// row_slider_at once subtracted the live readout's width from the track; past the screen's ceiling the readout grew a "
// max" suffix, the track shrank mid-drag, and dragging left did nothing.

// The second defect: the row's range was 75..200 while the screen could only apply 75..141 (the ceiling when reported;
// a later ruling made it 150), so many steps did nothing.
void tb_0079_phase_5b_task_5_fix_2() {
    const Config savedCfg = g_cfg;
    const int savedTab = menu_active_tab();
    const bool savedDirty = g_cfgDirty;
    const int ceil1440 = ui_scale_max_for(1440, 900, 30);
    CHECK(ceil1440 == 150);

    // A row's track width does not depend on its value: measured with the button held and the cursor off the row
    // (mid-drag), where s_scaleHeld freezes the applied scale, so any change in recorded track width comes from the
    // value.
    g_cfg.uiscale = 100;
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(3440, 1440);                      // settle the applied scale at 100
    {
      // 75 is in the list on purpose: its readout is three mono glyphs, not
      // four ("75%" against "100%"), testing digit count as well as the
      // dropped " max" suffix.
      const int stored[4] = { 75, 100, 141, 200 };  // under, at, and over the 1440p ceiling
      int trackW[4] = { 0, 0, 0, 0 };
      for (int i = 0; i < 4; i++) {
        g_cfg.uiscale = stored[i];
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = -1; g_ui.my = -1; g_ui.down = true; g_ui.pressed = false;
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        const CanvasOp* t = find_track_at_label("UI scale");
        CHECK(t);
        trackW[i] = t ? t->w : -1 - i;   // distinct on failure
      }
      CHECK(trackW[0] == trackW[1]);
      CHECK(trackW[1] == trackW[2]);
      CHECK(trackW[2] == trackW[3]);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(3440, 1440);
    }

    // Same for a row unrelated to the scale: "Field of view" spans 70..140, three glyphs at one end and four at the
    // other; every numeric slider has this in proportion to its digit-count range.
    g_cfg.uiscale = 100;
    g_cfg.fovon = 1;
    {
      int fovW[2] = { 0, 0 };
      for (int i = 0; i < 2; i++) {
        g_cfg.fov = (i == 0) ? FOV_MIN : FOV_MAX;
        menu_set_tab(TAB_VIEW);
        g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        const CanvasOp* t = find_track_at_label("Field of view");
        CHECK(t);
        fovW[i] = t ? t->w : -1 - i;
      }
      CHECK(fovW[0] == fovW[1]);
    }

    // ...and the reserve has to be a reserve: one taken from the range's low end is constant too but sizes the track
    // too wide and under its own readout. At the widest value every slider can show, no track may overlap its readout.
    g_cfg.fov = FOV_MAX;              g_cfg.espdrawdist   = ESP_DRAWDIST_MAX;
    g_cfg.freecamspeed = FREECAM_SPEED_MAX;
    g_cfg.aimcone = AIM_CONE_MAX;     g_cfg.aimrange      = AIM_RANGE_MAX;
    g_cfg.aimleadscale = AIM_LEADSCALE_MAX; g_cfg.aimsmooth = AIM_SMOOTH_MAX;
    g_cfg.silentmaxangle = SILENT_MAXANGLE_MAX;
    g_cfg.aimtarget = 1; g_cfg.aimpoint = 1;
    g_cfg.fovon = 1; g_cfg.freecamon = 1; g_cfg.aimon = 1; g_cfg.silenton = 1;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      menu_set_tab(tab);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* t = canvas_record_op(i);
        if (!t || t->kind != CANVAS_OP_BEVEL || t->colour != C_WELL) continue;
        const CanvasOp* v = find_readout_at_y(t->y);
        if (!v) continue;                       // a switch's well, not a slider's track
        CHECK(t->x + t->w <= v->x);
      }
    }
    g_cfg.fov = FOV_MIN;              g_cfg.espdrawdist   = ESP_DRAWDIST_MIN;
    g_cfg.freecamspeed = FREECAM_SPEED_MIN;
    g_cfg.aimcone = AIM_CONE_MIN;     g_cfg.aimrange      = AIM_RANGE_MIN;
    g_cfg.aimleadscale = AIM_LEADSCALE_MIN; g_cfg.aimsmooth = AIM_SMOOTH_MIN;
    g_cfg.silentmaxangle = SILENT_MAXANGLE_MIN;
    g_cfg.aimtarget = 0; g_cfg.aimpoint = 0;
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      menu_set_tab(tab);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* t = canvas_record_op(i);
        if (!t || t->kind != CANVAS_OP_BEVEL || t->colour != C_WELL) continue;
        const CanvasOp* v = find_readout_at_y(t->y);
        if (!v) continue;
        CHECK(t->x + t->w <= v->x);
      }
    }

    g_cfg = savedCfg;
    g_cfg.uiscale = 100;
    CHECK(slider_row_reaches_ends(TAB_SETTINGS, "UI scale", 3440, 1440,
                                  UI_SCALE_MIN, ceil1440, &g_cfg.uiscale, 100));
    g_cfg.uiscale = 100;
    const int ceil2160 = ui_scale_max_for(2160, 900, 30);
    CHECK(ceil2160 == UI_SCALE_MAX);
    CHECK(slider_row_reaches_ends(TAB_SETTINGS, "UI scale", 3840, 2160,
                                  UI_SCALE_MIN, ceil2160, &g_cfg.uiscale, 100));
    g_cfg.uiscale = 100;

    // No dead zone at the top of the track: hold the button and walk the cursor left one device pixel per frame from
    // the track's right edge; the value must never climb and must start falling within the first few pixels.
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* lab = canvas_record_find_text("UI scale");
      const CanvasOp* trk = find_track_at_label("UI scale");
      CHECK(lab && trk);
      if (lab && trk) {
        const int rightX = trk->x + trk->w, rowY = lab->y;
        bool nonIncreasing = true, movedEarly = false;
        int prev = 0;
        for (int f = 0; f < 20; f++) {
          menu_set_tab(TAB_SETTINGS);
          g_ui.mx = rightX - f; g_ui.my = rowY;
          g_ui.down = true; g_ui.pressed = false;
          menu_draw(3440, 1440);
          const int v = g_cfg.uiscale;
          if (f == 0) CHECK(v == ceil1440);           // the track's right end IS the ceiling
          else if (v > prev) nonIncreasing = false;   // and it never climbs walking left
          if (f > 0 && f <= 3 && v < ceil1440) movedEarly = true;
          prev = v;
        }
        CHECK(nonIncreasing);
        CHECK(movedEarly);
      }
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // An over-ceiling stored value is not rewritten by drawing: an ini written on a taller screen keeps its number
    // (knob at the right end, readout showing what is applied), and only a real drag stores anything new.
    g_cfg.uiscale = 200;
    g_cfgDirty = false;
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      char want[16];
      sprintf_s(want, "%d%%", ceil1440);
      CHECK(canvas_record_find_text(want) != nullptr);     // "141%", the applied scale
    }
    CHECK(canvas_record_find_text("200%") == nullptr);      // never the stored number
    CHECK(g_cfg.uiscale == 200);                            // ... which is still stored
    CHECK(!g_cfgDirty);                                     // and drawing dirtied nothing
    {
      // ... one real held frame at the track's middle does store a new
      // value. The track is re-found from this recording: the panel is
      // drawn at the ceiling now, so its device geometry moved.
      const CanvasOp* lab4 = canvas_record_find_text("UI scale");
      const CanvasOp* trk4 = find_track_at_label("UI scale");
      CHECK(lab4 && trk4);
      if (lab4 && trk4) {
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = trk4->x + trk4->w / 2; g_ui.my = lab4->y;
        g_ui.down = true; g_ui.pressed = false;
        menu_draw(3440, 1440);
        CHECK(g_cfg.uiscale >= UI_SCALE_MIN && g_cfg.uiscale <= ceil1440);
        CHECK(g_cfg.uiscale != 200);
        CHECK(g_cfgDirty);
        g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      }
    }

    // The " max" readout is gone entirely: the track's right end says the
    // same thing without a second vocabulary for it.
    g_cfg.uiscale = 200;
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      bool sawMax = false;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && strstr(o->text, " max")) sawMax = true;
      }
      CHECK(!sawMax);
    }

    // A screen so short that the ceiling is the floor leaves a one-value range: ui_slider_x guards hi == lo, and this
    // pins that the row still draws, reads 75% and cannot be dragged. 1280x720 is the shortest such screen (720 at 75%
    // and 768 at 80% unscale to the same 960-tall logical screen).
    g_cfg.uiscale = 200;
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_record_begin();
    menu_draw(1280, 720);
    canvas_record_end();
    CHECK(ui_scale_max_for(720, 900, 30) == UI_SCALE_MIN);
    CHECK(canvas_record_find_text("UI scale") != nullptr);
    CHECK(canvas_record_find_text("75%") != nullptr);
    {
      const CanvasOp* lab5 = canvas_record_find_text("UI scale");
      const CanvasOp* trk5 = find_track_at_label("UI scale");
      CHECK(lab5 && trk5);
      if (lab5 && trk5) {
        for (int x = trk5->x - 8; x <= trk5->x + trk5->w + 8; x++) {
          menu_set_tab(TAB_SETTINGS);
          g_ui.mx = x; g_ui.my = lab5->y; g_ui.down = true; g_ui.pressed = false;
          menu_draw(1280, 720);
        }
        CHECK(g_cfg.uiscale == 200 || g_cfg.uiscale == UI_SCALE_MIN);
        g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      }
    }
    // 1024x768 has a range of its own: ceiling 80, a stored 200 comes down
    // to it rather than to the floor. Read off the row, not the function, so
    // readout and ceiling are pinned to each other.
    g_cfg = savedCfg;
    g_cfg.uiscale = 200;
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_record_begin();
    menu_draw(1024, 768);
    canvas_record_end();
    CHECK(ui_scale_max_for(768, 900, 30) == 80);
    CHECK(canvas_record_find_text("UI scale") != nullptr);
    CHECK(canvas_record_find_text("80%") != nullptr);
    g_cfg.uiscale = 100;

    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg = savedCfg;
    menu_set_tab(savedTab);
    g_cfgDirty = savedDirty;
    canvas_set_scale(100);
    menu_draw(3440, 1440);   // one settled draw, so nothing after this inherits a latch
  }


// The free camera follows the mouse aim. It once turned a whole angle unit per count while modern mouse aim turned
// about 0.23, so it swung roughly four times faster than the gun; the rate is derived now and the slider and its config
// key are retired. Pinned: the conversion, the sub-unit carry, the retirement.
void tb_0115_camera_follows_mouse_aim() {
  short table[FREECAM_ANG_UNITS];
  for (int i = 0; i < FREECAM_ANG_UNITS; i++)
    table[i] = (short)(16384.0 * sin(6.283185307179586 * i / FREECAM_ANG_UNITS));

  {
    // Off: the camera keeps its historic one angle unit a mouse count on
    // both axes -- the identity, so nothing changes if the player never
    // switches modern mouse aim on.
    const FreecamSens off = freecam_sens_from_mouseaim(false, MOUSEAIM_SENS_DEFAULT,
                                                       MOUSEAIM_VSCALE_DEFAULT);
    CHECK(off.yawMilli == 1000);
    CHECK(off.pitchMilli == 1000);
    CHECK(off.yawMilli == freecam_sens_default().yawMilli);
    CHECK(off.pitchMilli == freecam_sens_default().pitchMilli);
    const FreecamSens off2 = freecam_sens_from_mouseaim(false, MOUSEAIM_SENS_MAX,
                                                        MOUSEAIM_VSCALE_MIN);
    CHECK(off2.yawMilli == 1000 && off2.pitchMilli == 1000);

    // ON, at the aim's own defaults: 0.040 degrees a count is 0.04 * 2048/360 = 0.22756 angle units, so 228 milli,
    // about a quarter of what the camera used to turn.
    const FreecamSens on = freecam_sens_from_mouseaim(true, 40, 100);
    CHECK(on.yawMilli == 228);
    CHECK(on.pitchMilli == 228);
    // ROUNDED TO NEAREST, not truncated: 227.56 truncating to 227 would bias every derived rate downward on both axes,
    // a systematic "slower than the gun" no carry could make up.
    CHECK(on.yawMilli != 227);
    CHECK(on.pitchMilli != 227);
    // BOTH ENDS of the aim's own range, so a conversion that happened to be
    // right at the default and wrong in its slope fails here.
    CHECK(freecam_sens_from_mouseaim(true, 5, 100).yawMilli == 28);
    CHECK(freecam_sens_from_mouseaim(true, 200, 100).yawMilli == 1138);
    CHECK(freecam_sens_from_mouseaim(true, 200, 100).yawMilli > 1000);
    CHECK(freecam_sens_from_mouseaim(true, 5, 100).yawMilli < 1000);

    // THE VERTICAL SCALE MOVES THE PITCH ONLY: the asymmetry the retired single percent could not express.
    const FreecamSens half = freecam_sens_from_mouseaim(true, 40, 50);
    CHECK(half.yawMilli == 228);
    CHECK(half.pitchMilli == 114);
    const FreecamSens onehalf = freecam_sens_from_mouseaim(true, 40, 150);
    CHECK(onehalf.yawMilli == 228);
    CHECK(onehalf.pitchMilli == 341);   // 0.06 deg * 2048/360 * 1000 = 341.3

    // OUT OF RANGE FOLLOWS THE AIM'S OWN CLAMPS, not a second set of bounds
    // invented here: sens 0 reads as 5 and sens 9999 as 200, because both
    // rates are read through mouseaim_sens_clamped/mouseaim_vscale_clamped.
    CHECK(freecam_sens_from_mouseaim(true, 0, 100).yawMilli
          == freecam_sens_from_mouseaim(true, MOUSEAIM_SENS_MIN, 100).yawMilli);
    CHECK(freecam_sens_from_mouseaim(true, 0, 100).yawMilli == 28);
    CHECK(freecam_sens_from_mouseaim(true, -50, 100).yawMilli == 28);
    CHECK(freecam_sens_from_mouseaim(true, 9999, 100).yawMilli
          == freecam_sens_from_mouseaim(true, MOUSEAIM_SENS_MAX, 100).yawMilli);
    CHECK(freecam_sens_from_mouseaim(true, 9999, 100).yawMilli == 1138);
    CHECK(freecam_sens_from_mouseaim(true, 40, 0).pitchMilli
          == freecam_sens_from_mouseaim(true, 40, MOUSEAIM_VSCALE_MIN).pitchMilli);
    CHECK(freecam_sens_from_mouseaim(true, 40, 9999).pitchMilli
          == freecam_sens_from_mouseaim(true, 40, MOUSEAIM_VSCALE_MAX).pitchMilli);

    // Both members always inside the milli bounds, over the whole cross
    // product and past both ends: no reachable pair hands freecam_step a
    // rate of zero.
    for (int sv = -25; sv <= 250; sv += 25)
      for (int vv = -25; vv <= 200; vv += 25) {
        const FreecamSens d = freecam_sens_from_mouseaim(true, sv, vv);
        CHECK(d.yawMilli >= FREECAM_SENS_MILLI_MIN && d.yawMilli <= FREECAM_SENS_MILLI_MAX);
        CHECK(d.pitchMilli >= FREECAM_SENS_MILLI_MIN && d.pitchMilli <= FREECAM_SENS_MILLI_MAX);
        CHECK(d.yawMilli == freecam_sens_milli_clamped(d.yawMilli));
        CHECK(d.pitchMilli == freecam_sens_milli_clamped(d.pitchMilli));
      }
  }

  {
    // At the aim's default rate three counts is 684 milli, not a whole unit: a camera that truncated and forgot would
    // sit still through a slow drag. Four consecutive tics pinned one at a time, since which tic turns and what stays
    // banked is the property.
    const FreecamSens aim = { 228, 228 };
    FreecamState s = { 0, 0, 0, 0, 0, true, 0, 0, 0, 0, 0 };
    CHECK(s.yawRem == 0 && s.pitchRem == 0);   // value-initialised: the right starting carry
    FreecamInput in = { 3, 0, false, false, false, false, false, false, false, false };

    const FreecamState t1 = freecam_step(s, in, 100, aim, table);
    CHECK(t1.ang == 0);         // 684 milli: not a unit yet
    CHECK(t1.yawRem == 684);
    const FreecamState t2 = freecam_step(t1, in, 100, aim, table);
    CHECK(t2.ang == 1);         // 684 + 684 = 1368: one unit, 368 banked
    CHECK(t2.yawRem == 368);
    const FreecamState t3 = freecam_step(t2, in, 100, aim, table);
    CHECK(t3.ang == 2);         // 368 + 684 = 1052: one unit, 52 banked
    CHECK(t3.yawRem == 52);
    const FreecamState t4 = freecam_step(t3, in, 100, aim, table);
    CHECK(t4.ang == 2);         // 52 + 684 = 736: nothing this tic
    CHECK(t4.yawRem == 736);
    // Nothing lost over the long run: a thousand tics of three counts is
    // exactly 684 whole units, bank empty at the end.
    {
      FreecamState run = s;
      for (int i = 0; i < 1000; i++) run = freecam_step(run, in, 100, aim, table);
      CHECK(run.ang == 684);
      CHECK(run.yawRem == 0);
    }

    // The mirror: C++ truncation puts the remainder's sign on the total's,
    // so a left drag banks negative and turns left at the same rate it
    // turns right. A floor-based division would turn faster one way.
    FreecamInput neg = { -3, 0, false, false, false, false, false, false, false, false };
    const FreecamState n1 = freecam_step(s, neg, 100, aim, table);
    CHECK(n1.ang == 0);
    CHECK(n1.yawRem == -684);
    const FreecamState n2 = freecam_step(n1, neg, 100, aim, table);
    CHECK(n2.ang == FREECAM_ANG_UNITS - 1);   // wrapped, not negative
    CHECK(n2.yawRem == -368);
    {
      FreecamState run = s;
      for (int i = 0; i < 1000; i++) run = freecam_step(run, neg, 100, aim, table);
      CHECK(run.ang == FREECAM_ANG_UNITS - 684);
      CHECK(run.yawRem == 0);
    }

    FreecamInput dyIn = { 0, 3, false, false, false, false, false, false, false, false };
    const FreecamState p1 = freecam_step(s, dyIn, 100, aim, table);
    CHECK(p1.pitch == 0);
    CHECK(p1.pitchRem == 684);
    CHECK(p1.yawRem == 0);      // the two banks are independent
    const FreecamState p2 = freecam_step(p1, dyIn, 100, aim, table);
    CHECK(p2.pitch == -1);
    CHECK(p2.pitchRem == 368);

    const FreecamSens unit = freecam_sens_default();
    const int probes[] = { 0, 1, -1, 7, -7, 37, -37, 1023, -1023 };
    for (int i = 0; i < (int)(sizeof(probes) / sizeof(probes[0])); i++) {
      FreecamInput q = { probes[i], probes[i], false, false, false, false,
                         false, false, false, false };
      const FreecamState o = freecam_step(s, q, 100, unit, table);
      CHECK(o.yawRem == 0 && o.pitchRem == 0);
      CHECK(o.ang == (int)(((long long)s.ang + probes[i]) & (FREECAM_ANG_UNITS - 1)));
      CHECK(o.pitch == clamp_int(s.pitch - probes[i], -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX));
    }
  }

  {
    FreecamState z = { 0, 0, 0, 0, 0, true, 0, 0, 0, 0, 0 };
    FreecamInput k = { 1000, 1000, false, false, false, false, false, false, false, false };
    const FreecamSens tiny = { FREECAM_SENS_MILLI_MIN, FREECAM_SENS_MILLI_MIN };
    const FreecamSens top  = { FREECAM_SENS_MILLI_MAX, FREECAM_SENS_MILLI_MAX };
    // A rate reaching the step by some route other than the derivation
    // still lands somewhere usable, same belt-and-braces as freecam_speed_clamped.
    const FreecamSens zero = { 0, 0 };
    CHECK(freecam_step(z, k, 100, zero, table).ang == freecam_step(z, k, 100, tiny, table).ang);
    CHECK(freecam_step(z, k, 100, zero, table).yawRem == freecam_step(z, k, 100, tiny, table).yawRem);
    const FreecamSens negative = { -5, -5 };
    CHECK(freecam_step(z, k, 100, negative, table).ang == freecam_step(z, k, 100, tiny, table).ang);
    CHECK(freecam_step(z, k, 100, negative, table).pitch == freecam_step(z, k, 100, tiny, table).pitch);
    const FreecamSens huge = { 100000, 100000 };
    CHECK(freecam_step(z, k, 100, huge, table).ang == freecam_step(z, k, 100, top, table).ang);
    CHECK(freecam_step(z, k, 100, huge, table).pitch == freecam_step(z, k, 100, top, table).pitch);
    CHECK(freecam_step(z, k, 100, tiny, table).ang == 1);

    // Saturation, not wrap: INT_MAX counts at the max rate is 2.1e13, so the
    // multiply is widened and the applied delta saturates at INT_MAX. An
    // int multiply would wrap the turn negative on an unpause.
    FreecamInput sat = { 2147483647, 0, false, false, false, false, false, false, false, false };
    const FreecamState os = freecam_step(z, sat, 100, top, table);
    CHECK(os.ang >= 0);
    CHECK(os.ang == (2147483647 & (FREECAM_ANG_UNITS - 1)));   // INT_MAX applied, wrapped
    CHECK(os.ang == 2047);
    CHECK(os.yawRem == 0);
    FreecamInput satNeg = { -2147483647 - 1, 0, false, false, false, false, false, false, false, false };
    const FreecamState osn = freecam_step(z, satNeg, 100, top, table);
    CHECK(osn.ang >= 0 && osn.ang < FREECAM_ANG_UNITS);

    // The accumulator's own ceiling (FREECAM_MOTION_LIMIT, input.h) at the
    // max rate applies exactly ten times, the real-world end of this range.
    FreecamInput lim = { FREECAM_MOTION_LIMIT, 0, false, false, false, false, false, false, false, false };
    const FreecamState ol = freecam_step(z, lim, 100, top, table);
    CHECK(ol.ang == (int)(((long long)10 * FREECAM_MOTION_LIMIT) & (FREECAM_ANG_UNITS - 1)));
    CHECK(ol.ang == 576);
    CHECK(ol.yawRem == 0);
  }

  {
    // A fresh camera carries nothing: remainders are unfinished motion from the previous flight and would put a stale
    // fraction of an old drag into the new one.
    PlayerPos p;
    p.posx = 500; p.posy = 600; p.posz = 700;
    p.oposx = 0; p.oposy = 0; p.oposz = 0;
    p.cursectnum = 3; p.valid = true;
    const FreecamState e = freecam_enter(p, 128, 100, table);
    CHECK(e.yawRem == 0);
    CHECK(e.pitchRem == 0);
    const FreecamState eNull = freecam_enter(p, 128, 100, 0);
    CHECK(eNull.yawRem == 0 && eNull.pitchRem == 0);

    // A null table returns the state untouched, remainders included: a
    // refused step banks nothing and spends nothing.
    FreecamState carried = { 1, 2, 3, 40, 50, true, 1, 2, 3, 40, 50 };
    carried.yawRem = 684;
    carried.pitchRem = -321;
    const FreecamSens aim = { 228, 228 };
    FreecamInput drag = { 3, 3, false, false, false, false, false, false, false, false };
    const FreecamState nul = freecam_step(carried, drag, 100, aim, 0);
    CHECK(nul.yawRem == 684);
    CHECK(nul.pitchRem == -321);
    CHECK(nul.ang == carried.ang && nul.pitch == carried.pitch);
    CHECK(nul.pang == carried.pang && nul.ppitch == carried.ppitch);
    const FreecamState live = freecam_step(carried, drag, 100, aim, table);
    CHECK(live.yawRem != 684);
  }

  {
    // An old ini still loads without error, and the dead key does not
    // survive into the unknown-line list config_save writes back: the parse
    // table recognises freecamsens and discards it.
    const Config savedCfg = g_cfg;
    const char* t = "test_freecamsens_rank9.ini";
    FILE* f = nullptr;
    CHECK(fopen_s(&f, t, "w") == 0 && f);
    if (f) { fputs("freecamsens=275\n", f); fputs("freecamspeed=42\n", f); fclose(f); }
    config_load(t);
    CHECK(g_cfg.freecamspeed == 42);   // the file really was read, line by line
    CHECK(config_save(t));
    bool sawSens = false, sawSpeed = false;
    CHECK(fopen_s(&f, t, "r") == 0 && f);
    if (f) {
      char line[512];
      while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "freecamsens")) sawSens = true;
        if (strstr(line, "freecamspeed")) sawSpeed = true;
      }
      fclose(f);
    }
    CHECK(!sawSens);    // gone from the file, not merely gone from the struct
    CHECK(sawSpeed);    // ... and the camera's other range still round trips
    remove(t);
    g_cfg = savedCfg;
  }
}

// A teleporter pair for tb_0134: the effector at (1000,2000,5000), its owner 8000/10000 away in xy and 25000 down z,
// both sectors' spans filled so one record can be re-tagged for each of the three cases.
static FreecamTransport tp_base() {
  FreecamTransport t;
  t.lotag = 0;
  t.seX = 1000; t.seY = 2000; t.seZ = 5000;
  t.ownerX = 9000; t.ownerY = 12000; t.ownerZ = 30000;
  t.ownerCeilZ = 20000; t.ownerFloorZ = 40000;
  t.secFloorZ = 16384; t.secCeilZ = -4096;
  t.tempData4 = 0;
  return t;
}

// Transports: the camera obeys movetransports' three cases.
void tb_0134_freecam_transport() {
  const int camX = 7000, camY = 7000;

  {
    FreecamTransport t = tp_base();
    const FreecamJump j = freecam_transport_jump(t, camX, camY, t.seZ);
    CHECK(j.hit == 1);
    CHECK(j.x == camX + (t.ownerX - t.seX));
    CHECK(j.y == camY + (t.ownerY - t.seY));
    CHECK(j.z == t.ownerZ + FREECAM_TRANSPORT_ZBAND);
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ + FREECAM_TRANSPORT_ZBAND - 1).hit == 1);
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ + FREECAM_TRANSPORT_ZBAND).hit == 0);
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ - FREECAM_TRANSPORT_ZBAND + 1).hit == 1);
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ - FREECAM_TRANSPORT_ZBAND).hit == 0);
    const FreecamJump miss = freecam_transport_jump(t, camX, camY, t.seZ + FREECAM_TRANSPORT_ZBAND);
    CHECK(miss.x == camX && miss.y == camY && miss.z == t.seZ + FREECAM_TRANSPORT_ZBAND);
    // temp_data[4] != 0 is one half of a water pair, never a floor teleporter.
    t.tempData4 = 1;
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ).hit == 0);
  }

  {
    FreecamTransport t = tp_base();
    t.lotag = 1;
    t.tempData4 = 1;
    const int band = t.secFloorZ - FREECAM_TRANSPORT_SURFACE;
    const FreecamJump j = freecam_transport_jump(t, camX, camY, band + 1);
    CHECK(j.hit == 1);
    CHECK(j.x == camX + (t.ownerX - t.seX));
    CHECK(j.y == camY + (t.ownerY - t.seY));
    CHECK(j.z == t.ownerCeilZ + FREECAM_TRANSPORT_LAND);
    CHECK(freecam_transport_jump(t, camX, camY, band).hit == 0);
    // Water needs temp_data[4] set, the opposite of lotag 0's test.
    t.tempData4 = 0;
    CHECK(freecam_transport_jump(t, camX, camY, band + 1).hit == 0);
  }

  {
    FreecamTransport t = tp_base();
    t.lotag = 2;
    t.tempData4 = 1;
    const int band = t.secCeilZ + FREECAM_TRANSPORT_CEILBAND;
    const FreecamJump j = freecam_transport_jump(t, camX, camY, band - 1);
    CHECK(j.hit == 1);
    CHECK(j.z == t.ownerFloorZ - FREECAM_TRANSPORT_LAND);
    CHECK(freecam_transport_jump(t, camX, camY, band).hit == 0);
    t.tempData4 = 0;
    CHECK(freecam_transport_jump(t, camX, camY, band - 1).hit == 0);
  }

  {
    FreecamTransport t = tp_base();
    t.ownerX = t.seX; t.ownerY = t.seY; t.ownerZ = t.seZ;
    CHECK(freecam_transport_jump(t, camX, camY, t.seZ).hit == 0);
    // A lotag this rule does not know, and engine_sector_lotag's own -1 for a
    // sector it may not read.
    FreecamTransport u = tp_base();
    u.lotag = 3;
    CHECK(freecam_transport_jump(u, camX, camY, u.seZ).hit == 0);
    u.lotag = -1;
    CHECK(freecam_transport_jump(u, camX, camY, u.seZ).hit == 0);
  }
}

// The teleport landing marker's one pure rule: the column is geometry only; where the point comes from (plan_teleport)
// and how it is drawn (camera_draw_teleport_marker, GL) are the DLL's.
void tb_0136_teleport_marker() {
  {
    const int x = 1000, y = -2000, z = 30000;
    const FreecamMarkBox b = freecam_teleport_box(x, y, z);
    int seen = 0;
    for (int i = 0; i < 4; i++) {
      const int dx = b.c[i].x - x, dy = b.c[i].y - y;
      CHECK(dx == FREECAM_TPMARK_HALF || dx == -FREECAM_TPMARK_HALF);
      CHECK(dy == FREECAM_TPMARK_HALF || dy == -FREECAM_TPMARK_HALF);
      seen |= 1 << (((dx > 0) ? 1 : 0) | ((dy > 0) ? 2 : 0));
    }
    CHECK(seen == 0xF);
    // Consecutive corners, 3 -> 0 included, share exactly one coordinate:
    // that is a side of the column, never its diagonal.
    for (int i = 0; i < 4; i++) {
      const FreecamMarkCorner& p = b.c[i];
      const FreecamMarkCorner& q = b.c[(i + 1) & 3];
      CHECK((p.x == q.x) != (p.y == q.y));
    }
    // Standing on the point: the bottom is the point's own z and the top is
    // one column height above it, which in Build's downward z is smaller.
    CHECK(b.zBottom == z);
    CHECK(b.zTop == z - FREECAM_TPMARK_HEIGHT);
    CHECK(b.zTop < b.zBottom);
  }

  {
    const int pts[6] = { 0, 1, -1, 32768, -32768, 1000000 };
    for (int i = 0; i < 6; i++) {
      const FreecamMarkBox b = freecam_teleport_box(pts[i], pts[5 - i], pts[i]);
      CHECK(b.c[0].x == pts[i] - FREECAM_TPMARK_HALF);
      CHECK(b.c[1].x == pts[i] + FREECAM_TPMARK_HALF);
      CHECK(b.c[0].y == pts[5 - i] - FREECAM_TPMARK_HALF);
      CHECK(b.c[2].y == pts[5 - i] + FREECAM_TPMARK_HALF);
      CHECK(b.zBottom == pts[i]);
      CHECK(b.zTop == pts[i] - FREECAM_TPMARK_HEIGHT);
    }
  }

  {
    // The point is engine memory this DLL does not own, so a coordinate at
    // the int limit must not put a corner on the far side of the map.
    const int hi = 2147483647, lo = -2147483647 - 1;
    const FreecamMarkBox b = freecam_teleport_box(hi, lo, lo);
    CHECK(b.c[0].x == hi - FREECAM_TPMARK_HALF);
    CHECK(b.c[1].x == hi);                 // saturated, not wrapped negative
    CHECK(b.c[0].y == lo);                 // saturated, not wrapped positive
    CHECK(b.c[2].y == lo + FREECAM_TPMARK_HALF);
    CHECK(b.zBottom == lo);
    CHECK(b.zTop == lo);                   // saturated: still not below the bottom
    CHECK(b.zTop <= b.zBottom);
    const FreecamMarkBox c = freecam_teleport_box(lo, hi, hi);
    CHECK(c.c[0].x == lo && c.c[1].x == lo + FREECAM_TPMARK_HALF);
    CHECK(c.c[0].y == hi - FREECAM_TPMARK_HALF && c.c[2].y == hi);
    CHECK(c.zBottom == hi && c.zTop == hi - FREECAM_TPMARK_HEIGHT);
  }
}
