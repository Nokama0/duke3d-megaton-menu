#include "test_common.h"
#include "../mouseaim.h"
#include "../nocull.h"   // nocull_available/nocull_mark_unavailable/nocull_reset_available

void tb_0084_phase_7_mouse_aim_rules() {
  CHECK(mouseaim_lead_capped(10.0, 64) == 10.0);
  CHECK(mouseaim_lead_capped(100.0, 64) == 64.0);
  CHECK(mouseaim_lead_capped(-100.0, 64) == -64.0);
  CHECK(mouseaim_lead_capped(2048.0 + 10.0, 64) == 10.0);    // wraps first
  CHECK(mouseaim_lead_capped(-2048.0 - 100.0, 64) == -64.0);
  CHECK(mouseaim_lead_capped(100.0, 0) == 100.0);            // no cap
  CHECK(MOUSEAIM_LEAD_CAP_UNITS == 64);
  // The cap follows the frame's own duration: the reference frame and faster keep the base, a slow frame scales, a
  // stall stops at 8x.
  CHECK(mouseaim_lead_cap_for_frame(64, 0) == 64);            // no measurement yet
  CHECK(mouseaim_lead_cap_for_frame(64, 3000) == 64);         // ~330 fps
  CHECK(mouseaim_lead_cap_for_frame(64, MOUSEAIM_LEAD_CAP_REF_US) == 64);
  CHECK(mouseaim_lead_cap_for_frame(64, 11400) == 128);       // half the reference rate
  CHECK(mouseaim_lead_cap_for_frame(64, 25000) == 280);       // 40 fps
  CHECK(mouseaim_lead_cap_for_frame(64, 200000) == 512);      // clamped at 8x
  CHECK(mouseaim_lead_cap_for_frame(0, 25000) == 0);          // 0 stays uncapped
  CHECK(mouseaim_lead_cap_for_frame(64, 25000) < 512);
  CHECK(mouseaim_sens_clamped(0) == MOUSEAIM_SENS_MIN && mouseaim_sens_clamped(999) == MOUSEAIM_SENS_MAX && mouseaim_sens_clamped(40) == 40);
  CHECK(mouseaim_vscale_clamped(0) == 50 && mouseaim_vscale_clamped(200) == 150);
  CHECK(fabs(mouseaim_yaw_units_per_count(40) - 0.04 * 2048.0 / 360.0) < 1e-9);
  CHECK(fabs(mouseaim_pitch_deg_per_count(40, 100) - 0.04) < 1e-9);
  CHECK(fabs(mouseaim_pitch_deg_per_count(40, 50) - 0.02) < 1e-9);
  CHECK(fabs(mouseaim_horiz_of_pitch_deg(0.0) - 100.0) < 1e-9);
  CHECK(fabs(mouseaim_pitch_deg_of_horiz(mouseaim_horiz_of_pitch_deg(30.0)) - 30.0) < 1e-9);
  CHECK(mouseaim_horiz_of_pitch_deg(89.0) < 299.0 && mouseaim_horiz_of_pitch_deg(-89.0) > -99.0);   // clamped inside the game's range
  {
    MouseAimState s; mouseaim_reset(&s);
    const double upc = mouseaim_yaw_units_per_count(40);
    long total = 0;
    for (int i = 0; i < 1000; i++) {
      const int c = (i % 10 < 3) ? 1 : 0;   // 0.3 counts per tic on average
      MouseAimTic t = mouseaim_tic(&s, c, 0, 40, 100, 0, 0, 100, false, true);
      total += 2 * t.avelAdd;               // one avel is two angle units
    }
    const double expect = 300.0 * upc;
    CHECK(fabs((double)total + s.yawPending - expect) < 1e-6);
    CHECK(fabs(s.yawPending) < 2.0);   // carry never exceeds one avel
  }
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimTic t = mouseaim_tic(&s, 100000, 0, 200, 100, 0, 0, 100, false, true);
    CHECK(t.avelAdd == 127);
    CHECK(s.yawPending > 0.0);
    MouseAimTic t2 = mouseaim_tic(&s, 0, 0, 200, 100, 0, 0, 100, false, true);
    CHECK(t2.avelAdd == 127);
    MouseAimState k; mouseaim_reset(&k);
    MouseAimTic t3 = mouseaim_tic(&k, 100000, 0, 200, 100, 100, 0, 100, false, true);
    CHECK(t3.avelAdd == 27);
  }
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimTic t = mouseaim_tic(&s, 0, -200, 40, 100, 0, 0, 100, false, true);
    CHECK(t.horzAdd > 0 && (t.horzAdd % 2) == 0);
    MouseAimState f; mouseaim_reset(&f);
    MouseAimTic tf = mouseaim_tic(&f, 0, -200, 40, 100, 0, 0, 100, true, true);
    CHECK(tf.horzAdd < 0 && tf.horzAdd == -t.horzAdd);
    MouseAimState a; mouseaim_reset(&a);
    MouseAimTic ta = mouseaim_tic(&a, 0, -200, 40, 100, 0, 5, 100, false, false);
    CHECK(ta.horzAdd == 0 && a.horizPending == 0.0);
    MouseAimState y; mouseaim_reset(&y); MouseAimState p; mouseaim_reset(&p);
    MouseAimTic ty = mouseaim_tic(&y, 100, 0, 40, 100, 0, 0, 100, false, true);
    MouseAimTic tp = mouseaim_tic(&p, 0, -100, 40, 100, 0, 0, 100, false, true);
    const double yawDeg = (2 * ty.avelAdd + y.yawPending) * 360.0 / 2048.0;
    const double pitchDeg = mouseaim_pitch_deg_of_horiz(100.0 + tp.horzAdd / 2.0 + p.horizPending);
    CHECK(fabs(yawDeg - pitchDeg) < 0.01);
    MouseAimState q; mouseaim_reset(&q);
    MouseAimTic tq = mouseaim_tic(&q, 0, -1, 40, 100, 0, 0, 100, false, true);
    CHECK(tq.horzAdd == 0 && q.horizPending > 0.0);
  }
  // horizNow must be ps.horiz + locHorz/2 (this tic's keyboard look), not ps.horiz alone, so the tan linearisation is
  // centred; the difference's sign comes from mouseaim_pitch_units_delta, not an assumption.
  {
    const double d100 = mouseaim_pitch_units_delta(-100, 40, 100, false, 100.0);
    const double d110 = mouseaim_pitch_units_delta(-100, 40, 100, false, 110.0);
    CHECK(fabs(d100 - d110) > 1e-6);
    const bool d110Smaller = fabs(d110) < fabs(d100);
    MouseAimState s0; mouseaim_reset(&s0);
    MouseAimTic t0 = mouseaim_tic(&s0, 0, -100, 40, 100, 0, 0, 100, false, true);    // locHorz 0, horizNow 100
    MouseAimState s1; mouseaim_reset(&s1);
    MouseAimTic t1 = mouseaim_tic(&s1, 0, -100, 40, 100, 0, 20, 110, false, true);   // locHorz 20, horizNow 110
    const double mag0 = fabs((double)t0.horzAdd + s0.horizPending);
    const double mag1 = fabs((double)t1.horzAdd + s1.horizPending);
    CHECK(fabs(mag0 - mag1) > 1e-6);
    CHECK(d110Smaller ? (mag1 < mag0) : (mag1 > mag0));
  }
  // The view is continuous across a tic (before/after/mid-interpolation/caught-up agree); an idle state returns the
  // engine's view.
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimView before = mouseaim_view(s, 1000, 100.0, 37, 0, 40, 100, false, true, 1.0);
    MouseAimTic t = mouseaim_tic(&s, 37, 0, 40, 100, 0, 0, 100, false, true);
    MouseAimView after = mouseaim_view(s, 1000, 100.0, 0, 0, 40, 100, false, true, 0.0);
    CHECK(fabs(before.ang - after.ang) < 1e-9);
    MouseAimView mid = mouseaim_view(s, 1000 + t.avelAdd, 100.0, 0, 0, 40, 100, false, true, 0.5);
    CHECK(fabs(mid.ang - before.ang) < 1e-9);
    MouseAimView end = mouseaim_view(s, 1000 + 2 * t.avelAdd, 100.0, 0, 0, 40, 100, false, true, 1.0);
    CHECK(fabs(end.ang - before.ang) < 1e-9);
    MouseAimState z; mouseaim_reset(&z);
    MouseAimView v0 = mouseaim_view(z, 512, 130.0, 0, 0, 40, 100, false, true, 0.3);
    CHECK(v0.ang == 512.0 && v0.horiz == 130.0);
  }
  CHECK(mouseaim_apply_for_player(3, 3));
  CHECK(!mouseaim_apply_for_player(1, 3));
  CHECK( mouseaim_gate(true,  true,  true,  false, false, false, false, false));
  CHECK(!mouseaim_gate(false, true,  true,  false, false, false, false, false));
  CHECK(!mouseaim_gate(true,  false, true,  false, false, false, false, false));
  CHECK(!mouseaim_gate(true,  true,  false, false, false, false, false, false));
  CHECK(!mouseaim_gate(true,  true,  true,  true,  false, false, false, false));   // free camera
  CHECK(!mouseaim_gate(true,  true,  true,  false, true,  false, false, false));   // panel lockout
  CHECK(!mouseaim_gate(true,  true,  true,  false, false, true,  false, false));   // game menu
  CHECK(!mouseaim_gate(true,  true,  true,  false, false, false, true,  false));   // paused
  CHECK(!mouseaim_gate(true,  true,  true,  false, false, false, false, true));    // viewscreen
  // "installed" is s_installed && nocull_available() (mouseaim.cpp): pins the compound against nocull.h's real latch.
  {
    nocull_reset_available();
    CHECK(nocull_available());
    nocull_mark_unavailable();
    const bool installed = true && nocull_available();   // thunks ok, redirect did not install
    CHECK(!installed);
    CHECK(!mouseaim_gate(true, true, installed, false, false, false, false, false));
    CHECK(strcmp(mouseaim_gate_reason(true, true, installed, false, false, false, false, false), "hooks not installed") == 0);
    nocull_reset_available();
  }
  // The six doubles match polymost's arithmetic at integer inputs (ang 300, level horiz, viewingrange 65536, halfx
  // 1720, ydimen 1440).
  {
    ViewDoubles d = mouseaim_view_doubles(300.0, 720.0, 65536, 1720, 1440);
    CHECK(fabs(d.cosang - cos(300.0 * 3.14159265358979323846 / 1024.0) / 16.0) < 1e-12);
    CHECK(fabs(d.sinang - sin(300.0 * 3.14159265358979323846 / 1024.0) / 16.0) < 1e-12);
    CHECK(fabs(d.cosang2 - d.cosang) < 1e-12 && fabs(d.sinang2 - d.sinang) < 1e-12);   // viewingrange 65536 == 1.0
    CHECK(fabs(d.shang) < 1e-12 && fabs(d.chang - 1.0) < 1e-12);                        // level: dh == 0
    ViewDoubles u = mouseaim_view_doubles(300.0, 600.0, 65536, 1720, 1440);              // horizon 120 px above centre
    CHECK(u.shang > 0.0 && fabs(u.shang * u.shang + u.chang * u.chang - 1.0) < 1e-12);
    ViewDoubles h = mouseaim_view_doubles(300.5, 720.0, 65536, 1720, 1440);
    ViewDoubles n = mouseaim_view_doubles(301.0, 720.0, 65536, 1720, 1440);
    CHECK((h.sinang > d.sinang && h.sinang < n.sinang) || (h.sinang < d.sinang && h.sinang > n.sinang));
    CHECK(fabs(mouseaim_horiz_units_to_px(100.0, 65536, 1440) - 720.0) < 1e-9);
    CHECK(fabs(mouseaim_horiz_units_to_px(101.0, 131072, 1440) - 722.0) < 1e-9);
  }
  #define P7_REL32(b) ((unsigned)(b)[1] | ((unsigned)(b)[2] << 8) | ((unsigned)(b)[3] << 16) | ((unsigned)(b)[4] << 24))
  CHECK(thunk_decode_target(rva::THUNK_DNGETMOUSEINPUT_RVA, P7_REL32(rva::THUNK_DNGETMOUSEINPUT_BYTES)) == rva::DNGETMOUSEINPUT_BODY_RVA);
  CHECK(thunk_decode_target(rva::THUNK_READMOUSEXY_RVA,     P7_REL32(rva::THUNK_READMOUSEXY_BYTES))     == rva::READMOUSEXY_BODY_RVA);
  #undef P7_REL32
  CHECK(rva::PS_AIM_MODE == 640 && rva::UD_MOUSEFLIP == 2384 && rva::MM_DX == 0x001D3048 && rva::MM_DY == 0x001D3044 && rva::MM_NUMDEVICES == 0x001D3054);
}

// The yaw/horiz pending accumulators and mouseaim_view's pending-count terms are clamped so a stuck mouse or a refusing
// clamp cannot grow them without bound.
void tb_0087_phase_7_fix1_pending_caps() {
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimTic t = mouseaim_tic(&s, 10000000, 0, 200, 100, 0, 0, 100, false, true);
    CHECK(t.avelAdd == 127);
    CHECK(s.yawPending == MOUSEAIM_YAW_PENDING_MAX);
    MouseAimTic t2 = mouseaim_tic(&s, 0, 0, 200, 100, 0, 0, 100, false, true);
    CHECK(t2.avelAdd == 127);
  }
  {
    MouseAimState s; mouseaim_reset(&s);
    for (int i = 0; i < 100; i++)
      mouseaim_tic(&s, 0, -1000, 40, 100, 0, 127, 100, false, true);
    CHECK(s.horizPending == MOUSEAIM_HORIZ_PENDING_MAX);
  }
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimView v = mouseaim_view(s, 1000, 100.0, 100000000, 0, 40, 100, false, true, 1.0);
    CHECK(v.ang <= 1000.0 + MOUSEAIM_YAW_PENDING_MAX + 2.0);
    CHECK(v.ang >= 1000.0 - MOUSEAIM_YAW_PENDING_MAX - 2.0);
  }
  // mouseaim_gate_reason agrees with mouseaim_gate over all 128 combinations of the seven bools, plus each reason
  // string.
  {
    bool agree = true;
    for (int m = 0; m < 128; m++) {
      const bool on   = (m & 1) != 0, ver  = (m & 2) != 0,  ins  = (m & 4) != 0;
      const bool cam  = (m & 8) != 0, lock = (m & 16) != 0, menu = (m & 32) != 0;
      const bool paus = (m & 64) != 0;
      const bool gate = mouseaim_gate(on, ver, ins, cam, lock, menu, paus, false);
      const bool says = strcmp(mouseaim_gate_reason(on, ver, ins, cam, lock, menu, paus, false), "on") == 0;
      if (gate != says) agree = false;
    }
    CHECK(agree);
    CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, false, false, false, false), "on") == 0);
    CHECK(strcmp(mouseaim_gate_reason(false, true, true, false, false, false, false, false), "switch off") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, true, false, false, false, false, false, false), "hooks not installed") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, false, true, false, false, false, false, false), "engine or player unavailable") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, true, true, true, false, false, false, false), "free camera") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, true, false, false, false), "panel lockout") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, false, true, false, false), "game menu") == 0);
    CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, false, false, true, false), "paused") == 0);
  }
  {
    CHECK(mouseaim_add_counts(0, 12) == 12);
    CHECK(mouseaim_add_counts(12, -30) == -18);
    CHECK(mouseaim_add_counts(MOUSEAIM_COUNTS_MAX - 5, 100) == MOUSEAIM_COUNTS_MAX);
    CHECK(mouseaim_add_counts(-MOUSEAIM_COUNTS_MAX + 5, -100) == -MOUSEAIM_COUNTS_MAX);
    CHECK(mouseaim_add_counts(MOUSEAIM_COUNTS_MAX, 2000000000) == MOUSEAIM_COUNTS_MAX);
    CHECK(mouseaim_add_counts(-MOUSEAIM_COUNTS_MAX, -2000000000) == -MOUSEAIM_COUNTS_MAX);
    CHECK(mouseaim_add_counts(MOUSEAIM_COUNTS_MAX, -MOUSEAIM_COUNTS_MAX) == 0);   // clamp, not a latch
  }
}

// The six view doubles the render override writes reproduce proj_gproj_recomputed (polymost_drawrooms' nine
// assignments) at integer inputs: the only thing that makes the override safe, since the suite can't see the
// live-memory path. The sintable fixture is rounded symmetrically (cardinals exact, 1e-9).
void tb_0088_phase_7_task4_view_doubles_match_polymost() {
  static short st[2048];
  for (int i = 0; i < 2048; i++) {
    const double s = 16384.0 * sin((double)i * 2.0 * 3.14159265358979323846 / 2048.0);
    st[i] = (short)(s >= 0.0 ? s + 0.5 : s - 0.5);
  }

  CamState cam = {};
  cam.posx = 0; cam.posy = 0; cam.posz = 0;
  cam.ydimen = 1440;
  cam.halfxdimen = 1720;
  cam.xdimenscale = 65536;
  cam.viewingrange = 87381;      // not the neutral 65536, so the *2 pair's own factor is exercised
  cam.windowx1 = 0; cam.windowy1 = 0;
  cam.valid = true;

  const int horizons[3] = { 720, 462, 978 };   // level, looking down, looking up (viewport pixels)

  {
    const int angles[3] = { 0, 512, 1024 };
    for (int a = 0; a < 3; a++) {
      for (int h = 0; h < 3; h++) {
        CamState c = cam; c.ang = angles[a]; c.horiz = horizons[h];
        const GProj g = proj_gproj_recomputed(c, st);
        const ViewDoubles d = mouseaim_view_doubles((double)c.ang, (double)c.horiz,
                                                    c.viewingrange, c.halfxdimen, c.ydimen);
        CHECK(g.valid);
        CHECK(fabs(d.cosang  - g.cosang)  < 1e-9);
        CHECK(fabs(d.sinang  - g.sinang)  < 1e-9);
        CHECK(fabs(d.cosang2 - g.cosang2) < 1e-9);
        CHECK(fabs(d.sinang2 - g.sinang2) < 1e-9);
        CHECK(fabs(d.shang   - g.shang)   < 1e-9);
        CHECK(fabs(d.chang   - g.chang)   < 1e-9);
      }
    }
  }

  // Three ordinary angles, to the table's 16-bit resolution: 1e-5 is above half a table step (1.9e-6) and far below
  // real divergence.
  {
    const int angles[3] = { 137, 613, 1801 };
    for (int a = 0; a < 3; a++) {
      for (int h = 0; h < 3; h++) {
        CamState c = cam; c.ang = angles[a]; c.horiz = horizons[h];
        const GProj g = proj_gproj_recomputed(c, st);
        const ViewDoubles d = mouseaim_view_doubles((double)c.ang, (double)c.horiz,
                                                    c.viewingrange, c.halfxdimen, c.ydimen);
        CHECK(g.valid);
        CHECK(fabs(d.cosang  - g.cosang)  < 1e-5);
        CHECK(fabs(d.sinang  - g.sinang)  < 1e-5);
        CHECK(fabs(d.cosang2 - g.cosang2) < 1e-5);
        CHECK(fabs(d.sinang2 - g.sinang2) < 1e-5);
        // shang/chang skip the table (same double arithmetic on the same integers): a hair-thin epsilon pins exact
        // equality without being hostage to a future /fp: or /arch: change.
        CHECK(fabs(d.shang - g.shang) < 1e-12);
        CHECK(fabs(d.chang - g.chang) < 1e-12);
      }
    }
  }

  {
    const int scales[2] = { 65536, 131072 };
    for (int s = 0; s < 2; s++) {
      for (int h = 0; h < 3; h++) {
        const double units = 100.0 + ((double)horizons[h] - (double)(cam.ydimen >> 1)) * 65536.0 / (double)scales[s];
        CHECK(fabs(mouseaim_horiz_units_to_px(units, scales[s], cam.ydimen) - (double)horizons[h]) < 1e-9);
      }
    }
  }
}

// Black void, morphing textures and view turning while paused had two causes: the render override deciding on the
// render clock instead of the tic clock, and moving pitch after polymost_drawrooms built the screen clip polygon from
// the old one.
void tb_0090_phase_7_fixb_viewscreen_gate() {
  {
    bool agree = true;
    for (int m = 0; m < 256; m++) {
      const bool on   = (m & 1)   != 0, ver  = (m & 2)   != 0, ins  = (m & 4)  != 0;
      const bool cam  = (m & 8)   != 0, lock = (m & 16)  != 0, menu = (m & 32) != 0;
      const bool paus = (m & 64)  != 0, view = (m & 128) != 0;
      const bool gate = mouseaim_gate(on, ver, ins, cam, lock, menu, paus, view);
      const bool says = strcmp(mouseaim_gate_reason(on, ver, ins, cam, lock, menu, paus, view), "on") == 0;
      if (gate != says) agree = false;
    }
    CHECK(agree);
  }
  CHECK(!mouseaim_gate(true, true, true, false, false, false, false, true));
  CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, false, false, false, true), "viewscreen") == 0);
  CHECK( mouseaim_gate(true, true, true, false, false, false, false, false));
  CHECK(rva::PS_NEWOWNER == 708);
}
void tb_0091_phase_7_fixc_frame_gates_and_frustum() {
  // The render side asks the same eight-term gate on its own clock: Duke's pause stops the tic clock, and s_active
  // would stay true.
  CHECK(!mouseaim_gate(true, true, true, false, false, false, true, false));    // paused
  CHECK(strcmp(mouseaim_gate_reason(true, true, true, false, false, false, true, false), "paused") == 0);
  CHECK(!mouseaim_gate(true, true, true, false, false, true, false, false));    // the game's menu
  CHECK(!mouseaim_gate(true, true, true, false, true, false, false, false));    // the panel's lockout
  CHECK( mouseaim_gate(true, true, true, false, false, false, false, false));


  // The screen clip polygon polymost_drawrooms builds from the viewport corners (tilt, pitch, clipped to z > 1, handed
  // to initmosts). The override moved pitch AFTER that build, so geometry drew against a clip built for another pitch.
  // This block is the rebuild, transcribed from the engine's loop (3440x1440, ghalfx 1720, ghoriz 720).
  {
    const int wx1 = 0, wy1 = 0, wx2 = 3439, wy2 = 1439;
    const double ghalfx = 1720.0, ghoriz = 720.0;

    {
      const Frustum f = mouseaim_frustum(wx1, wy1, wx2, wy2, ghalfx, ghoriz, 1.0, 0.0, 0.0, 1.0);
      CHECK(f.n == 4);
      const double ex[4] = { -1.0, 3442.0, 3442.0, -1.0 };
      const double ey[4] = { -1.0, -1.0, 1442.0, 1442.0 };
      for (int i = 0; i < 4; i++) {
        CHECK(fabs(f.px[i] - ex[i]) < 1e-9);
        CHECK(fabs(f.py[i] - ey[i]) < 1e-9);
      }
    }

    // Pitched (gshang 0.3, gchang sqrt(0.91)): the top two points move to where a hand formula from the same inputs
    // puts them.
    {
      const double gshang = 0.3, gchang = sqrt(0.91);
      const Frustum f = mouseaim_frustum(wx1, wy1, wx2, wy2, ghalfx, ghoriz, 1.0, 0.0, gshang, gchang);
      CHECK(f.n == 4);
      const double topx[2] = { -1.0, 3442.0 };
      for (int i = 0; i < 2; i++) {
        const double ox = topx[i] - ghalfx, oy = -1.0 - ghoriz;   // no tilt: tx == ox, ty == oy
        const double vy = ghalfx * gshang + oy * gchang;
        const double vz = ghalfx * gchang - oy * gshang;
        CHECK(vz > 1.0);
        const double r = ghalfx / vz;
        CHECK(fabs(f.px[i] - (ox * r + ghalfx)) < 1e-9);
        CHECK(fabs(f.py[i] - (vy * r + ghoriz)) < 1e-9);
        CHECK(fabs(f.py[i] - (-1.0)) > 1.0);   // the pitch really did move them
      }
    }

    // Tilted (gctang cos(0.1), gstang sin(0.1)), level pitch: px changes, the term most likely dropped in a
    // transcription.
    {
      const double gctang = cos(0.1), gstang = sin(0.1);
      const Frustum lvl = mouseaim_frustum(wx1, wy1, wx2, wy2, ghalfx, ghoriz, 1.0, 0.0, 0.0, 1.0);
      const Frustum f   = mouseaim_frustum(wx1, wy1, wx2, wy2, ghalfx, ghoriz, gctang, gstang, 0.0, 1.0);
      CHECK(f.n == 4);
      bool moved = false;
      for (int i = 0; i < 4; i++) if (fabs(f.px[i] - lvl.px[i]) > 1.0) moved = true;
      CHECK(moved);
      const double ox = -1.0 - ghalfx, oy = -1.0 - ghoriz;
      const double tx = oy * gstang + ox * gctang;
      const double ty = oy * gctang - ox * gstang;
      CHECK(fabs(f.px[0] - (tx + ghalfx)) < 1e-9);
      CHECK(fabs(f.py[0] - (ty + ghoriz)) < 1e-9);
    }

    // One corner behind the near plane (steep pitch down, 0.35 rad tilt so exactly one corner crosses): the ring gains
    // two crossings at z == 1 and the polygon has five points.
    {
      const double gshang = -0.9, gchang = sqrt(0.19);
      const double gctang = cos(0.35), gstang = sin(0.35);
      const Frustum f = mouseaim_frustum(wx1, wy1, wx2, wy2, ghalfx, ghoriz, gctang, gstang, gshang, gchang);

      const double sx[4] = { -1.0, 3442.0, 3442.0, -1.0 };
      const double sy[4] = { -1.0, -1.0, 1442.0, 1442.0 };
      double vx[4], vy[4], vz[4];
      for (int i = 0; i < 4; i++) {
        const double ox = sx[i] - ghalfx, oy = sy[i] - ghoriz;
        const double tx = oy * gstang + ox * gctang;
        const double ty = oy * gctang - ox * gstang;
        vx[i] = tx;
        vy[i] = ghalfx * gshang + ty * gchang;
        vz[i] = ghalfx * gchang - ty * gshang;
      }
      CHECK(vz[0] > 1.0 && vz[1] <= 1.0 && vz[2] > 1.0 && vz[3] > 1.0);   // exactly corner 1 is behind
      CHECK(f.n == 5);
      for (int i = 0; i < f.n; i++) {
        CHECK(f.px[i] == f.px[i] && fabs(f.px[i]) < 1e300);   // finite, not NaN or infinity
        CHECK(f.py[i] == f.py[i] && fabs(f.py[i]) < 1e300);
      }
      const int keep[3] = { 0, 2, 3 };   // surviving corners, engine's own order
      const int at[3]   = { 0, 3, 4 };
      for (int k = 0; k < 3; k++) {
        const double r = ghalfx / vz[keep[k]];
        CHECK(fabs(f.px[at[k]] - (vx[keep[k]] * r + ghalfx)) < 1e-9);
        CHECK(fabs(f.py[at[k]] - (vy[keep[k]] * r + ghoriz)) < 1e-9);
      }
      const int ei[2] = { 0, 1 }, ej[2] = { 1, 2 };   // the two crossings, at z == 1 so radius is ghalfx
      for (int e = 0; e < 2; e++) {
        const int i = ei[e], j = ej[e];
        const double t = (1.0 - vz[i]) / (vz[j] - vz[i]);
        CHECK(t > 0.0 && t < 1.0);
        const double cx = (vx[j] - vx[i]) * t + vx[i];
        const double cy = (vy[j] - vy[i]) * t + vy[i];
        CHECK(fabs(f.px[e + 1] - (cx * ghalfx + ghalfx)) < 1e-9);
        CHECK(fabs(f.py[e + 1] - (cy * ghalfx + ghoriz)) < 1e-9);
      }
    }
  }

  // initmosts is called, never hooked, so a fingerprint mismatch would crash silently; pins that the thunk's five bytes
  // decode to the body.
  #define P7C_REL32(b) ((unsigned)(b)[1] | ((unsigned)(b)[2] << 8) | ((unsigned)(b)[3] << 16) | ((unsigned)(b)[4] << 24))
  CHECK(thunk_decode_target(rva::THUNK_INITMOSTS_RVA, P7C_REL32(rva::THUNK_INITMOSTS_BYTES)) == rva::INITMOSTS_BODY_RVA);
  #undef P7C_REL32
  CHECK(rva::GCTANG == 0x01773298 && rva::GSTANG == 0x01774320);
  CHECK(rva::WINDOWX2 == 0x018911E0 && rva::WINDOWY2 == 0x01893F50);
  {
    nocull_reset_available();
    const bool installed = true /* both thunks */ && false /* initmosts */ && nocull_available();
    CHECK(!installed);
    CHECK(!mouseaim_gate(true, true, installed, false, false, false, false, false));
    CHECK(strcmp(mouseaim_gate_reason(true, true, installed, false, false, false, false, false), "hooks not installed") == 0);
  }
}

void tb_0092_phase_7_fixd_pause_lurch_and_locavel_clamp() {
  // mouseaim_discard_this_tic(wasActive, nowActive, frameGateClosedSinceLastTic), all eight rows. Either disjunct
  // suffices: nowActive && !wasActive is the reopening tic; the other exists because Duke's pause stops the tic clock
  // (s_active stays true), so the render override sets a latch read once per tic.
  CHECK(!mouseaim_discard_this_tic(false, false, false));   // shut, stays shut, no gate closure seen
  CHECK( mouseaim_discard_this_tic(false, false, true));    // shut, stays shut, but a frame closed the gate
  CHECK( mouseaim_discard_this_tic(false, true,  false));   // reopens
  CHECK( mouseaim_discard_this_tic(false, true,  true));    // reopens and a frame closed the gate
  CHECK(!mouseaim_discard_this_tic(true,  false, false));   // closing: the gate branch resets anyway
  CHECK( mouseaim_discard_this_tic(true,  false, true));    // closing, and a frame closed the gate too
  CHECK(!mouseaim_discard_this_tic(true,  true,  false));   // steady open, no gate closure: convert
  CHECK( mouseaim_discard_this_tic(true,  true,  true));    // steady open but paused since the last tic: discard

  // The -128 guard: locAvel == -128 gave clamp_int(-128,-127,127) - (-128) == +1, a spurious avel; both are clamped to
  // -127..127 (unreachable today since dnOverrideInput clamps, but this is a blind read of live engine memory).
  {
    MouseAimState s; mouseaim_reset(&s);
    MouseAimTic t = mouseaim_tic(&s, 0, 0, 40, 100, -128, 0, 100, false, true);
    CHECK(t.avelAdd == 0);
  }
}

// The engine keeps two copies of the view yaw: our fractional doubles, and the integer cosglobalang/singlobalang
// (16384-scaled) that polymost_drawalls/scansector/drawsprite read after our hook. mouseaim_int_trig fixes the second;
// the fixture uses the same closed form, pinning the convention, not table bytes.
void tb_0093_phase_7_fixe_int_trig_and_pitch_match() {
  {
    static short st[2048];
    for (int i = 0; i < 2048; i++) {
      const double s = 16384.0 * sin((double)i * 2.0 * MOUSEAIM_PI / 2048.0);
      st[i] = (short)(s >= 0.0 ? s + 0.5 : s - 0.5);
    }
    const int angs[7] = { 0, 512, 1024, 1536, 137, 613, 1801 };   // four cardinals, three ordinary
    for (int k = 0; k < 7; k++) {
      const int ang = angs[k];
      long c = 0, s = 0;
      mouseaim_int_trig((double)ang, &c, &s);
      CHECK(labs(c - (long)st[(ang + 512) & 2047]) <= 1);
      CHECK(labs(s - (long)st[ang & 2047]) <= 1);
    }
    long c0 = 0, s0 = 0, c1 = 0, s1 = 0, c2 = 0, s2 = 0;
    mouseaim_int_trig(100.0, &c0, &s0);
    mouseaim_int_trig(100.5, &c1, &s1);
    mouseaim_int_trig(101.0, &c2, &s2);
    CHECK(s0 < s1 && s1 < s2);
    CHECK(c0 > c1 && c1 > c2);
  }
  CHECK(rva::COSGLOBALANG == 0x018D1688 && rva::SINGLOBALANG == 0x01893F24);

  // Build's shear pitch projects at halfxdimen px/radian regardless of viewingrange, so off the neutral 90 FOV equal
  // mouse travel moved yaw and pitch by different screen distances (FOV 120: horizontal much slower).
  // mouseaim_pitch_match corrects it.
  {
    CHECK(fabs(mouseaim_pitch_match(65536) - 1.0) < 1e-9);
    CHECK(fabs(mouseaim_pitch_match(113512) - 0.5774) < 1e-3);
    CHECK(fabs(mouseaim_pitch_match(0) - 4.0) < 1e-9);
    CHECK(fabs(mouseaim_pitch_match(-1) - 4.0) < 1e-9);
    CHECK(fabs(mouseaim_pitch_match(1000000) - 0.25) < 1e-9);
    const double deg1     = mouseaim_pitch_deg_per_count(40, 100, 1.0);
    const double degMatch = mouseaim_pitch_deg_per_count(40, 100, 0.5774);
    CHECK(fabs(degMatch - 0.5774 * deg1) < 1e-6);
  }
}

// The engine keeps a THIRD copy of the view yaw, the short globalang the sky pans by (polymost_drawalls), which the
// override hadn't written, so the sky moved by the integer angle while walls turned with the fractional one.
// mouseaim_globalang_of_view is the fractional angle rounded to nearest and wrapped.
void tb_0094_phase_7_fixf_globalang_of_view() {
  CHECK(mouseaim_globalang_of_view(0.4)    == 0);
  CHECK(mouseaim_globalang_of_view(0.5)    == 1);      // nearest, not truncated
  CHECK(mouseaim_globalang_of_view(2047.6) == 0);      // rounds to 2048, wraps to 0
  CHECK(mouseaim_globalang_of_view(-0.4)   == 0);
  CHECK(mouseaim_globalang_of_view(-0.6)   == 2047);   // rounds to -1, wraps to 2047
  CHECK(mouseaim_globalang_of_view(1023.5) == 1024);
  CHECK(mouseaim_globalang_of_view(2047.4) == 2047);

  {
    bool identity = true;
    for (int a = 0; a < 2048; a++) if (mouseaim_globalang_of_view((double)a) != a) identity = false;
    CHECK(identity);
  }

  {
    bool within = true;
    for (int i = 0; i <= 20700; i++) {
      const double a = -10.0 + (double)i * 0.1;
      const double r = (double)mouseaim_globalang_of_view(a);
      double am = fmod(a, 2048.0);
      if (am < 0.0) am += 2048.0;
      double d = fabs(r - am);
      if (2048.0 - d < d) d = 2048.0 - d;
      if (d > 0.5 + 1e-9) within = false;
    }
    CHECK(within);
  }

  {
    const double bound = 16384.0 * sin(MOUSEAIM_PI / 2048.0) + 1.0;
    bool close = true;
    for (int i = 0; i <= 20700; i++) {
      const double a = -10.0 + (double)i * 0.1;
      const int r = mouseaim_globalang_of_view(a);
      long ca = 0, sa = 0, cr = 0, sr = 0;
      mouseaim_int_trig(a, &ca, &sa);
      mouseaim_int_trig((double)r, &cr, &sr);
      if ((double)labs(ca - cr) > bound || (double)labs(sa - sr) > bound) close = false;
    }
    CHECK(close);
  }

  // The store is two bytes at rva::GLOBALANG; numwalls sits two bytes before and sprite[] two after, so a wider store
  // or a transposed digit would hit a live global rather than unmapped memory.
  CHECK(rva::GLOBALANG == 0x019D4B5Eu);
  CHECK(rva::NUMWALLS + 2 == rva::GLOBALANG && rva::GLOBALANG + 2 == rva::SPRITE);
  CHECK(sizeof(short) == 2);
}

// The mirror reflection slid inside its frame. preparemirror computes `tang = (getangle(dx, dy) * 2 - ang) & 2047` and
// drawrooms runs a second pass with it; mouseaim_mirror_view flips the yaw lead's sign about the mirror wall
// (2*angInterp - A) and passes horiz through unmirrored, matching preparemirror.
void tb_0095_phase_7_fixg_mirror_view() {
  {
    MouseAimView v; v.ang = 103.25; v.horiz = 112.5;
    const MouseAimView m = mouseaim_mirror_view(v, 100);
    CHECK(fabs(m.ang - 96.75) < 1e-12 && fabs(m.horiz - 112.5) < 1e-12);
  }
  {
    MouseAimView v; v.ang = 2047.5; v.horiz = 100.0;
    const MouseAimView m = mouseaim_mirror_view(v, 2047);
    CHECK(fabs(m.ang - 2046.5) < 1e-12 && fabs(m.horiz - 100.0) < 1e-12);
  }
  {
    MouseAimView v; v.ang = 497.0; v.horiz = 87.25;
    const MouseAimView m = mouseaim_mirror_view(v, 500);
    CHECK(fabs(m.ang - 503.0) < 1e-12 && fabs(m.horiz - 87.25) < 1e-12);
  }

  {
    const int angs[3] = { 0, 1024, 2047 };
    bool identity = true;
    for (int i = 0; i < 3; i++) {
      MouseAimView v; v.ang = (double)angs[i]; v.horiz = 100.0;
      const MouseAimView m = mouseaim_mirror_view(v, angs[i]);
      if (fabs(m.ang - (double)angs[i]) > 1e-12 || fabs(m.horiz - 100.0) > 1e-12) identity = false;
    }
    CHECK(identity);
  }

  {
    const int angs[4] = { 0, 300, 1024, 2047 };
    const double leads[4] = { -3.5, -0.25, 0.75, 2.0 };
    bool involution = true, sameSize = true;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        MouseAimView v; v.ang = (double)angs[i] + leads[j]; v.horiz = 100.0 + (double)j;
        const MouseAimView m = mouseaim_mirror_view(v, angs[i]);
        const MouseAimView b = mouseaim_mirror_view(m, angs[i]);
        if (fabs(b.ang - v.ang) > 1e-12 || fabs(b.horiz - v.horiz) > 1e-12) involution = false;
        if (fabs(fabs(m.ang - (double)angs[i]) - fabs(v.ang - (double)angs[i])) > 1e-12) sameSize = false;
      }
    }
    CHECK(involution);
    CHECK(sameSize);
  }

  {
    const double horizs[3] = { 12.5, 100.0, 287.75 };
    bool kept = true;
    for (int i = 0; i < 3; i++) {
      MouseAimView v; v.ang = 640.125; v.horiz = horizs[i];
      const MouseAimView m = mouseaim_mirror_view(v, 637);
      if (m.horiz != horizs[i]) kept = false;
    }
    CHECK(kept);
  }
}
