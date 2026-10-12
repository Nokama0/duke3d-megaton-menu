#include "test_common.h"

#include "test_blocks.h"



// Split from one test main(). To add a check: add a tb_ function to the right area file, declare it in test_blocks.h,
// call it from main() after its dependency.





// A config line longer than CONFIG_LINE_BUFSIZE is drained, not parsed as a truncated (and wrong) value: g_cfg.fov
// stays 90.
void tb_0006_overlong_lines_skipped_partially_parsed() {
    const char* key_prefix = "fov=";
    const char* overflow_value = "71";
    int padding = CONFIG_LINE_BUFSIZE - 1 - (int)strlen(key_prefix);

    fopen_s(&f, tmp, "w");
    fputs("[nukepanel]\n", f);
    fputs(key_prefix, f);
    for (int i = 0; i < padding; i++) fputc('x', f);
    fputs(overflow_value, f);
    fputs("\n", f);
    fclose(f);
    config_load(tmp);
    CHECK(g_cfg.fov == 90);
  }



// Hit-testing is inclusive of the top-left edge, exclusive of bottom-right.
void tb_0015_hit_testing_inclusive_top_left() {
r = { 10, 20, 100, 30 };
  CHECK(ui_hit(r, 10, 20));
  CHECK(ui_hit(r, 109, 49));
  CHECK(!ui_hit(r, 9, 20));
  CHECK(!ui_hit(r, 110, 50));

  cur = { 0, 0, 200, 500 };
  a = ui_row(cur, 30, 4);
  CHECK(a.x == 0 && a.y == 0 && a.w == 200 && a.h == 30);
  b = ui_row(cur, 30, 4);
  CHECK(b.y == 34);
  CHECK(cur.y == 68 && cur.h == 500 - 68);

  UiRect track = { 100, 0, 200, 8 };
  CHECK(ui_slider_value(track, 100, 70, 140) == 70);
  CHECK(ui_slider_value(track, 300, 70, 140) == 140);
  CHECK(ui_slider_value(track, 200, 70, 140) == 105);
  CHECK(ui_slider_value(track, 0,    70, 140) == 70);
  CHECK(ui_slider_value(track, 9999, 70, 140) == 140);
  CHECK(ui_slider_x(track, 70,  70, 140) == 100);
  CHECK(ui_slider_x(track, 140, 70, 140) == 300);

  // Press edge fires only on the up->down transition, so a held button cannot re-trigger a control every frame.
  CHECK(ui_press_edge(true,  false) == true);
  CHECK(ui_press_edge(true,  true)  == false);
  CHECK(ui_press_edge(false, true)  == false);
  CHECK(ui_press_edge(false, false) == false);

  // Initial key press triggers a one-shot action; OS/SDL auto-repeat must not.
  CHECK(ui_key_is_initial_press(false) == true);   // initial press: triggers
  CHECK(ui_key_is_initial_press(true)  == false);  // auto-repeat: does not

  // Save-and-restore toggle (SDL relative-mouse mode, cursor visibility): forced to openValue while the panel owns
  // input, restores the observed savedValue otherwise.
  CHECK(ui_saved_toggle_value(true,  0, 1) == 0);   // panel open: forced to openValue regardless of saved
  CHECK(ui_saved_toggle_value(true,  0, 0) == 0);   // already at openValue: stays there
  CHECK(ui_saved_toggle_value(false, 0, 1) == 1);   // panel closed: restores the saved value
  CHECK(ui_saved_toggle_value(false, 0, 0) == 0);   // restores saved even when it equals openValue
  CHECK(ui_saved_toggle_value(true,  1, 0) == 1);   // symmetric for an openValue of 1 (e.g. show cursor)
  CHECK(ui_saved_toggle_value(false, 1, 0) == 0);
}



// view_freeze_action / settling_tics_after_call: pure clock-phase logic for the settling window (input.cpp cannot link
// into test_host). VFA_FULL and VFA_SETTLING both force all frozen fields; the freeze must never engage once the window
// expired (VFA_NONE) until the panel opens again.
void tb_0025_view_freeze_action_settling_tics_after_call_pure_clock_phase() {
    CHECK(view_freeze_action(true, false, 0) == VFA_FULL);
    CHECK(view_freeze_action(true, false, 2) == VFA_FULL);
    CHECK(view_freeze_action(true, false, 1) == VFA_FULL);

    CHECK(view_freeze_action(false, false, 2) == VFA_SETTLING);
    CHECK(view_freeze_action(false, false, 1) == VFA_SETTLING);
    CHECK(view_freeze_action(false, false, 2) != VFA_FULL);
    CHECK(view_freeze_action(false, false, 1) != VFA_FULL);

    CHECK(view_freeze_action(false, false, 0) == VFA_NONE);
    CHECK(view_freeze_action(false, false, -1) == VFA_NONE);
    CHECK(view_freeze_action(false, false, 0) != VFA_FULL);
    CHECK(view_freeze_action(false, false, 0) != VFA_SETTLING);

    int remaining = 4;
    remaining = settling_tics_after_call(false, remaining);
    CHECK(remaining == 3);
    remaining = settling_tics_after_call(false, remaining);
    CHECK(remaining == 2);
    remaining = settling_tics_after_call(false, remaining);
    CHECK(remaining == 1);
    remaining = settling_tics_after_call(false, remaining);
    CHECK(remaining == 0);
    remaining = settling_tics_after_call(false, remaining);
    CHECK(remaining == 0);   // stays expired, does not go negative
    CHECK(settling_tics_after_call(true, 4) == 4);   // untouched while panel is open

    // Full window walked as hooked_processinput does (decide, then advance, once per tic); the real
    // SETTLING_WINDOW_TICS is 2.
    const int realSettlingWindowTics = 2;
    int r = realSettlingWindowTics;
    int settlingCalls = 0;
    for (int i = 0; i < 5; i++) {
      const ViewFreezeAction a = view_freeze_action(false, false, r);
      CHECK(a != VFA_FULL);   // never "restore horiz via the open path" while closed
      if (a == VFA_SETTLING) settlingCalls++;
      r = settling_tics_after_call(false, r);
    }
    CHECK(settlingCalls == realSettlingWindowTics);
    CHECK(view_freeze_action(false, false, r) == VFA_NONE);
  }



// summarize_samples: pure min/max/distinct-count logic behind the horiz render-clock sampling diagnostic.
void tb_0026_summarize_samples_pure_min_max_distinct() {
    const long allSame[5] = { 76, 76, 76, 76, 76 };
    SampleSummary s = summarize_samples(allSame, 5);
    CHECK(s.minVal == 76);
    CHECK(s.maxVal == 76);
    CHECK(s.distinctCount == 1);

    const long oneWobble[5] = { 76, 76, 75, 76, 76 };
    s = summarize_samples(oneWobble, 5);
    CHECK(s.minVal == 75);
    CHECK(s.maxVal == 76);
    CHECK(s.distinctCount == 2);

    // Every sample different: distinctCount is a genuine uniqueness count, not (max - min + 1), which is wrong once a
    // value repeats.
    const long allDifferent[5] = { 10, 50, 30, 90, 20 };
    s = summarize_samples(allDifferent, 5);
    CHECK(s.minVal == 10);
    CHECK(s.maxVal == 90);
    CHECK(s.distinctCount == 5);

    const long outOfOrder[4] = { 5, -3, 5, 8 };
    s = summarize_samples(outOfOrder, 4);
    CHECK(s.minVal == -3);
    CHECK(s.maxVal == 8);
    CHECK(s.distinctCount == 3);

    const long single[1] = { -42 };
    s = summarize_samples(single, 1);
    CHECK(s.minVal == -42);
    CHECK(s.maxVal == -42);
    CHECK(s.distinctCount == 1);

    s = summarize_samples(allSame, 0);
    CHECK(s.distinctCount == 0);
  }



// build_mod_paths (dllmain's path-building logic): a normal directory builds both paths untruncated.
void tb_0027_build_mod_paths_dllmain_s_path_building() {
    char ini[260], logp[260];
    bool ok = build_mod_paths(tb_path("C:|Games|Duke3D|"), ini, sizeof(ini), logp, sizeof(logp));
    CHECK(ok);
    CHECK(!strcmp(ini, tb_path("C:|Games|Duke3D|duke3dmod.ini")));
    CHECK(!strcmp(logp, tb_path("C:|Games|Duke3D|duke3dmod.log")));
  }



// An over-long directory (deeply nested Steam library) must truncate safely rather than abort via the CRT
// invalid-parameter handler, as a plain sprintf_s overflow would.
void tb_0028_over_long_directory_deeply_nested() {
    char longdir[1024];
    memset(longdir, 'a', sizeof(longdir) - 2);
    longdir[sizeof(longdir) - 2] = '\\';
    longdir[sizeof(longdir) - 1] = 0;  // ~1022 'a's + trailing backslash

    char ini[260], logp[260];
    bool ok = build_mod_paths(longdir, ini, sizeof(ini), logp, sizeof(logp));
    CHECK(!ok);                          // truncation reported, not a crash
    CHECK(strlen(ini)  < sizeof(ini));   // still a valid, null-terminated string
    CHECK(strlen(logp) < sizeof(logp));
  }



// Empty directory (the "GetModuleFileNameA gave us nothing usable" fallback) still produces a plain, usable filename.
void tb_0029_empty_directory_getmodulefilenamea_gave_us() {
    char ini[260], logp[260];
    bool ok = build_mod_paths("", ini, sizeof(ini), logp, sizeof(logp));
    CHECK(ok);
    CHECK(!strcmp(ini,  "duke3dmod.ini"));
    CHECK(!strcmp(logp, "duke3dmod.log"));
  }



// The atlas packer: fontgen.cpp rasterizes glyphs into a texture at run time, laid out at the applied scale; this
// layout half is pure and pinned here since fontgen.cpp cannot link into test_host.
void tb_0031_phase_5b_task_8_atlas() {
    PackBox boxes[288];
    for (int i = 0; i < 288; i++) {
      boxes[i].w = (i % 7 == 0) ? 0 : 4 + (i * 13) % 37;   // some empty, up to 40 wide
      boxes[i].h = (i % 7 == 0) ? 0 : 6 + (i * 7) % 41;    // up to 46 tall
    }
    PackPos pos[288];
    CHECK(fontpack_shelf(boxes, 288, 1024, 1024, 1, pos));
    // inside bounds with the pad from every edge; no two overlap (a pad-wide gutter counts as overlap).
    for (int i = 0; i < 288; i++) {
      if (boxes[i].w == 0 || boxes[i].h == 0) { CHECK(pos[i].x == 0 && pos[i].y == 0); continue; }
      CHECK(pos[i].x >= 1 && pos[i].y >= 1);
      CHECK(pos[i].x + boxes[i].w + 1 <= 1024 && pos[i].y + boxes[i].h + 1 <= 1024);
      for (int j = i + 1; j < 288; j++) {
        if (boxes[j].w == 0 || boxes[j].h == 0) continue;
        const bool apart = pos[i].x + boxes[i].w + 1 <= pos[j].x || pos[j].x + boxes[j].w + 1 <= pos[i].x ||
                           pos[i].y + boxes[i].h + 1 <= pos[j].y || pos[j].y + boxes[j].h + 1 <= pos[i].y;
        CHECK(apart);
      }
    }
    CHECK(pos[1].x == 1 && pos[1].y == 1);
    CHECK(pos[2].y == 1 && pos[2].x == 1 + boxes[1].w + 1);
    // the ladder picks the FIRST rung that fits: these 288 boxes need more than 512x256 but fit 512x512 or
    // 1024x512.
    PackBox chosen = {0, 0};
    const int rung = fontpack_choose(boxes, 288, 1, pos, &chosen);
    CHECK(rung >= 1 && rung <= 2);
    CHECK(chosen.w == FONTPACK_LADDER[rung].w && chosen.h == FONTPACK_LADDER[rung].h);
    PackPos posPrev[288];
    if (rung > 0) CHECK(!fontpack_shelf(boxes, 288, FONTPACK_LADDER[rung - 1].w, FONTPACK_LADDER[rung - 1].h, 1, posPrev));
    PackBox huge[1] = { { 2050, 10 } };
    PackPos hp[1];
    CHECK(!fontpack_shelf(huge, 1, 2048, 2048, 1, hp));
    CHECK(fontpack_choose(huge, 1, 1, hp, &chosen) == -1);
    // one box exactly filling a rung (minus the pad) fits; one pixel more does not.
    PackBox exact[1] = { { 510, 254 } };
    CHECK(fontpack_shelf(exact, 1, 512, 256, 1, hp));
    PackBox over[1] = { { 511, 254 } };
    CHECK(!fontpack_shelf(over, 1, 512, 256, 1, hp));
    PackPos untouched[2] = { { 7, 9 }, { 11, 13 } };
    CHECK(fontpack_shelf(boxes, 0, 512, 256, 1, untouched));
    CHECK(untouched[0].x == 7 && untouched[0].y == 9);
    CHECK(untouched[1].x == 11 && untouched[1].y == 13);
    CHECK(!fontpack_shelf(nullptr, 4, 512, 256, 1, untouched));
  }



void tb_0049_what_panel_actually_draws() {

    // Pure predicates pin the decision; this pins that menu_draw acts on it. canvas.cpp's NO_GL bodies record every
    // draw call, so a wrong heading, unindented row or reverted count is visible here, not just in the logic.
    {
      const int savedTab = menu_active_tab();
      const VisState saved2 = vis_snapshot();
      const int savedFovOn2 = g_cfg.fovon;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

      // Every enemy sub-toggle on, parent OFF: the fresh-install state the greying rules are about.
      const VisState vOff = { 0, 1, 1, 1, 1, 0, 0, 6144 };
      vis_restore(vOff);
      g_cfg.fovon = 0;
      menu_set_tab(TAB_VISUALS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());   // truncation would make every
      CHECK(canvas_record_count() > 0);     // "not drawn" check vacuous

      // Placeholder gone, guarded against vacuity: also confirm a string that IS on this tab is found.
      CHECK(canvas_record_find_text("ESP arrives in phase 2.") == nullptr);
      CHECK(canvas_record_find_text("Show enemies") != nullptr);

      const CanvasOp* lShow    = canvas_record_find_text("Show enemies");
      const CanvasOp* lBoxes   = canvas_record_find_text("Bounding boxes");
      const CanvasOp* lHealth  = canvas_record_find_text("Health bars");
      const CanvasOp* lNames   = canvas_record_find_text("Name and health");
      const CanvasOp* lDist    = canvas_record_find_text("Distance");
      const CanvasOp* lItems   = canvas_record_find_text("Items and pickups");
      const CanvasOp* lSecrets = canvas_record_find_text("Secret areas");
      const CanvasOp* lRange   = canvas_record_find_text("Draw distance");
      CHECK(lShow && lBoxes && lHealth && lNames && lDist && lItems && lSecrets && lRange);

      const CanvasOp* gEnemies = canvas_record_find_text("ENEMIES");
      const CanvasOp* gWorld   = canvas_record_find_text("WORLD");
      const CanvasOp* gRange   = canvas_record_find_text("RANGE");
      CHECK(gEnemies && gWorld && gRange);
      if (gEnemies && gWorld && gRange) {
        CHECK(gEnemies->colour == C_BONEFNT);
        CHECK(gWorld->colour   == C_BONEFNT);
        CHECK(gRange->colour   == C_BONEFNT);
        CHECK(gEnemies->font == FONT_SMALL);
        CHECK(gWorld->font   == FONT_SMALL);
        CHECK(gRange->font   == FONT_SMALL);
        CHECK(gEnemies->colour != C_BONE);   // heading dimmer than its rows
      }

      if (gEnemies && gWorld && gRange && lShow && lBoxes && lHealth && lNames
          && lDist && lItems && lSecrets && lRange) {
        CHECK(gEnemies->y < lShow->y);
        CHECK(lShow->y    < lBoxes->y);
        CHECK(lBoxes->y   < lHealth->y);
        CHECK(lHealth->y  < lNames->y);
        CHECK(lNames->y   < lDist->y);
        CHECK(lDist->y    < gWorld->y);
        CHECK(gWorld->y   < lItems->y);
        CHECK(lItems->y   < lSecrets->y);
        CHECK(lSecrets->y < gRange->y);
        CHECK(gRange->y   < lRange->y);
        const UiRect pr = menu_panel_rect(3440, 1440);
        CHECK(lRange->y >= pr.y && lRange->y < pr.y + pr.h);
        CHECK(gEnemies->y >= pr.y);
      }

      if (lShow && lBoxes && lHealth && lNames && lDist && lItems && lSecrets && lRange) {
        CHECK(lBoxes->x > lShow->x);
        CHECK(lBoxes->x == lHealth->x);
        CHECK(lHealth->x == lNames->x);
        CHECK(lNames->x == lDist->x);
        CHECK(lShow->x == lItems->x);
        CHECK(lItems->x == lSecrets->x);
        CHECK(lSecrets->x == lRange->x);
      }

      if (lShow && lBoxes && lHealth && lNames && lDist) {
        CHECK(lShow->colour   == C_BONE);      // the live parent
        CHECK(lBoxes->colour  == C_BONEFNT);   // greyed out
        CHECK(lHealth->colour == C_BONEFNT);
        CHECK(lNames->colour  == C_BONEFNT);
        CHECK(lDist->colour   == C_BONEFNT);
      }
      // On-but-greyed keeps its knob right and drops its bevel to neutral steel (C_RAISED fill): neither live nor off.
      CHECK(canvas_record_find_kind(CANVAS_OP_BEVEL, C_RAISED) != nullptr);

      CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_HAZARD) != nullptr);
      CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_RED) == nullptr);
      CHECK(canvas_record_find_text("Idle") != nullptr);

      // Any ESP category on, alone, turns the strip red and is counted: exposes one category missing from the sum.
      {
        const VisState only[3] = { { 1, 1, 1, 1, 1, 0, 0, 6144 },
                                   { 0, 1, 1, 1, 1, 1, 0, 6144 },
                                   { 0, 1, 1, 1, 1, 0, 1, 6144 } };
        for (int k = 0; k < 3; k++) {
          vis_restore(only[k]);
          g_cfg.fovon = 0;
          menu_set_tab(TAB_VISUALS);
          canvas_record_begin();
          menu_draw(3440, 1440);
          canvas_record_end();
          CHECK(!canvas_record_overflowed());
          CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_RED) != nullptr);
          CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_HAZARD) == nullptr);
          CHECK(canvas_record_find_text("1 cheat live") != nullptr);
          CHECK(canvas_record_find_text("Idle") == nullptr);
        }
      }
      {
        const VisState two = { 0, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(two);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("2 cheats live") != nullptr);
        CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_RED) != nullptr);
      }
      {
        const VisState all = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(all);
        g_cfg.fovon = 1;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("4 cheats live") != nullptr);
      }
      {
        const VisState esp3 = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(esp3);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("3 cheats live") != nullptr);
        CHECK(canvas_record_find_text("1 cheat live") == nullptr);
      }
      // The strip reports whole-panel state on every tab: Combat must not make live ESP look idle.
      {
        const VisState esp1 = { 1, 1, 1, 1, 1, 0, 0, 6144 };
        vis_restore(esp1);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_COMBAT);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_kind(CANVAS_OP_HAZARD, C_RED) != nullptr);
        CHECK(canvas_record_find_text("1 cheat live") != nullptr);
        CHECK(canvas_record_find_text("AIMBOT") != nullptr);
        CHECK(canvas_record_find_text("Show enemies") == nullptr);
      }

      {
        const VisState on = { 1, 1, 1, 1, 1, 0, 0, 6144 };
        vis_restore(on);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        const CanvasOp* s2 = canvas_record_find_text("Show enemies");
        const CanvasOp* b2 = canvas_record_find_text("Bounding boxes");
        CHECK(s2 && b2);
        if (s2 && b2) {
          CHECK(s2->colour == C_BONE);
          CHECK(b2->colour == C_BONEDIM);      // live, and clearly a sub-row
          CHECK(b2->colour != C_BONEFNT);      // ... not greyed out
          CHECK(b2->colour != s2->colour);
        }
        CHECK(canvas_record_find_kind(CANVAS_OP_BEVEL, C_RED) != nullptr);
        CHECK(canvas_record_find_kind(CANVAS_OP_BEVEL, C_RED) != nullptr);
      }
      {
        const VisState w = { 0, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(w);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_kind(CANVAS_OP_BEVEL, C_BLUE) != nullptr);
        CHECK(canvas_record_find_kind(CANVAS_OP_BEVEL, C_HAZARD) != nullptr);
        CHECK(canvas_esp_colour(false) == C_BLUE);
        CHECK(canvas_esp_secret_colour() == C_HAZARD);
        CHECK(canvas_esp_colour(true) == C_RED);
      }
      {
        VisState d = { 0, 1, 1, 1, 1, 0, 0, 6144 };
        vis_restore(d);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("12 m") != nullptr);   // 6144 / 512
        CHECK(canvas_record_find_text("2 m") == nullptr);
        d.drawdist = 1024;
        vis_restore(d);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("2 m") != nullptr);    // 1024 / 512
        CHECK(canvas_record_find_text("12 m") == nullptr);
      }

      // Every row's label binds to the field it actually drives, via vis_row_click_mask (vis_sweep unions a bitmask
      // over the panel).
      {
        const VisState full = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(full);
        g_cfg.fovon = 0;
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(!canvas_record_overflowed());

        struct { const char* label; int bit; } rows[] = {
          { "Show enemies",       VF_ENEMIES },
          { "Bounding boxes",     VF_BOXES },
          { "Health bars",        VF_HEALTH },
          { "Name and health",    VF_NAMES },
          { "Distance",           VF_DISTANCE },
          { "Items and pickups",  VF_ITEMS },
          { "Secret areas",       VF_SECRETS },
          { "Draw distance",      VF_DRAWDIST },
        };
        for (int i = 0; i < 8; i++) {
          const CanvasOp* op = canvas_record_find_text(rows[i].label);
          CHECK(op != nullptr);
          if (!op) continue;
          const int mask = vis_row_click_mask(op->y, 3440, 1440, full);
          CHECK(mask == rows[i].bit);
        }
      }

      {
        const VisState h = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        vis_restore(h);
        menu_set_tab(TAB_VISUALS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        bool houseStyle = true, sawALabel = false;
        for (int i = 0; i < canvas_record_count(); i++) {
          const CanvasOp* o = canvas_record_op(i);
          if (!o || o->kind != CANVAS_OP_TEXT || o->font != FONT_UI) continue;
          sawALabel = true;
          for (const char* c = o->text; *c; c++) {
            if ((unsigned char)*c > 0x7F) houseStyle = false;   // no em dash (7-bit font)
            if (c != o->text && *c >= 'A' && *c <= 'Z') houseStyle = false;   // sentence case
          }
          if (o->text[0] < 'A' || o->text[0] > 'Z') houseStyle = false;
        }
        CHECK(sawALabel);      // proves houseStyle isn't vacuously true
        CHECK(houseStyle);
      }

      g_ui.mx = 0; g_ui.my = 0;
      g_cfgDirty = false;
      g_cfg.fovon = savedFovOn2;
      vis_restore(saved2);
      menu_set_tab(savedTab);
    }
  }




// silent.cpp cannot link into test_host (patches live executable memory, reads/writes the player struct), so the shoot
// detour's spread-site decision lives in accuracy.h and is driven here over a synthetic code buffer holding the real
// site RVAs.
void tb_0062_phase_4_task_5_perfect() {
    // Pinned against the list silent aim already accepts (target.cpp), so a typo is a failure, not a shot that silently
    // never patches.
    const short AT_PISTOL   = 0x0a23;
    const short AT_CHAINGUN = 0x09e8;
    const short AT_SHOTGUN  = 0x0a35;
    const short AT_EXPANDER = 0x0990;
    CHECK(silent_atwith_is_player_shot(AT_PISTOL));
    CHECK(silent_atwith_is_player_shot(AT_CHAINGUN));
    CHECK(silent_atwith_is_player_shot(AT_SHOTGUN));
    CHECK(silent_atwith_is_player_shot(AT_EXPANDER));
    CHECK(AT_PISTOL != AT_CHAINGUN && AT_PISTOL != AT_SHOTGUN && AT_PISTOL != AT_EXPANDER);
    CHECK(AT_CHAINGUN != AT_SHOTGUN && AT_CHAINGUN != AT_EXPANDER);
    CHECK(AT_SHOTGUN != AT_EXPANDER);

    CHECK(accuracy_pair_sites(ACC_PAIR_PISTOL)   == rva::ACC_PISTOL);
    CHECK(accuracy_pair_sites(ACC_PAIR_HITSCAN)  == rva::ACC_HITSCAN);
    CHECK(accuracy_pair_sites(ACC_PAIR_EXPANDER) == rva::ACC_EXPANDER);
    CHECK(accuracy_pair_sites(ACC_PAIR_NONE) == 0);
    CHECK(accuracy_pair_sites(ACC_PAIR_COUNT) == 0);
    CHECK(accuracy_pair_sites(-99) == 0);
    CHECK(accuracy_pair_sites(99) == 0);
    CHECK(ACC_PAIR_SITES == 2);
    CHECK(ACC_PAIR_PISTOL == 0 && ACC_PAIR_HITSCAN == 1 && ACC_PAIR_EXPANDER == 2);
    CHECK(ACC_PAIR_COUNT == 3);
    CHECK(ACC_PAIR_NONE < 0);

    {
      const rva::AccSite* s = (const rva::AccSite*)1;   // poisoned, not null
      CHECK(accuracy_sites_for(AT_PISTOL,   false, false, false, false, &s) == 0);
      CHECK(s == 0);
      s = (const rva::AccSite*)1;
      CHECK(accuracy_sites_for(AT_CHAINGUN, false, false, false, false, &s) == 0);
      CHECK(s == 0);
      s = (const rva::AccSite*)1;
      CHECK(accuracy_sites_for(AT_SHOTGUN,  false, false, false, false, &s) == 0);
      CHECK(s == 0);
      s = (const rva::AccSite*)1;
      CHECK(accuracy_sites_for(AT_EXPANDER, false, false, false, false, &s) == 0);
      CHECK(s == 0);
    }

    {
      const rva::AccSite* s = 0;
      CHECK(accuracy_sites_for(AT_PISTOL, true, false, false, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_PISTOL);
      CHECK(accuracy_sites_for(AT_CHAINGUN, false, true, false, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_SHOTGUN, false, false, true, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_EXPANDER, false, false, false, true, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_EXPANDER);
    }

    // Chaingun and shotgun share ACC_HITSCAN (one branch in shoot), but the pair is chosen from the shot's own atwith,
    // not the site list: chaingun on with shotgun off patches for 0x9e8 and not 0xa35, same five bytes.
    {
      const rva::AccSite* s = 0;
      CHECK(accuracy_sites_for(AT_CHAINGUN, false, true, false, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      s = (const rva::AccSite*)1;
      CHECK(accuracy_sites_for(AT_SHOTGUN, false, true, false, false, &s) == 0);
      CHECK(s == 0);
      CHECK(accuracy_sites_for(AT_SHOTGUN, false, false, true, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      s = (const rva::AccSite*)1;
      CHECK(accuracy_sites_for(AT_CHAINGUN, false, false, true, false, &s) == 0);
      CHECK(s == 0);
      // Both on is still ONE pair, never a union: patch_bracket_arm refuses a list holding the hitscan pair twice (it
      // would rewrite ten bytes of the game's code for the life of the process).
      CHECK(accuracy_sites_for(AT_CHAINGUN, false, true, true, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_SHOTGUN, false, true, true, false, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_PISTOL, true, true, true, true, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_PISTOL);
      CHECK(accuracy_sites_for(AT_CHAINGUN, true, true, true, true, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_SHOTGUN, true, true, true, true, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_HITSCAN);
      CHECK(accuracy_sites_for(AT_EXPANDER, true, true, true, true, &s) == ACC_PAIR_SITES);
      CHECK(s == rva::ACC_EXPANDER);
    }

    // Sixteen toggle combinations by four weapons, as the rule rather than sixty-four literals: each shot is patched
    // exactly when its own toggle is on.
    {
      bool ok = true, sawPatched = false, sawRefused = false;
      for (int m = 0; m < 16; m++) {
        const bool p  = (m & 1) != 0, c  = (m & 2) != 0;
        const bool sh = (m & 4) != 0, ex = (m & 8) != 0;
        const short at[4]  = { AT_PISTOL, AT_CHAINGUN, AT_SHOTGUN, AT_EXPANDER };
        const bool  on[4]  = { p, c, sh, ex };
        const rva::AccSite* want[4] = { rva::ACC_PISTOL, rva::ACC_HITSCAN,
                                        rva::ACC_HITSCAN, rva::ACC_EXPANDER };
        const int   pair[4] = { ACC_PAIR_PISTOL, ACC_PAIR_HITSCAN,
                                ACC_PAIR_HITSCAN, ACC_PAIR_EXPANDER };
        for (int w = 0; w < 4; w++) {
          const rva::AccSite* got = (const rva::AccSite*)1;
          const int n = accuracy_sites_for(at[w], p, c, sh, ex, &got);
          const int pr = accuracy_pair_for(at[w], p, c, sh, ex);
          if (on[w]) {
            if (n != ACC_PAIR_SITES || got != want[w] || pr != pair[w]) ok = false;
            sawPatched = true;
          } else {
            if (n != 0 || got != 0 || pr != ACC_PAIR_NONE) ok = false;
            sawRefused = true;
          }
        }
      }
      CHECK(ok);
      CHECK(sawPatched);
      CHECK(sawRefused);
    }

    // Somebody else's shot is never patched: 0x664 SPIT is an enemy weapon; 0x8f8..0x8fb are blood hitscans
    // checkhitsprite fires back into shoot (can hit the player's sprite); the rest have no spread sites (RPG/devastator
    // 0xa2d, freezer 0x669, shrinker 0x9fc, tripbomb 0xa03, mighty foot 0x9d9).
    {
      const short others[] = { 0x0664, 0x0550, 0x0659, 0x08f8, 0x08f9, 0x08fa, 0x08fb,
                               0x0a2d, 0x0669, 0x09fc, 0x0a03, 0x09d9, 0 };
      bool ok = true;
      for (int m = 0; m < 16; m++) {
        const bool p  = (m & 1) != 0, c  = (m & 2) != 0;
        const bool sh = (m & 4) != 0, ex = (m & 8) != 0;
        for (int i = 0; i < (int)(sizeof(others) / sizeof(others[0])); i++) {
          const rva::AccSite* got = (const rva::AccSite*)1;
          if (accuracy_sites_for(others[i], p, c, sh, ex, &got) != 0) ok = false;
          if (got != 0) ok = false;
          if (accuracy_pair_for(others[i], p, c, sh, ex) != ACC_PAIR_NONE) ok = false;
        }
      }
      CHECK(ok);
    }

    // Whole short range, everything on: exactly four values select a pair; a fifth would be a shot patched unasked.
    {
      int patched = 0;
      bool ok = true;
      for (int a = -32768; a <= 32767; a++) {
        const short at = (short)a;
        const rva::AccSite* got = (const rva::AccSite*)1;
        const int n = accuracy_sites_for(at, true, true, true, true, &got);
        if (n != 0 && n != ACC_PAIR_SITES) ok = false;      // never a union, never one site
        if (n == 0) { if (got != 0) ok = false; continue; }
        patched++;
        if (at != AT_PISTOL && at != AT_CHAINGUN && at != AT_SHOTGUN && at != AT_EXPANDER)
          ok = false;
        if (got != rva::ACC_PISTOL && got != rva::ACC_HITSCAN && got != rva::ACC_EXPANDER)
          ok = false;
      }
      CHECK(ok);
      CHECK(patched == 4);
    }

    {
      CHECK(accuracy_sites_for(AT_PISTOL, true, false, false, false, 0) == ACC_PAIR_SITES);
      CHECK(accuracy_sites_for(AT_PISTOL, false, false, false, false, 0) == 0);
      CHECK(accuracy_sites_for(0x0664, true, true, true, true, 0) == 0);
    }

    // Whatever it returns, the bracket can always accept: patch_bracket_arm refuses an overlapping list, which would be
    // a feature that silently never armed. Every list is one pair and no pair self-overlaps.
    {
      bool ok = true;
      const short at[4] = { AT_PISTOL, AT_CHAINGUN, AT_SHOTGUN, AT_EXPANDER };
      for (int w = 0; w < 4; w++) {
        const rva::AccSite* got = 0;
        const int n = accuracy_sites_for(at[w], true, true, true, true, &got);
        if (n < 1 || n > ACC_MAX_SITES) ok = false;
        if (!got || acc_sites_overlap(got, n)) ok = false;
        if (got && (got[0].centre != 0x10 || got[1].centre != 0x80)) ok = false;
      }
      CHECK(ok);
    }

    // Per-pair session latch. accuracy_should_arm's first argument is isPlayerShot; every check here passes true to
    // test the latch/pair half alone (the shooter half follows).
    {
      accuracy_latch_reset_all();
      CHECK(!accuracy_pair_latched(ACC_PAIR_PISTOL));
      CHECK(!accuracy_pair_latched(ACC_PAIR_HITSCAN));
      CHECK(!accuracy_pair_latched(ACC_PAIR_EXPANDER));
      CHECK(accuracy_should_arm(ACC_PAIR_PISTOL, true));
      CHECK(accuracy_should_arm(ACC_PAIR_HITSCAN, true));
      CHECK(accuracy_should_arm(ACC_PAIR_EXPANDER, true));
      CHECK(accuracy_pair_latched(ACC_PAIR_NONE));
      CHECK(accuracy_pair_latched(ACC_PAIR_COUNT));
      CHECK(accuracy_pair_latched(-99));
      CHECK(accuracy_pair_latched(99));
      CHECK(!accuracy_should_arm(ACC_PAIR_NONE, true));
      CHECK(!accuracy_should_arm(99, true));

      // One pair only: latching the pistol leaves the other two armable (a fingerprint mismatch on one pair's sites
      // says nothing about another's, on a different page in a different handler).
      accuracy_latch_pair(ACC_PAIR_PISTOL);
      CHECK(accuracy_pair_latched(ACC_PAIR_PISTOL));
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, true));
      CHECK(accuracy_should_arm(ACC_PAIR_HITSCAN, true));
      CHECK(accuracy_should_arm(ACC_PAIR_EXPANDER, true));
      accuracy_latch_pair(ACC_PAIR_PISTOL);
      CHECK(accuracy_pair_latched(ACC_PAIR_PISTOL));
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, true));
      accuracy_latch_pair(-1);
      accuracy_latch_pair(ACC_PAIR_COUNT);
      accuracy_latch_pair(1234);
      CHECK(accuracy_should_arm(ACC_PAIR_HITSCAN, true));
      CHECK(accuracy_should_arm(ACC_PAIR_EXPANDER, true));

      // accuracy_sites_for stays pure (says what this shot would patch); the latch is a separate gate the hook checks
      // before arming.
      {
        const rva::AccSite* got = 0;
        CHECK(accuracy_sites_for(AT_PISTOL, true, false, false, false, &got) == ACC_PAIR_SITES);
        CHECK(got == rva::ACC_PISTOL);
        CHECK(!accuracy_should_arm(accuracy_pair_for(AT_PISTOL, true, false, false, false), true));
      }
    }

    // The shooter gate: accuracy_pair_for cannot tell an enemy's hitscan shot from the player's
    // (LIZTROOP/PIGCOP/ENFORCER reach the same atwith values via shoot's other branch, accuracy.h), so isPlayerShot is
    // the one term between an enemy shot and an armed bracket; it must refuse independently of the latch/pair.
    {
      accuracy_latch_reset_all();
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, false));
      CHECK(!accuracy_should_arm(ACC_PAIR_HITSCAN, false));
      CHECK(!accuracy_should_arm(ACC_PAIR_EXPANDER, false));
      CHECK(accuracy_should_arm(ACC_PAIR_PISTOL, true));
      CHECK(accuracy_should_arm(ACC_PAIR_HITSCAN, true));
      CHECK(accuracy_should_arm(ACC_PAIR_EXPANDER, true));

      {
        const int pistolPair = accuracy_pair_for(AT_PISTOL, true, true, true, true);
        CHECK(pistolPair == ACC_PAIR_PISTOL);   // a real pair, not vacuous
        CHECK(!accuracy_should_arm(pistolPair, /*isPlayerShot=*/false));
        const int hitscanPair = accuracy_pair_for(AT_CHAINGUN, true, true, true, true);
        CHECK(hitscanPair == ACC_PAIR_HITSCAN);
        CHECK(!accuracy_should_arm(hitscanPair, /*isPlayerShot=*/false));
        const int expanderPair = accuracy_pair_for(AT_EXPANDER, true, true, true, true);
        CHECK(expanderPair == ACC_PAIR_EXPANDER);
        CHECK(!accuracy_should_arm(expanderPair, /*isPlayerShot=*/false));
      }

      accuracy_latch_pair(ACC_PAIR_PISTOL);
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, true));    // latched
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, false));   // not the player, and latched
      accuracy_latch_reset_all();
      CHECK(!accuracy_should_arm(ACC_PAIR_PISTOL, false));   // not the player, unlatched
      CHECK(accuracy_should_arm(ACC_PAIR_PISTOL, true));     // the player, unlatched: arms

      accuracy_latch_reset_all();
      CHECK(accuracy_note_arm_result(ACC_PAIR_HITSCAN, true));    // armed: nothing latches
      CHECK(!accuracy_pair_latched(ACC_PAIR_HITSCAN));
      CHECK(!accuracy_note_arm_result(ACC_PAIR_HITSCAN, false));  // refused: latch it
      CHECK(accuracy_pair_latched(ACC_PAIR_HITSCAN));
      CHECK(!accuracy_pair_latched(ACC_PAIR_PISTOL));
      CHECK(!accuracy_pair_latched(ACC_PAIR_EXPANDER));
      accuracy_latch_reset_all();
      CHECK(!accuracy_pair_latched(ACC_PAIR_HITSCAN));
    }

    // The decision driving the real bracket, on a synthetic page-aligned code buffer holding the real site RVAs and the
    // six real call sites (a wrong constant fails here). The site list comes from accuracy_sites_for, as the shoot
    // detour does.
    {
      const unsigned CODE_SIZE = 0x000F6000u;
      unsigned char* code = (unsigned char*)_aligned_malloc(CODE_SIZE, ACC_TEST_PAGE_SIZE);
      CHECK(code != 0);
      if (code) {
        for (unsigned i = 0; i < CODE_SIZE; i++) code[i] = (unsigned char)(i * 7 + 3);
        const rva::AccSite all[6] = {
          rva::ACC_PISTOL[0],   rva::ACC_PISTOL[1],
          rva::ACC_HITSCAN[0],  rva::ACC_HITSCAN[1],
          rva::ACC_EXPANDER[0], rva::ACC_EXPANDER[1],
        };
        for (int i = 0; i < 6; i++) acc_expected_bytes(all[i].rva, code + all[i].rva);
        unsigned char* pristine = (unsigned char*)malloc(CODE_SIZE);
        CHECK(pristine != 0);
        if (pristine) {
          memcpy(pristine, code, CODE_SIZE);
          acc_write_watch_fn() = acc_test_write_watch;
          acc_protect_fn() = acc_test_protect_ok;
          accuracy_latch_reset_all();

          {
            const short at[4] = { AT_PISTOL, AT_CHAINGUN, AT_SHOTGUN, AT_EXPANDER };
            for (int w = 0; w < 4; w++) {
              acc_test_protect_reset();
              const rva::AccSite* sites = 0;
              const int n = accuracy_sites_for(at[w], w == 0, w == 1, w == 2, w == 3, &sites);
              CHECK(n == ACC_PAIR_SITES);
              CHECK(sites != 0);
              if (n && sites) {
                PatchBracket b;
                CHECK(patch_bracket_arm(&b, code, sites, n));
                bool patchedOk = true, othersOk = true;
                for (int k = 0; k < n; k++) {
                  unsigned char want[ACC_SITE_BYTES];
                  acc_patch_bytes(sites[k].centre, want);
                  if (memcmp(code + sites[k].rva, want, ACC_SITE_BYTES) != 0) patchedOk = false;
                }
                for (int k = 0; k < 6; k++) {
                  bool mine = false;
                  for (int q = 0; q < n; q++) if (all[k].rva == sites[q].rva) mine = true;
                  if (mine) continue;
                  if (memcmp(code + all[k].rva, pristine + all[k].rva, ACC_SITE_BYTES) != 0)
                    othersOk = false;
                }
                CHECK(patchedOk);
                CHECK(othersOk);
                CHECK(patch_bracket_release(&b));
              }
              CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
              CHECK(acc_test_write_violations() == 0);
              CHECK(acc_test_pages_writable() == 0);
            }
          }

          // Chaingun on, shotgun off, through the bracket: chaingun's shot patches the shared hitscan pair, shotgun's
          // does not arm, so shotgun keeps its spread despite sharing the same five bytes.
          {
            acc_test_protect_reset();
            const rva::AccSite* sites = 0;
            CHECK(accuracy_sites_for(AT_CHAINGUN, false, true, false, false, &sites)
                  == ACC_PAIR_SITES);
            {
              PatchBracket b;
              CHECK(patch_bracket_arm(&b, code, sites, ACC_PAIR_SITES));
              unsigned char want[ACC_SITE_BYTES];
              acc_patch_bytes(rva::ACC_HITSCAN[0].centre, want);
              CHECK(memcmp(code + rva::ACC_HITSCAN[0].rva, want, ACC_SITE_BYTES) == 0);
              CHECK(patch_bracket_release(&b));
            }
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
            const rva::AccSite* none = (const rva::AccSite*)1;
            CHECK(accuracy_sites_for(AT_SHOTGUN, false, true, false, false, &none) == 0);
            CHECK(none == 0);
            CHECK(acc_site_matches(code, rva::ACC_HITSCAN[0].rva));
            CHECK(acc_site_matches(code, rva::ACC_HITSCAN[1].rva));
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // The destructor is the restore, which makes an early return between arm and the original shoot() safe: this
          // scope arms and leaves without calling release, as such a return would.
          {
            acc_test_protect_reset();
            const rva::AccSite* sites = 0;
            CHECK(accuracy_sites_for(AT_EXPANDER, false, false, false, true, &sites)
                  == ACC_PAIR_SITES);
            {
              PatchBracket b;
              CHECK(patch_bracket_arm(&b, code, sites, ACC_PAIR_SITES));
              CHECK(memcmp(pristine, code, CODE_SIZE) != 0);
            }
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
            CHECK(acc_test_pages_writable() == 0);
            CHECK(acc_test_write_violations() == 0);
          }

          // A union of pairs is refused (accuracy_sites_for never builds one, but a duplicated pair would save the
          // patch as its own original and leave ten bytes of the game's code rewritten permanently).
          {
            acc_test_protect_reset();
            const rva::AccSite uni[4] = {
              rva::ACC_HITSCAN[0], rva::ACC_HITSCAN[1],
              rva::ACC_HITSCAN[0], rva::ACC_HITSCAN[1],
            };
            CHECK(acc_sites_overlap(uni, 4));
            PatchBracket b;
            CHECK(!patch_bracket_arm(&b, code, uni, 4));
            CHECK(!b.armed);
            CHECK(acc_test_write_calls() == 0);
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // Degradation rule, end to end: one byte of the pistol's yaw site is poisoned (as a mismatched binary would
          // present it). The arm refuses, nothing is written, that pair latches off for the session, and the other two
          // pairs keep working.
          {
            acc_test_protect_reset();
            accuracy_latch_reset_all();
            const unsigned poisonRva = rva::ACC_PISTOL[0].rva + 2u;
            const unsigned char saved = code[poisonRva];
            code[poisonRva] = (unsigned char)(saved ^ 0xFFu);
            CHECK(!acc_site_matches(code, rva::ACC_PISTOL[0].rva));

            const rva::AccSite* sites = 0;
            const int pair = accuracy_pair_for(AT_PISTOL, true, false, false, false);
            CHECK(pair == ACC_PAIR_PISTOL);
            CHECK(accuracy_sites_for(AT_PISTOL, true, false, false, false, &sites)
                  == ACC_PAIR_SITES);
            CHECK(accuracy_should_arm(pair, true));
            {
              PatchBracket b;
              const bool armed = patch_bracket_arm(&b, code, sites, ACC_PAIR_SITES);
              CHECK(!armed);
              CHECK(!accuracy_note_arm_result(pair, armed));
            }
            CHECK(acc_test_write_calls() == 0);
            CHECK(acc_test_protect_calls() == 0);
            CHECK(acc_test_pages_writable() == 0);
            // Latched, and NOT retried: the next shot with the same weapon never reaches the arm at all.
            CHECK(accuracy_pair_latched(ACC_PAIR_PISTOL));
            CHECK(!accuracy_should_arm(accuracy_pair_for(AT_PISTOL, true, false, false, false), true));
            CHECK(accuracy_should_arm(accuracy_pair_for(AT_CHAINGUN, false, true, false, false), true));
            CHECK(accuracy_should_arm(accuracy_pair_for(AT_EXPANDER, false, false, false, true), true));
            {
              acc_test_protect_reset();
              const rva::AccSite* hs = 0;
              CHECK(accuracy_sites_for(AT_SHOTGUN, false, false, true, false, &hs)
                    == ACC_PAIR_SITES);
              PatchBracket b;
              CHECK(patch_bracket_arm(&b, code, hs, ACC_PAIR_SITES));
              CHECK(patch_bracket_release(&b));
              CHECK(acc_test_write_violations() == 0);
            }
            code[poisonRva] = saved;
            accuracy_latch_reset_all();
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          }

          // The nested-arm re-entrancy guard: accuracy_may_attempt_arm is the pure decision it reduces to. An unguarded
          // second arm on the same live site fails its fingerprint check and would falsely latch a pair the binary
          // never disagreed with (shown below).
          {
            CHECK(!accuracy_may_attempt_arm(/*alreadyArmed=*/true));
            CHECK( accuracy_may_attempt_arm(/*alreadyArmed=*/false));

            acc_test_protect_reset();
            accuracy_latch_reset_all();
            const rva::AccSite* pistolSites = 0;
            CHECK(accuracy_sites_for(AT_PISTOL, true, false, false, false, &pistolSites)
                  == ACC_PAIR_SITES);

            PatchBracket outer;
            CHECK(patch_bracket_arm(&outer, code, pistolSites, ACC_PAIR_SITES));
            CHECK(outer.armed);
            {
              PatchBracket inner;
              const bool innerArmed = patch_bracket_arm(&inner, code, pistolSites, ACC_PAIR_SITES);
              CHECK(!innerArmed);
              CHECK(!inner.armed);
              CHECK(!accuracy_note_arm_result(ACC_PAIR_PISTOL, innerArmed));
              CHECK(accuracy_pair_latched(ACC_PAIR_PISTOL));
            }
            accuracy_latch_reset_all();

            CHECK(!accuracy_may_attempt_arm(/*outer still armed*/ true));
            CHECK(!accuracy_pair_latched(ACC_PAIR_PISTOL));

            CHECK(patch_bracket_release(&outer));
            CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
            accuracy_latch_reset_all();
          }

          acc_protect_fn() = 0;
          // Nothing this block did ever wrote to a page that was not writable at the time, and the buffer is exactly as
          // it was planted.
          CHECK(acc_test_write_viol_total() == 0);
          CHECK(memcmp(pristine, code, CODE_SIZE) == 0);
          acc_write_watch_fn() = 0;
          free(pristine);
        }
        _aligned_free(code);
      }
      accuracy_latch_reset_all();
    }

    // Config: defaults, round trip, truthy clamp.
    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.accpistol == 0);
      CHECK(g_cfg.accchaingun == 0);
      CHECK(g_cfg.accshotgun == 0);
      CHECK(g_cfg.accexpander == 0);
    }
    {
      const char* t = "test_accuracy_task5.ini";
      config_load(tb_path("Z:|does|not|exist.ini"));
      g_cfg.accpistol = 1;
      g_cfg.accchaingun = 0;
      g_cfg.accshotgun = 1;
      g_cfg.accexpander = 1;
      CHECK(config_save(t));
      // Poisoned, so a reload that did nothing at all would fail rather than pass by leaving the values where they
      // were.
      g_cfg.accpistol = 0;
      g_cfg.accchaingun = 1;
      g_cfg.accshotgun = 0;
      g_cfg.accexpander = 0;
      config_load(t);
      CHECK(g_cfg.accpistol == 1);
      CHECK(g_cfg.accchaingun == 0);
      CHECK(g_cfg.accshotgun == 1);
      CHECK(g_cfg.accexpander == 1);
      remove(t);
    }
    {
      // A hand-edited ini: every switch reads back as exactly 0 or 1, so nothing downstream has to cope with a 7.
      const char* t = "test_accuracy_clamp.ini";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f != nullptr);
      if (f) {
        fputs("[nukepanel]\n", f);
        fputs("accpistol=7\n", f);
        fputs("accchaingun=-3\n", f);
        fputs("accshotgun=0\n", f);
        fputs("accexpander=1\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.accpistol == 1);
        CHECK(g_cfg.accchaingun == 1);
        CHECK(g_cfg.accshotgun == 0);
        CHECK(g_cfg.accexpander == 1);
        remove(t);
      }
      config_load(tb_path("Z:|does|not|exist.ini"));
    }

    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0) == 1);   // pistol alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0) == 1);   // chaingun alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0) == 1);   // shotgun alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0) == 1);   // expander alone
    // Four switches, four counts: four parents, not one category.
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0) == 4);
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0) == 11);
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0) == 7);   // and they really are the last four
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 7, -3, 0, 0, 0, 0, 0) == 2);
    {
      bool agree = true, sawEleven = false;
      for (int fv = 0; fv < 2; fv++)
      for (int en = 0; en < 2; en++)
      for (int it = 0; it < 2; it++)
      for (int se = 0; se < 2; se++)
      for (int ao = 0; ao < 2; ao++)
      for (int so = 0; so < 2; so++)
      for (int fc = 0; fc < 2; fc++)
      for (int ap = 0; ap < 2; ap++)
      for (int ac = 0; ac < 2; ac++)
      for (int as = 0; as < 2; as++)
      for (int ae = 0; ae < 2; ae++) {
        const int n = menu_live_count(fv, en, it, se, 0, 0, ao, so, fc, ap, ac, as, ae, 0, 0, 0);
        if (n != fv + en + it + se + ao + so + fc + ap + ac + as + ae) agree = false;
        if (n == 11) sawEleven = true;
      }
      CHECK(agree);
      CHECK(sawEleven);
    }

    // The Weapons tab's own rows.
    {
      const int savedTab = menu_active_tab();
      const WeapState savedWeap = weap_snapshot();
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

      g_cfg.accpistol = 0; g_cfg.accchaingun = 0;
      g_cfg.accshotgun = 0; g_cfg.accexpander = 0;
      menu_set_tab(TAB_WEAPONS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_count() > 0);

      CHECK(canvas_record_find_text("Perfect accuracy arrives in phase 4.") == nullptr);
      CHECK(canvas_record_find_text("PERFECT ACCURACY") != nullptr);

      const CanvasOp* gAcc  = canvas_record_find_text("PERFECT ACCURACY");
      const CanvasOp* rPis  = canvas_record_find_text("Pistol");
      const CanvasOp* rChn  = canvas_record_find_text("Chaingun");
      const CanvasOp* rSht  = canvas_record_find_text("Shotgun");
      const CanvasOp* rExp  = canvas_record_find_text("Expander");
      CHECK(gAcc && rPis && rChn && rSht && rExp);
      if (gAcc && rPis && rChn && rSht && rExp) {
        CHECK(gAcc->colour == C_BONEFNT);
        CHECK(gAcc->font == FONT_SMALL);
        CHECK(gAcc->y < rPis->y);
        CHECK(rPis->y < rChn->y);
        CHECK(rChn->y < rSht->y);
        CHECK(rSht->y < rExp->y);
        CHECK(rPis->colour == C_BONE);
        CHECK(rChn->colour == C_BONE);
        CHECK(rSht->colour == C_BONE);
        CHECK(rExp->colour == C_BONE);
        CHECK(rPis->x == rChn->x && rChn->x == rSht->x && rSht->x == rExp->x);
      }

      {
        int hazardBevels = 0;
        for (int i = 0; i < canvas_record_count(); i++) {
          const CanvasOp* o = canvas_record_op(i);
          if (o && o->kind == CANVAS_OP_BEVEL && o->colour == C_HAZARD) hazardBevels++;
        }
        CHECK(hazardBevels == 0);
      }
      g_cfg.accpistol = 1; g_cfg.accchaingun = 1;
      g_cfg.accshotgun = 1; g_cfg.accexpander = 1;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      {
        int hazardBevels = 0;
        for (int i = 0; i < canvas_record_count(); i++) {
          const CanvasOp* o = canvas_record_op(i);
          if (o && o->kind == CANVAS_OP_BEVEL && o->colour == C_HAZARD) hazardBevels++;
        }
        CHECK(hazardBevels == 4);
      }
      CHECK(canvas_record_find_text("4 cheats live") != nullptr);

      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_WEAPONS, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_WEAPONS, 1024, 768, 18, &worst));
      }

      // Every row is reachable, no click moves two fields. The Weapons tab also carries MOUSE AIM's own switch (its own
      // WF_ bit); a press-edge sweep (held = false) reaches it alongside the four accuracy switches (the two sliders
      // under it need a held sweep, covered by tb_0086).
      {
        // WF_FIRERATE joins both sets via the reset glyph beside "Fire rate" (a small button acting on the press edge).
        // Its MOUSE AIM neighbours don't: mouseaimon is 0 in both fixtures, so no glyph, and "Weapon switch speed" is
        // no WeapState field. Both fixtures start firerate at 0, not FIRERATE_DEFAULT, so the reset moves it.
        const WeapState off = { 0, 0, 0, 0 };
        bool multi = false;
        const int reached = weap_sweep(3440, 1440, off, false, &multi);
        CHECK(reached == (WF_PISTOL | WF_CHAINGUN | WF_SHOTGUN | WF_EXPANDER | WF_MOUSEAIMON
                          | WF_FIRERATE));
        CHECK(!multi);
        const WeapState on = { 1, 1, 1, 1 };
        bool multi2 = false;
        const int reached2 = weap_sweep(3440, 1440, on, false, &multi2);
        CHECK(reached2 == (WF_PISTOL | WF_CHAINGUN | WF_SHOTGUN | WF_EXPANDER | WF_MOUSEAIMON
                           | WF_FIRERATE));
        CHECK(!multi2);
      }

      // Each label drives its own field: the sweep proves reachability, not that "Shotgun" moves accshotgun (four
      // identical-shape rows differing by name is where a swapped binding hides).
      {
        const WeapState off = { 0, 0, 0, 0 };
        weap_restore(off);
        menu_set_tab(TAB_WEAPONS);
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        const CanvasOp* lp = canvas_record_find_text("Pistol");
        const CanvasOp* lc = canvas_record_find_text("Chaingun");
        const CanvasOp* ls = canvas_record_find_text("Shotgun");
        const CanvasOp* le = canvas_record_find_text("Expander");
        CHECK(lp && lc && ls && le);
        if (lp && lc && ls && le) {
          CHECK(weap_row_click_mask(lp->y, 3440, 1440, off) == WF_PISTOL);
          CHECK(weap_row_click_mask(lc->y, 3440, 1440, off) == WF_CHAINGUN);
          CHECK(weap_row_click_mask(ls->y, 3440, 1440, off) == WF_SHOTGUN);
          CHECK(weap_row_click_mask(le->y, 3440, 1440, off) == WF_EXPANDER);
        }
      }

      weap_restore(savedWeap);
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // accuracy_row_live/accuracy_row_unavailable, then the same through the Weapons tab's real rendering: a latched
    // pair's row gets the "(Unavailable)" suffix, its switch value is untouched (config never sees the latch), and it
    // leaves the live count.
    {
      CHECK( accuracy_row_live(1, false));
      CHECK(!accuracy_row_live(1, true));
      CHECK(!accuracy_row_live(0, false));
      CHECK(!accuracy_row_live(0, true));
      CHECK(!accuracy_row_unavailable(false));
      CHECK( accuracy_row_unavailable(true));

      const int savedTab = menu_active_tab();
      const WeapState savedWeap = weap_snapshot();
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      accuracy_latch_reset_all();
      g_cfg.accpistol = 1; g_cfg.accchaingun = 1;
      g_cfg.accshotgun = 1; g_cfg.accexpander = 1;
      menu_set_tab(TAB_WEAPONS);

      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Pistol") != nullptr);
      CHECK(canvas_record_find_text("Pistol (Unavailable)") == nullptr);
      CHECK(canvas_record_find_text("4 cheats live") != nullptr);

      accuracy_latch_pair(ACC_PAIR_PISTOL);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Pistol (Unavailable)") != nullptr);
      CHECK(canvas_record_find_text("Pistol") == nullptr);   // plain label gone
      CHECK(canvas_record_find_text("Chaingun") != nullptr);
      CHECK(canvas_record_find_text("Shotgun") != nullptr);
      CHECK(canvas_record_find_text("Expander") != nullptr);
      CHECK(g_cfg.accpistol == 1);   // the switch itself never moved
      CHECK(canvas_record_find_text("3 cheats live") != nullptr);
      CHECK(canvas_record_find_text("4 cheats live") == nullptr);

      // Latch the shared hitscan pair: both Chaingun and Shotgun get the suffix, since one fingerprint mismatch takes
      // both down.
      accuracy_latch_reset_all();
      accuracy_latch_pair(ACC_PAIR_HITSCAN);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Chaingun (Unavailable)") != nullptr);
      CHECK(canvas_record_find_text("Shotgun (Unavailable)") != nullptr);
      CHECK(canvas_record_find_text("Chaingun") == nullptr);
      CHECK(canvas_record_find_text("Shotgun") == nullptr);
      CHECK(canvas_record_find_text("Pistol") != nullptr);     // pistol untouched
      CHECK(canvas_record_find_text("Expander") != nullptr);   // expander untouched
      CHECK(canvas_record_find_text("2 cheats live") != nullptr);   // pistol + expander only

      // A pair whose switch is off still shows the suffix, but "off" and "off, unavailable" both count 0: guards
      // accuracy_row_live's `value != 0` term against a latch that counted regardless.
      accuracy_latch_reset_all();
      g_cfg.accexpander = 0;
      accuracy_latch_pair(ACC_PAIR_EXPANDER);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Expander (Unavailable)") != nullptr);
      CHECK(canvas_record_find_text("3 cheats live") != nullptr);   // pistol+chaingun+shotgun

      accuracy_latch_reset_all();
      weap_restore(savedWeap);
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    {
      const int savedTab = menu_active_tab();
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_set_tab(TAB_WEAPONS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Each switch removes that weapon's own spread.") == nullptr);
      CHECK(canvas_record_find_text("Shotgun pellets all land in one hole.") == nullptr);
      CHECK(streq(tooltip_for_row("Pistol", 3440, 1440),
                  "Removes this weapon's own spread."));
      CHECK(streq(tooltip_for_row("Chaingun", 3440, 1440),
                  "Removes this weapon's own spread."));
      CHECK(streq(tooltip_for_row("Expander", 3440, 1440),
                  "Removes this weapon's own spread."));
      CHECK(streq(tooltip_for_row("Shotgun", 3440, 1440),
                  "Removes the spread; pellets land in one hole."));
      CHECK(strlen("Removes this weapon's own spread.") < 63);
      CHECK(strlen("Removes the spread; pellets land in one hole.") < 63);
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // Far-to-near draw order: far first, so the near label is drawn last and wins every overlap.
    {
      EspMarker m[6];
      // The fixture is neither sorted nor reversed on the way in, so a "sort" that only reverses the array still fails.
      const int dist[6] = { 900, 100, 900, 500, 100, 700 };
      for (int i = 0; i < 6; i++) {
        m[i].kind = ESP_KIND_ENEMY; m[i].index = 100 + i;
        m[i].dist2d = dist[i]; m[i].seq = i;
        const ScreenPt zp = { 0, 0, 0, dist[i], true };
        const ScreenBox zb = { 0, 0, 0, 0, false, 0 };
        m[i].p = zp; m[i].box = zb;
      }
      esp_sort_far_to_near(m, 6);
      CHECK(m[0].dist2d == 900 && m[1].dist2d == 900);
      CHECK(m[2].dist2d == 700);
      CHECK(m[3].dist2d == 500);
      CHECK(m[4].dist2d == 100 && m[5].dist2d == 100);
      // Stable at equal distances: otherwise same-range enemies swap places every frame and their labels flicker.
      CHECK(m[0].seq == 0 && m[1].seq == 2);
      CHECK(m[4].seq == 1 && m[5].seq == 4);
      CHECK(m[0].index == 100 && m[1].index == 102);
      CHECK(m[5].index == 104 && m[5].p.dist2d == 100);
      esp_sort_far_to_near(0, 6);              // null is a no-op, not a fault
      esp_sort_far_to_near(m, 0);
      esp_sort_far_to_near(m, 1);
      CHECK(m[0].seq == 0 && m[1].seq == 2);   // unchanged by all three
      // Categories sort together, not per category: a secret room three metres away must draw over an enemy forty
      // metres away.
      EspMarker mix[3];
      mix[0].kind = ESP_KIND_SECRET; mix[0].index = 7; mix[0].dist2d = 1500; mix[0].seq = 0;
      mix[1].kind = ESP_KIND_ENEMY;  mix[1].index = 8; mix[1].dist2d = 20000; mix[1].seq = 1;
      mix[2].kind = ESP_KIND_ITEM;   mix[2].index = 9; mix[2].dist2d = 9000;  mix[2].seq = 2;
      esp_sort_far_to_near(mix, 3);
      CHECK(mix[0].kind == ESP_KIND_ENEMY);
      CHECK(mix[1].kind == ESP_KIND_ITEM);
      CHECK(mix[2].kind == ESP_KIND_SECRET);
    }

    // Two-run label, name then numbers: the name stays Bahnschrift (FONT_SMALL), cur/max numbers move to Cascadia Mono
    // (FONT_MONO) so digits keep their width down a column. The pair centres over the box together, or the runs would
    // slide as health changes.
    {
      const EspLabelRuns r = esp_label_runs(200, 100, 60, 30);
      CHECK(r.totalW == 60 + ESP_LABEL_RUN_GAP + 30);
      CHECK(r.nameX == 250 - r.totalW / 2);
      CHECK(r.numX == r.nameX + 60 + ESP_LABEL_RUN_GAP);
      // No gap when either run is empty (unnamed enemy: numbers alone; item: name alone): neither may be pushed off
      // centre.
      const EspLabelRuns nameOnly = esp_label_runs(200, 100, 60, 0);
      CHECK(nameOnly.totalW == 60);
      CHECK(nameOnly.nameX == 250 - 30);
      const EspLabelRuns numOnly = esp_label_runs(200, 100, 0, 30);
      CHECK(numOnly.totalW == 30);
      CHECK(numOnly.numX == 250 - 15);
      // A saturated box origin cannot overflow: project_world saturates its coordinates, so a box can start at INT_MAX.
      const EspLabelRuns huge = esp_label_runs(2147483647, 2147483647, 60, 30);
      CHECK(huge.nameX <= 2147483647 && huge.numX <= 2147483647);
    }
    {
      // Runs share a bottom edge, so the taller mono face rises above the name rather than dropping into the
      // ESP_LABEL_GAP.
      CHECK(esp_label_mono_y(100, 16, 18) == 98);
      CHECK(esp_label_mono_y(100, 18, 16) == 102);
      CHECK(esp_label_mono_y(100, 16, 16) == 100);
      // Against the real metrics: a regenerated atlas that changed a face's height is caught here, not in game.
      CHECK(esp_label_mono_y(0, font_height[FONT_SMALL], font_height[FONT_MONO])
            == font_height[FONT_SMALL] - font_height[FONT_MONO]);
      CHECK(FONT_MONO != FONT_SMALL);
      CHECK(canvas_text_width("00000", FONT_MONO) == 5 * canvas_text_width("0", FONT_MONO));
      CHECK(canvas_text_width("iiiii", FONT_MONO) == canvas_text_width("00000", FONT_MONO));
    }

    // Explodables: crack sprites and breakable walls.

    // Wall field offsets, copied from docs/re/switches-breakables.md G, pinned against the stride they must fit inside.
    {
      CHECK(rva::WAL_POINT2     == 8u);
      CHECK(rva::WAL_NEXTSECTOR == 12u);
      CHECK(rva::WAL_CSTAT      == 14u);
      CHECK(rva::WAL_PICNUM     == 16u);
      CHECK(rva::WAL_PAL        == 21u);
      CHECK(rva::WAL_LOTAG      == 26u);
      CHECK(rva::WAL_HITAG      == 28u);
      // All seven distinct, or a transposed pair reads the wrong field silently.
      const unsigned offs[7] = { rva::WAL_POINT2, rva::WAL_NEXTSECTOR, rva::WAL_CSTAT,
                                 rva::WAL_PICNUM, rva::WAL_PAL, rva::WAL_LOTAG, rva::WAL_HITAG };
      bool allDistinct = true;
      for (int i = 0; i < 7; i++)
        for (int j = i + 1; j < 7; j++)
          if (offs[i] == offs[j]) allDistinct = false;
      CHECK(allDistinct);
      // The end of every read stays inside the 32-byte walltype, the end-of-read discipline sprite_field_read_ok holds
      // sprite fields to.
      CHECK(rva::WAL_HITAG + 2 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_PAL   + 1 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_POINT2     + 2 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_NEXTSECTOR + 2 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_CSTAT      + 2 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_PICNUM     + 2 <= rva::WALL_STRIDE);
      CHECK(rva::WAL_LOTAG      + 2 <= rva::WALL_STRIDE);
    }

    // Crack and breakable tables: named-lookup tables in esp_names.cpp; a null answer means "not one of these".
    {
      // The four crack picnums, CRACK1..CRACK4 (docs/re/switches-breakables.md B).
      CHECK(streq(esp_explodable_name(546), "Explodable"));
      CHECK(streq(esp_explodable_name(547), "Explodable"));
      CHECK(streq(esp_explodable_name(548), "Explodable"));
      CHECK(streq(esp_explodable_name(549), "Explodable"));
      CHECK(esp_explodable_name_count() == 4);
      // Neighbours are not cracks: an off-by-one would put a marker on every wall panel beside a crack.
      CHECK(esp_explodable_name(545) == nullptr);
      CHECK(esp_explodable_name(550) == nullptr);
      // These five are the game's destructible props (two boxes, a palm tree, a cone, a tire; checkhitsprite
      // 0x38c/0x38e/0x3b7/0x3d2/0x3de), not cracks.
      CHECK(esp_explodable_name(908) == nullptr);
      CHECK(esp_explodable_name(910) == nullptr);
      CHECK(esp_explodable_name(951) == nullptr);
      CHECK(esp_explodable_name(978) == nullptr);
      CHECK(esp_explodable_name(990) == nullptr);
      // Explosive barrels are deliberately not in this table: explodables are the crack family only.
      CHECK(esp_explodable_name(1079) == nullptr);
      CHECK(esp_explodable_name(1247) == nullptr);
      CHECK(esp_explodable_name(1238) == nullptr);

      // The nineteen breakable wall picnums (docs/re/switches-breakables.md C).
      const int breakables[19] = { 293, 297, 299, 301, 305, 306, 307,
                                   1212, 1215,
                                   4130, 4131, 4132, 4133, 4134, 4136, 4138, 4140, 4142, 4181 };
      for (int i = 0; i < 19; i++) CHECK(streq(esp_breakable_name(breakables[i]), "Breakable"));
      CHECK(esp_breakable_name_count() == 19);
      // Broken picnums the engine swaps in are not in the set: "still in the set" means "not yet broken".
      CHECK(esp_breakable_name(360) == nullptr && esp_breakable_name(363) == nullptr);
      CHECK(esp_breakable_name(4144) == nullptr && esp_breakable_name(4147) == nullptr);
      CHECK(esp_breakable_name(4203) == nullptr);
      // Glass, screens and light walls are shootable decoration, not explodables (docs/re/switches-breakables.md C).
      CHECK(esp_breakable_name(263) == nullptr && esp_breakable_name(275) == nullptr);
      CHECK(esp_breakable_name(806) == nullptr && esp_breakable_name(1289) == nullptr);
      CHECK(esp_breakable_name(120) == nullptr && esp_breakable_name(705) == nullptr);
      CHECK(esp_breakable_name(4135) == nullptr);   // between 4134 and 4136, deliberately absent
      CHECK(esp_breakable_name(4137) == nullptr);
      // The four name tables are disjoint over the whole tile range: an enemy must never read as "Breakable", a crack
      // never as a pickup.
      bool overlap = false;
      for (int pic = 0; pic < 9500; pic++) {
        const int n = (esp_enemy_name(pic) ? 1 : 0) + (esp_item_name(pic) ? 1 : 0)
                    + (esp_explodable_name(pic) ? 1 : 0) + (esp_breakable_name(pic) ? 1 : 0);
        if (n > 1) overlap = true;
      }
      CHECK(!overlap);
      // Every table entry is inside the tile range the accessors bound against.
      for (int i = 0; i < esp_explodable_name_count(); i++) {
        const int pic = esp_explodable_name_picnum(i);
        CHECK(pic >= 0 && pic < 9500);
      }
      for (int i = 0; i < esp_breakable_name_count(); i++) {
        const int pic = esp_breakable_name_picnum(i);
        CHECK(pic >= 0 && pic < 9500);
      }
      CHECK(esp_explodable_name_picnum(-1) == -1);
      CHECK(esp_explodable_name_picnum(esp_explodable_name_count()) == -1);
      CHECK(esp_breakable_name_picnum(-1) == -1);
      CHECK(esp_breakable_name_picnum(esp_breakable_name_count()) == -1);
    }

    // Crack visibility and the wall midpoint.
    {
      // Shown only when hitag > 0 (docs/re/switches-breakables.md B): hitag links the crack to movestandables' DETONATE
      // branch; the game never blows up a hitag-0 crack, it is inert decoration.
      CHECK(!esp_crack_visible(0));   // 546, hitag 0 -> not shown
      CHECK( esp_crack_visible(7));   // 546, hitag 7 -> shown
      CHECK( esp_crack_visible(1));
      CHECK(!esp_crack_visible(-1));
      // Liveness is not this function's job: the game deletes a detonated crack's sprite outright, so "still on statnum
      // 6" is the rule.

      // 2D midpoint of a wall, widened so opposite-end int coordinates cannot overflow (Build coordinates are signed
      // longs).
      CHECK(esp_wall_mid(0, 100) == 50);
      CHECK(esp_wall_mid(100, 0) == 50);            // order does not matter
      CHECK(esp_wall_mid(-100, 100) == 0);
      CHECK(esp_wall_mid(-1000, -2000) == -1500);
      CHECK(esp_wall_mid(2147483647, 2147483647) == 2147483647);
      CHECK(esp_wall_mid(-2147483647 - 1, -2147483647 - 1) == -2147483647 - 1);
      CHECK(esp_wall_mid(2147483647, -2147483647 - 1) == 0);   // no wrap

      // Z: halfway between floor and ceiling, so the marker sits mid-panel; ceilingz is the smaller number (z grows
      // downward).
      CHECK(esp_wall_mid_z(0, -16384) == -8192);
      CHECK(esp_wall_mid_z(-16384, 0) == -8192);
      CHECK(esp_wall_mid_z(2147483647, -2147483647 - 1) == 0);
      CHECK(esp_wall_mid_z(4096, 4096) == 4096);    // a zero-height sector is its own midpoint
    }

    // Wall snapshot's seqlock and its bound: the scan runs on the tic clock, markers draw on the render clock, so the
    // handoff is the seqlock AimSnapshot and FreecamState already use (esp.cpp cannot link into test_host; driven on a
    // synthetic snapshot).
    {
      EspWallSnapshot s;
      s.seq = 0; s.count = 0; s.overflow = 0;
      const unsigned before = s.seq;
      snap_begin(&s.seq);
      CHECK((s.seq & 1u) != 0);                       // odd while being written
      s.count = 3;
      snap_end(&s.seq);
      CHECK((s.seq & 1u) == 0);                       // even again, one step higher
      CHECK(!snap_read_ok(before, s.seq));            // a write happened across that read
      const unsigned stable = s.seq;
      CHECK(snap_read_ok(stable, s.seq));             // and a quiet read is accepted
      CHECK(ESP_WALL_MAX == 128);
      // Struct sized to hold what it claims: a snapshot reporting a count past its own array would read off the end on
      // the render clock.
      CHECK((int)(sizeof(s.m) / sizeof(s.m[0])) == ESP_WALL_MAX);
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.espexplodables == 0);               // every feature defaults off
      // Opens the GL bracket on its own, or markers never appear with the panel closed. Eight parameters: espenemies,
      // espitems, espsecrets, aimmarker, playermarker, notice, espexplodables.
      CHECK(esp_any_category_on(0, 0, 0, 0, 0, 0, 1, 0));
      CHECK(!esp_any_category_on(0, 0, 0, 0, 0, 0, 0, 0));
      CHECK(menu_live_count(0,0,0,0, 1, 0, 0,0,0, 0,0,0,0, 0, 0, 0) == 1);   // espexplodables alone
      CHECK(menu_live_count(0,0,0,0, 0, 0, 0,0,0, 0,0,0,0, 0, 0, 0) == 0);

      // Ini round trip, checked against espsecrets both ways, so an aliased load or wrong save cannot match a fixture
      // that agrees.
      const char* t = "test_esp_task2_phase5a.ini";
      FILE* f = nullptr;
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespsecrets=1\nespexplodables=0\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espsecrets == 1);
      CHECK(g_cfg.espexplodables == 0);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espsecrets == 1);
      CHECK(g_cfg.espexplodables == 0);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespsecrets=0\nespexplodables=1\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espsecrets == 0);
      CHECK(g_cfg.espexplodables == 1);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espsecrets == 0);
      CHECK(g_cfg.espexplodables == 1);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespexplodables=7\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espexplodables == 1);
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespexplodables=-3\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espexplodables == 1);

      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults in place
    }

    // The Visuals row is reachable, and alone: vis_sweep clicks every pixel and unions what changed, and `multi` is the
    // "no click moves two fields" guarantee every other row on this tab carries.
    {
      const int savedTab = menu_active_tab();
      const VisState saved = vis_snapshot();
      bool multi = false;
      const VisState eoff = { 0, 1, 1, 1, 1, 0, 0, 6144, 0 };
      const int reached = vis_sweep(3440, 1440, eoff, false, &multi);
      CHECK((reached & VF_EXPLODABLES) != 0);
      CHECK(!multi);
      vis_restore(saved);
      menu_set_tab(savedTab);
    }

    // Whether the tic-clock scan should run at all: pinned so dropping the gate (unconditional scan) fails here. Any
    // nonzero value counts.
    {
      CHECK(!esp_world_should_scan(0, 0));
      CHECK( esp_world_should_scan(1, 0));
      CHECK( esp_world_should_scan(7, 0));
      CHECK( esp_world_should_scan(-3, 0));
    }

    // Render-side count clamp, independent of the seqlock (whose ordering under /O2 volatile semantics is untestable
    // directly), the same shape as wall_range_ok/sprite_field_read_ok.
    {
      CHECK(esp_wall_count_clamped(-5) == 0);
      CHECK(esp_wall_count_clamped(0) == 0);
      CHECK(esp_wall_count_clamped(64) == 64);
      CHECK(esp_wall_count_clamped(ESP_WALL_MAX) == ESP_WALL_MAX);
      CHECK(esp_wall_count_clamped(ESP_WALL_MAX + 1) == ESP_WALL_MAX);
      CHECK(esp_wall_count_clamped(2147483647) == ESP_WALL_MAX);
      CHECK(esp_wall_count_clamped(-2147483647 - 1) == 0);
    }

    // canvas_esp_explodable_colour: equal to C_HAZARD and to canvas_esp_secret_colour() on purpose, different from
    // every other marker colour.
    {
      CHECK(canvas_esp_explodable_colour() == C_HAZARD);
      // Equal to the secret colour on purpose: the label above the marker ("Explodable"/"Breakable" vs "Secret") tells
      // them apart.
      CHECK(canvas_esp_explodable_colour() == canvas_esp_secret_colour());
      CHECK(canvas_esp_explodable_colour() != canvas_esp_colour(true));    // enemy
      CHECK(canvas_esp_explodable_colour() != canvas_esp_colour(false));   // item
      CHECK(canvas_esp_explodable_colour() != canvas_aim_colour());
      CHECK(canvas_esp_explodable_colour() != canvas_player_colour());
    }

    // Switches and locks, with the combination hint.

    // PS_GOT_ACCESS, copied from docs/re/switches-breakables.md G, pinned against the struct it must fit inside.
    {
      CHECK(rva::PS_GOT_ACCESS == 732u);
      CHECK(rva::PS_GOT_ACCESS + 2u <= rva::PS_STRIDE);
      // Distinct from every neighbouring field, or a transposed offset silently reads the wrong one.
      CHECK(rva::PS_GOT_ACCESS != rva::PS_CURSECTNUM);
      CHECK(rva::PS_GOT_ACCESS != rva::PS_CURR_WEAPON);
      CHECK(rva::PS_GOT_ACCESS != rva::PS_AUTO_AIM);
    }

    {
      CHECK(SPR_LOTAG == 0x26u);
      CHECK(SPR_HITAG == 0x28u);
      CHECK(sprite_field_read_ok(SPR_LOTAG, 2));
      CHECK(sprite_field_read_ok(SPR_HITAG, 2));
      // Sprite switches live on statnum 0 (docs/re/switches-breakables.md A), distinct from STAT_ACTOR/STAT_ZOMBIEACTOR
      // and ESP_STAT_STANDABLE.
      CHECK(ESP_STAT_SWITCH == 0);
      CHECK(ESP_STAT_SWITCH != STAT_ACTOR);
      CHECK(ESP_STAT_SWITCH != STAT_ZOMBIEACTOR);
      CHECK(ESP_STAT_SWITCH != ESP_STAT_STANDABLE);
    }

    // The wall scan's gate covers switches too: esp_world_should_scan runs when either espexplodables or espswitches is
    // on, since the scan is the shared producer for both wall-mark kinds.
    {
      CHECK(!esp_world_should_scan(0, 0));
      CHECK( esp_world_should_scan(1, 0));
      CHECK( esp_world_should_scan(0, 1));
      CHECK( esp_world_should_scan(1, 1));
      CHECK( esp_world_should_scan(7, 0));
      CHECK( esp_world_should_scan(0, -3));
    }


    // Which tiles are switches: the engine's own case lists out of checkhitswitch (VA 0x0050ea80,
    // docs/re/switches-breakables.md A), base picnums plus their pressed frame; 146..149 is a four-frame cycle, not a
    // pair.
    {
      const int bases[19] = { 130, 132, 134, 136, 138, 140, 162, 164, 166, 168, 170,
                              712, 860, 862, 864, 1111, 1122, 1142, 146 };
      for (int i = 0; i < 18; i++) {                     // the eighteen two-frame families
        CHECK(switch_base_for(bases[i]) == bases[i]);
        CHECK(switch_base_for(bases[i] + 1) == bases[i]);   // the pressed frame
        CHECK(switch_base_for(bases[i] + 2) != bases[i]);   // and nothing past it
      }
      CHECK(switch_base_for(146) == 146);
      CHECK(switch_base_for(147) == 146);
      CHECK(switch_base_for(148) == 146);
      CHECK(switch_base_for(149) == 146);
      // 150 is also a switch tile: the door-tile "usable" set (isadoorwall), disjoint from the case-list families
      // above. It once read -1, so door-tile "usable" walls got no marker at all.
      CHECK(switch_base_for(150) == 150);
      CHECK(switch_base_for(145) == -1);     // still nothing: not a door tile either
      CHECK(switch_base_for(0) == -1);
      CHECK(switch_base_for(129) == -1);
      CHECK(switch_base_for(131) == 130);    // ... but the pressed ACCESSSWITCH is
      CHECK(switch_base_for(1680) == -1);    // an enemy tile is never a switch
      CHECK(switch_base_for(9499) == -1);
      CHECK(switch_base_for(-1) == -1);
    }

    // The door-tile "usable" set: isadoorwall(picnum), VA 0x0050be30 (docs/re/switches-breakables.md A). A
    // door-textured wall with nonzero lotag fires its channel when pressed, with no switch frame to flip.
    {
      const int doors[] = { 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
                            395, 447, 448, 449, 717, 781, 1102, 1144, 1169,
                            1178, 1179, 4391 };
      for (int i = 0; i < (int)(sizeof(doors) / sizeof(doors[0])); i++)
        CHECK(isadoorwall(doors[i]));

      // Set edges, pinned individually: an off-by-one drops a real door tile or marks a neighbour that is not one.
      CHECK(!isadoorwall(149));
      CHECK( isadoorwall(150));
      CHECK( isadoorwall(159));
      CHECK(!isadoorwall(160));
      CHECK(!isadoorwall(1177));
      CHECK(!isadoorwall(1180));
      CHECK(!isadoorwall(394));  CHECK(!isadoorwall(396));
      CHECK(!isadoorwall(446));  CHECK(!isadoorwall(450));
      CHECK(!isadoorwall(716));  CHECK(!isadoorwall(718));
      CHECK(!isadoorwall(780));  CHECK(!isadoorwall(782));
      CHECK(!isadoorwall(1101)); CHECK(!isadoorwall(1103));
      CHECK(!isadoorwall(1143)); CHECK(!isadoorwall(1145));
      CHECK(!isadoorwall(1168)); CHECK(!isadoorwall(1170));
      CHECK(!isadoorwall(4390)); CHECK(!isadoorwall(4392));
      CHECK(!isadoorwall(0));
      CHECK(!isadoorwall(130));
      CHECK(!isadoorwall(1680));
      CHECK(!isadoorwall(-1));

      // switch_base_for returns the picnum itself for a door tile, unlike the pressed-frame arithmetic of the two-state
      // families.
      CHECK(switch_base_for(150) == 150);
      CHECK(switch_base_for(159) == 159);
      CHECK(switch_base_for(4391) == 4391);
      CHECK(switch_base_for(151) != switch_base_for(150));   // no base/base+1 pairing here

      // switch_classify: SW_DOOR, label "Usable" (never "Switch"), no second line at any pal/hitag/access combination.
      const SwitchInfo door = switch_classify(150, 0, 0, 9, 0);
      CHECK(door.family == SW_DOOR);
      CHECK(door.base == 150);
      CHECK(door.line == nullptr);
      CHECK(streq(switch_label(door.family), "Usable"));
      CHECK(streq(switch_label(SW_TWOSTATE), "Switch"));
      CHECK(streq(switch_label(SW_ACCESS), "Switch"));
      CHECK(streq(switch_label(SW_MULTI), "Switch"));
      CHECK(streq(switch_label(SW_COMBINATION), "Switch"));
      CHECK(switch_classify(159, 21, 1, 9, 7).family == SW_DOOR);
      CHECK(switch_classify(159, 21, 1, 9, 7).line == nullptr);
      CHECK(switch_classify(4391, 0, 0, 4, 0).family == SW_DOOR);
      // lotag 0 not shown, like every other family; lotag -1 (end of level) is.
      CHECK(switch_classify(150, 0, 0, 0, 0).family == SW_NONE);
      CHECK(switch_classify(150, 0, 0, -1, 0).family == SW_DOOR);

      // The door-tile set is disjoint from every other named table over the whole tile range.
      bool doorOverlap = false;
      for (int pic = 0; pic < 9500; pic++) {
        if (!isadoorwall(pic)) continue;
        if (esp_breakable_name(pic) || esp_enemy_name(pic) ||
            esp_item_name(pic) || esp_explodable_name(pic))
          doorOverlap = true;
      }
      CHECK(!doorOverlap);
    }

    {
      // Lotag 0 not shown (the game ignores such a switch); lotag -1 ends the level and is a live switch
      // (docs/re/switches-breakables.md A).
      const SwitchInfo dead = switch_classify(130, 0, 0, 0, 0);
      CHECK(dead.family == SW_NONE);
      CHECK(switch_classify(132, 0, 0, -1, 0).family == SW_TWOSTATE);

      // Access: card chosen by pal, lock state is the player's got_access bit for it: pal 0 blue -> bit 1, 21 red ->
      // bit 2, 23 yellow -> bit 4, from checkhitswitch's quote 70/71/72 branches.
      const SwitchInfo needBlue = switch_classify(130, ESP_PAL_KEY_BLUE, 0, 4, 0);
      CHECK(needBlue.family == SW_ACCESS && needBlue.base == 130);
      CHECK(streq(needBlue.line, "Needs blue card"));
      CHECK(streq(switch_classify(170, ESP_PAL_KEY_RED, 0, 4, 0).line, "Needs red card"));
      CHECK(streq(switch_classify(130, ESP_PAL_KEY_YELLOW, 0, 4, 0).line, "Needs yellow card"));
      // Held: the line is dropped, not rewritten (a lock you can open is not worth screen space).
      CHECK(switch_classify(130, ESP_PAL_KEY_BLUE, 0, 4, 1).line == nullptr);
      CHECK(switch_classify(130, ESP_PAL_KEY_RED, 0, 4, 2).line == nullptr);
      CHECK(switch_classify(130, ESP_PAL_KEY_YELLOW, 0, 4, 4).line == nullptr);
      // The right bit, not any bit: holding blue does not open red.
      CHECK(streq(switch_classify(130, ESP_PAL_KEY_RED, 0, 4, 1).line, "Needs red card"));
      CHECK(streq(switch_classify(130, ESP_PAL_KEY_BLUE, 0, 4, 6).line, "Needs blue card"));
      CHECK(switch_classify(130, ESP_PAL_KEY_RED, 0, 4, 7).line == nullptr);
      // A pal the engine has no colour for gets no second line, not a guessed one (as esp_item_name_pal does for a
      // card).
      CHECK(switch_classify(130, 9, 0, 4, 0).family == SW_ACCESS);
      CHECK(switch_classify(130, 9, 0, 4, 0).line == nullptr);
      CHECK(switch_classify(131, ESP_PAL_KEY_BLUE, 0, 4, 0).family == SW_ACCESS);

      // Keycards are never consumed in this build, so an opened access switch sits on its pressed frame with got_access
      // still set; the line must be null regardless of bits, or ESP keeps asking for a card the player already used.
      CHECK(switch_classify(131, 0, 0, 4, 0).family == SW_ACCESS);
      CHECK(switch_classify(131, 0, 0, 4, 0).line == nullptr);           // 131, pal 0, bits 0 -> no line
      CHECK(streq(switch_classify(130, 0, 0, 4, 0).line, "Needs blue card"));  // 130, pal 0, bits 0
      CHECK(switch_classify(130, 0, 0, 4, 1).line == nullptr);           // 130, pal 0, bits 1 -> held already
      CHECK(switch_classify(171, ESP_PAL_KEY_RED, 0, 4, 0).family == SW_ACCESS);
      CHECK(switch_classify(171, ESP_PAL_KEY_RED, 0, 4, 0).line == nullptr);
      CHECK(switch_classify(131, ESP_PAL_KEY_YELLOW, 0, 4, 0).line == nullptr);
      // A pressed frame with an unrecognised pal is still no-line: pressed is checked before the pal switch, not
      // instead of it.
      CHECK(switch_classify(131, 9, 0, 4, 0).family == SW_ACCESS);
      CHECK(switch_classify(131, 9, 0, 4, 0).line == nullptr);

      // Combination: each member's required state is its hitag (0 = base frame, 1 = pressed), members share a lotag;
      // the game counts members whose frame matches their hitag against the member count.
      CHECK(streq(switch_classify(162, 0, 1, 9, 0).line, "Flip"));    // on base, wants pressed
      CHECK(streq(switch_classify(163, 0, 1, 9, 0).line, "Leave"));   // pressed, wants pressed
      CHECK(streq(switch_classify(162, 0, 0, 9, 0).line, "Leave"));   // base, wants base
      CHECK(streq(switch_classify(163, 0, 0, 9, 0).line, "Flip"));    // pressed, wants base
      CHECK(switch_classify(166, 0, 1, 9, 0).family == SW_COMBINATION);
      CHECK(switch_classify(1142, 0, 1, 9, 0).family == SW_COMBINATION);
      CHECK(streq(switch_classify(1143, 0, 0, 9, 0).line, "Flip"));
      // A hitag outside {0, 1} reads as "wants pressed" (any-nonzero tolerance, as combat_sub_enabled).
      CHECK(streq(switch_classify(162, 0, 7, 9, 0).line, "Flip"));

      CHECK(switch_classify(134, 0, 0, 9, 0).family == SW_TWOSTATE);
      CHECK(switch_classify(134, 0, 0, 9, 0).line == nullptr);
      CHECK(switch_classify(146, 0, 0, 9, 0).family == SW_MULTI);
      CHECK(switch_classify(146, 0, 0, 9, 0).line == nullptr);
      CHECK(switch_classify(149, 0, 0, 9, 0).family == SW_MULTI);
      CHECK(switch_classify(135, 0, 0, 9, 0).family == SW_TWOSTATE);
      CHECK(switch_classify(141, 0, 0, 9, 0).family == SW_TWOSTATE);

      CHECK(switch_classify(1680, 0, 0, 9, 0).family == SW_NONE);
      CHECK(switch_classify(300, 21, 1, 9, 7).family == SW_NONE);
      CHECK(switch_classify(300, 21, 1, 9, 7).line == nullptr);
    }

    // Which wall/lotag pairs the tic-clock scan may spend an ESP_WALL_MAX slot capturing: switch_base_for(picnum) >= 0
    // is not enough, since switch_classify refuses lotag 0 for every family, and that must run before the scan spends a
    // slot.
    {
      CHECK(!esp_wall_switch_capturable(150, 0));
      CHECK(!esp_wall_switch_capturable(159, 0));
      CHECK(!esp_wall_switch_capturable(4391, 0));
      CHECK(esp_wall_switch_capturable(150, 5));
      CHECK(esp_wall_switch_capturable(4391, 5));
      // lotag -1 is not zero (end-of-level switch) and stays capturable: the test must be `lotag != 0`, never `lotag >
      // 0`.
      CHECK(esp_wall_switch_capturable(150, -1));
      // A real switch (two-frame families or four-frame MULTISWITCH) with lotag 0 is refused too: the rule is the
      // family's, not doors'.
      CHECK(!esp_wall_switch_capturable(130, 0));
      CHECK(!esp_wall_switch_capturable(146, 0));
      CHECK(esp_wall_switch_capturable(130, 4));
      CHECK(esp_wall_switch_capturable(130, -1));
      CHECK(esp_wall_switch_capturable(146, 4));
      CHECK(!esp_wall_switch_capturable(1680, 5));
      CHECK(!esp_wall_switch_capturable(0, 5));
      CHECK(!esp_wall_switch_capturable(-1, 5));
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.espswitches == 0);
      CHECK(esp_any_category_on(0, 0, 0, 0, 0, 0, 0, 1));
      CHECK(!esp_any_category_on(0, 0, 0, 0, 0, 0, 0, 0));
      CHECK(menu_live_count(0,0,0,0, 0,1, 0,0,0, 0,0,0,0, 0, 0, 0) == 1);   // espswitches alone
      CHECK(menu_live_count(0,0,0,0, 1,1, 0,0,0, 0,0,0,0, 0, 0, 0) == 2);   // both new categories
      // Colour is pickup blue, deliberately, differing from the other markers.
      CHECK(canvas_esp_switch_colour() == C_BLUE);
      CHECK(canvas_esp_switch_colour() != canvas_esp_secret_colour());
      CHECK(canvas_esp_switch_colour() != canvas_aim_colour());
      CHECK(canvas_esp_switch_colour() != canvas_player_colour());
      CHECK(canvas_esp_switch_colour() != canvas_esp_colour(true));
      // Shares C_BLUE with the item colour on purpose (config.h), but must never be mistaken for the enemy colour.
      CHECK(canvas_esp_switch_colour() == canvas_esp_colour(false));   // item, on purpose
      CHECK(canvas_esp_switch_colour() != canvas_esp_explodable_colour());
    }

    // Ini round trip: espswitches checked against espexplodables both ways.
    {
      const char* t = "test_esp_task3_phase5a.ini";
      FILE* f = nullptr;
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespexplodables=1\nespswitches=0\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espexplodables == 1);
      CHECK(g_cfg.espswitches == 0);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espexplodables == 1);
      CHECK(g_cfg.espswitches == 0);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespexplodables=0\nespswitches=1\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espexplodables == 0);
      CHECK(g_cfg.espswitches == 1);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espexplodables == 0);
      CHECK(g_cfg.espswitches == 1);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespswitches=7\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espswitches == 1);
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespswitches=-3\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espswitches == 1);

      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults in place
    }

    {
      const int savedTab = menu_active_tab();
      const VisState saved = vis_snapshot();
      bool multi = false;
      const VisState soff = { 0, 1, 1, 1, 1, 0, 0, 6144, 0, 0 };
      const int reached = vis_sweep(3440, 1440, soff, false, &multi);
      CHECK((reached & VF_SWITCHES) != 0);
      CHECK(!multi);
      vis_restore(saved);
      menu_set_tab(savedTab);
    }


    // Silent aim re-solves from the live target: hooked_shoot runs inside processinput before aim_tic publishes this
    // tic's snapshot, so the substituted solution is one tic old (~33ms of target motion, silent.h). This re-runs the
    // same lead/solve from where the target IS at the moment of the shot.
    {
      WeaponInfo hit; hit.weaponIndex = 1; hit.cls = WC_HITSCAN; hit.vel = 0; hit.horizMul = 1;
      Point3 eye; eye.x = 0; eye.y = 0; eye.z = 0;

      // A target that moved yields the live angle, not the published one (the published pair is deliberately nowhere
      // near live geometry).
      Point3 live; live.x = 1000; live.y = 1000; live.z = 0;
      // silent_resolve's trailing curAng/maxRedirectUnits re-check the live angle against the redirect cone. These
      // calls don't test the cone, so maxRedirectUnits = 1024 (half the circle, the max wrapped delta) passes any
      // curAng; the cone is pinned below.
      const SilentResolve moved =
          silent_resolve(eye, 0, hit, 1234, 77, live, 0, 0, 0, 0, 70, 0, 1024);
      CHECK(moved.valid);
      CHECK(moved.live);
      CHECK(moved.ang != 1234);
      CHECK(!moved.coneRejected);
      // ... and it is the SAME answer target_solve gives for the same geometry: the re-solve is the existing solver,
      // not a copy.
      Point3 aimPt; aimPt.x = 1000; aimPt.y = 1000; aimPt.z = 0;
      const AimSolution direct = target_solve(eye, aimPt, 0, hit);
      CHECK(moved.ang == direct.ang);
      CHECK(moved.horiz == direct.horiz);

      // A TARGET THAT DID NOT MOVE lands on the published answer: the two paths agree when there is nothing to correct.
      Point3 same;
      same.x = 1000; same.y = 0; same.z = 0;
      const AimSolution pub = target_solve(eye, same, 0, hit);
      const SilentResolve still =
          silent_resolve(eye, 0, hit, pub.ang, pub.horiz, same, 0, 0, 0, 0, 70, 0, 1024);
      CHECK(still.valid && still.ang == pub.ang && still.horiz == pub.horiz);

      // Lead is applied through the same target_lead/target_lead_scaled the aimbot uses, at the same scale.
      WeaponInfo rpg; rpg.weaponIndex = 4; rpg.cls = WC_PROJECTILE; rpg.vel = 644; rpg.horizMul = 8;
      Point3 mover; mover.x = 8000; mover.y = 0; mover.z = 0;
      const SilentResolve noLead =
          silent_resolve(eye, 0, rpg, 0, 0, mover, 0, 300, 0, 0, 70, 0, 1024);        // leadVel 0 = no lead
      const SilentResolve withLead =
          silent_resolve(eye, 0, rpg, 0, 0, mover, 0, 300, 0, rpg.vel, 70, 0, 1024);
      CHECK(noLead.valid && withLead.valid);
      CHECK(withLead.ang != noLead.ang);                       // the lead moved the angle
      const Point3 led = target_lead(eye, mover, 0, 300, 0, rpg.vel);
      const Point3 scaled = target_lead_scaled(mover, led, 70);
      const AimSolution expect = target_solve(eye, scaled, 0, rpg);
      CHECK(withLead.ang == expect.ang && withLead.horiz == expect.horiz);
      const SilentResolve zeroScale =
          silent_resolve(eye, 0, rpg, 0, 0, mover, 0, 300, 0, rpg.vel, 0, 0, 1024);
      CHECK(zeroScale.ang == noLead.ang && zeroScale.horiz == noLead.horiz);

      // A degenerate live solve (zero distance, never divides) falls back to the published pair rather than
      // substituting garbage.
      const SilentResolve degenerate =
          silent_resolve(eye, 0, hit, 1234, 77, eye, 0, 0, 0, 0, 70, 0, 1024);
      CHECK(!degenerate.live);
      CHECK(degenerate.valid);
      CHECK(degenerate.ang == 1234 && degenerate.horiz == 77);
      CHECK(!degenerate.coneRejected);
      // An unreachable pitch is the other fallback: the engine's own clamp would eat it, so the reach-checked published
      // pair is used.
      Point3 sky; sky.x = 40; sky.y = 0; sky.z = -400000;
      const SilentResolve unreachable =
          silent_resolve(eye, 0, hit, 1234, 77, sky, 0, 0, 0, 0, 70, 0, 1024);
      CHECK(!aim_horiz_reachable(target_solve(eye, sky, 0, hit).horiz));
      CHECK(!unreachable.live);
      CHECK(unreachable.valid);
      CHECK(unreachable.ang == 1234 && unreachable.horiz == 77);
      CHECK(!unreachable.coneRejected);

      // horizoff is carried into the live solve too: it is the player's own view offset, and dropping it tilts every
      // redirected shot.
      const SilentResolve withOff =
          silent_resolve(eye, 40, hit, 1234, 77, live, 0, 0, 0, 0, 70, 0, 1024);
      CHECK(withOff.horiz != moved.horiz);
      CHECK(withOff.horiz == target_solve(eye, aimPt, 40, hit).horiz);

      // The live re-solve must still pass the redirect cone silent_decide gated the substitution on, or a close fast
      // target's fresh angle could exceed "Max redirect". Reusing `direct` from the moved case isolates
      // curAng/maxRedirectUnits.
      const int maxU = 200;
      const SilentResolve insideCone =
          silent_resolve(eye, 0, hit, 1234, 77, live, 0, 0, 0, 0, 70,
                        direct.ang, maxU);
      CHECK(insideCone.valid);
      CHECK(insideCone.live);
      CHECK(!insideCone.coneRejected);
      CHECK(insideCone.ang == direct.ang && insideCone.horiz == direct.horiz);

      // Outside the cone: curAng is direct.ang's exact opposite (1024 units, the largest wrapped delta), failing any
      // maxUnits below 1024; as far from the cone as Build angles go, so it cannot pass by coincidence if the cone
      // check were dropped.
      const int oppositeAng = (direct.ang + 1024) & 2047;
      const SilentResolve outsideCone =
          silent_resolve(eye, 0, hit, 1234, 77, live, 0, 0, 0, 0, 70,
                        oppositeAng, maxU);
      CHECK(outsideCone.valid);          // a refusal is not an invalid solve
      CHECK(!outsideCone.live);          // ... but it did NOT win
      CHECK(outsideCone.coneRejected);   // ... and specifically because of the cone
      CHECK(outsideCone.ang == 1234 && outsideCone.horiz == 77);   // published pair, untouched
    }
    {
      // Smoother's accessor: the velocity the aimbot leads with; refuses when the identity does not match (a recycled
      // slot must not hand this shot the previous actor's motion).
      VelSmooth s = { -1, 0, 0, 0, 0, 0 };
      int ux = 0, uy = 0, uz = 0;
      CHECK(aim_vel_update(&s, 11, 1680, 300, -120, 0, &ux, &uy, &uz));
      int rx = 0, ry = 0, rz = 0;
      CHECK(vel_smooth_read(&s, 11, 1680, &rx, &ry, &rz));
      CHECK(rx == ux && ry == uy && rz == uz);
      CHECK(rx != 0 && ry != 0);        // fixture really is moving on both axes
      // Identity is the (index, picnum) pair: a Build slot recycles the instant its actor dies, so index alone is not
      // enough.
      int nx = 7, ny = 7, nz = 7;
      CHECK(!vel_smooth_read(&s, 12, 1680, &nx, &ny, &nz));   // wrong index
      CHECK(!vel_smooth_read(&s, 11, 2000, &nx, &ny, &nz));   // wrong picnum
      CHECK(nx == 7 && ny == 7 && nz == 7);                   // a refusal writes nothing
      CHECK(!vel_smooth_read(0, 11, 1680, &nx, &ny, &nz));
      CHECK(!vel_smooth_read(&s, 11, 1680, 0, &ny, &nz));     // a null out is a refusal
    }

    // Rung-to-z mapping: the live re-solve must rebuild its aim point at the rung cansee chose for a partially covered
    // target, not always rung 0. target_probe_z addresses the three formulas aim_probe_zs's ladder is built from by
    // rung index.
    {
      const int spriteZ = 100000;
      const int yrepeat = 40;
      const int tilesizy = 64;
      const int h = yrepeat * tilesizy;   // 2560

      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 0) ==
            target_aim_z(spriteZ, yrepeat, tilesizy, AP_CENTRE));
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_UPPER, 0) ==
            target_aim_z(spriteZ, yrepeat, tilesizy, AP_UPPER));
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 1) == spriteZ - 3 * h);
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 2) == spriteZ - 4 * h + h / 4);

      // Rung 0 and rung 2 give different z for the same sprite: the fact the old code ignored, and the fix depends on.
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 0) !=
            target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 2));
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 1) !=
            target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 2));

      // Out of range (including -1, "nothing was visible") falls back to rung 0, never garbage.
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 5) ==
            target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 0));
      CHECK(target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, -1) ==
            target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 0));

      // aim_probe_zs's ladder is built from exactly this function, in rung order: pinned against each other so the two
      // cannot drift.
      int probeZ[AIM_PROBE_MAX];
      const int probes = aim_probe_zs(spriteZ, yrepeat, tilesizy, AP_CENTRE, probeZ, AIM_PROBE_MAX);
      CHECK(probes == 3);   // all three rungs distinct for this fixture, so none dedupe away
      CHECK(probeZ[0] == target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 0));
      CHECK(probeZ[1] == target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 1));
      CHECK(probeZ[2] == target_probe_z(spriteZ, yrepeat, tilesizy, AP_CENTRE, 2));
    }

    // aim_candidate_ok's sixth term and the healthy-first preference (the one-view dying rule target_dying was replaced
    // by actor_state's targetable set).
    {
      // A DYING ENEMY IS FILTERED OUT OF TARGETING, a filter and not a preference. Every other term held constant, so
      // this pins the sixth alone: `targetable` is fed from the actor state (tb_0113's consumer table) and the term is
      // required.
      CHECK( aim_candidate_ok(true, true, true, true, true, true));
      CHECK(!aim_candidate_ok(true, true, true, true, true, false));
      // ... and it did not eat any of the other five on the way in.
      CHECK(!aim_candidate_ok(false, true, true, true, true, true));
      CHECK(!aim_candidate_ok(true, false, true, true, true, true));
      CHECK(!aim_candidate_ok(true, true, false, true, true, true));
      CHECK(!aim_candidate_ok(true, true, true, false, true, true));
      CHECK(!aim_candidate_ok(true, true, true, true, false, true));

      // Preference stays the fallback for an unreadable action: a non-positive-health candidate with unreadable action
      // is still eligible, still deprioritised.
      CHECK(!target_candidate_healthy(0));
      CHECK( target_candidate_healthy(1));
      TargetCandidate cands[2];
      memset(cands, 0, sizeof(cands));
      cands[0].index = 11; cands[0].picnum = 1680; cands[0].extra = 0;
      cands[0].x = 1000; cands[0].y = 0; cands[0].dist2d = 1000;
      cands[1].index = 12; cands[1].picnum = 1680; cands[1].extra = 30;
      cands[1].x = 4000; cands[1].y = 0; cands[1].dist2d = 4000;
      // A healthy one wins even though the unhealthy one is nearer and better centred.
      CHECK(target_pick(cands, 2, 0, 0, 0, TM_CROSSHAIR, 512) == 12);
      // A lone unhealthy one is still picked: never filtered by health, only by the dying rule, which lives in the
      // walk.
      CHECK(target_pick(cands, 1, 0, 0, 0, TM_CROSSHAIR, 512) == 11);
    }

    // Nearest-sector seeding for the out-of-bounds camera.

    // Point-to-segment distance, squared, in doubles: the perpendicular form needs a cross product squared over a
    // length squared, a 2^110 intermediate with Build coordinates; doubles carry it exactly.
    {
      CHECK(nearsect_point_seg_dist2(0, 0, 10, -5, 10, 5) == 100.0);
      CHECK(nearsect_point_seg_dist2(0, 0, -5, 10, 5, 10) == 100.0);
      // ... and outside it, at each end: the endpoint's own distance. A formula that skipped the clamp on t returns the
      // smaller perpendicular distance to the infinite line, failing here.
      CHECK(nearsect_point_seg_dist2(0, 0, 10, 10, 10, 20) == 200.0);   // past the A end
      CHECK(nearsect_point_seg_dist2(0, 0, 10, -20, 10, -10) == 200.0); // past the B end
      CHECK(nearsect_point_seg_dist2(10, 0, 10, -5, 10, 5) == 0.0);
      CHECK(nearsect_point_seg_dist2(10, 5, 10, -5, 10, 5) == 0.0);     // exactly on an endpoint
      // A degenerate wall (both vertices the same point) is its own vertex, not a division by zero: Build maps do
      // contain them.
      CHECK(nearsect_point_seg_dist2(3, 4, 0, 0, 0, 0) == 25.0);
      CHECK(nearsect_point_seg_dist2(0, 0, 7, 0, 7, 0) == 49.0);
      CHECK(nearsect_point_seg_dist2(0, 0, 10, -5, 10, 5)
         == nearsect_point_seg_dist2(0, 0, 10, 5, 10, -5));
      CHECK(nearsect_point_seg_dist2(1000000, 0, 1000300, -400, 1000300, 400) == 90000.0);
    }
    {
      // Build's z grows downward: ceilingz is the smaller number, "inside" is ceilingz <= z <= floorz. Backwards, the
      // tie-break would prefer the wrong sector, invisible with one sector fixture.
      CHECK( nearsect_z_inside(0, -1000, 1000));
      CHECK( nearsect_z_inside(-1000, -1000, 1000));   // on the ceiling counts
      CHECK( nearsect_z_inside(1000, -1000, 1000));    // on the floor counts
      CHECK(!nearsect_z_inside(-1001, -1000, 1000));
      CHECK(!nearsect_z_inside(1001, -1000, 1000));
      CHECK( nearsect_z_inside(500, 500, 500));
      CHECK(!nearsect_z_inside(499, 500, 500));
      // Inverted data (floor above ceiling) contains nothing, not everything: corruption must not win a tie.
      CHECK(!nearsect_z_inside(0, 1000, -1000));
    }
    {
      // The search over a synthetic two-room map: two square rooms of four walls well apart on x, the camera left of
      // room 0; sectors are stacked in z for the tie-break. Room 0: x 0..100, y 0..100, walls 0..3. Room 1: x
      // 1000..1100, walls 4..7.
      NearSectWall w[8] = {
        {   0,   0, 1 }, { 100,   0, 2 }, { 100, 100, 3 }, {   0, 100, 0 },
        {1000,   0, 5 }, {1100,   0, 6 }, {1100, 100, 7 }, {1000, 100, 4 },
      };
      NearSectSector s[2] = {
        { 0, 4, -2000, 2000 },
        { 4, 4, -2000, 2000 },
      };
      CHECK(nearest_sector(-50, 50, 0, w, s, 8, 2) == 0);
      CHECK(nearest_sector(1200, 50, 0, w, s, 8, 2) == 1);
      CHECK(nearest_sector(900, 50, 0, w, s, 8, 2) == 1);
      CHECK(nearest_sector(50, 50, 0, w, s, 8, 2) == 0);
      // Z is not a distance term: a camera far above the map still picks the sector nearest in 2D.
      CHECK(nearest_sector(-50, 50, -999999, w, s, 8, 2) == 0);
      CHECK(nearest_sector(1200, 50, 999999, w, s, 8, 2) == 1);

      // Tie-break: camera exactly halfway between the rooms' facing walls (x = 550), sectors given different z spans so
      // only z separates them.
      NearSectSector tie[2] = {
        { 0, 4, -2000, -1000 },     // room 0 spans z -2000..-1000
        { 4, 4,  1000,  2000 },     // room 1 spans z  1000.. 2000
      };
      CHECK(nearest_sector(550, 50, -1500, w, tie, 8, 2) == 0);   // z inside room 0
      CHECK(nearest_sector(550, 50,  1500, w, tie, 8, 2) == 1);   // z inside room 1
      CHECK(nearest_sector(550, 50, 0, w, tie, 8, 2) == 0);
      CHECK(nearest_sector(550, 50, 0, w, tie, 8, 2) == 0);
      // A nearer sector still beats an inside-z one: z is a tie-break, not a term. Room 1 contains the z; room 0 is
      // nearer and wins.
      CHECK(nearest_sector(-50, 50, 1500, w, tie, 8, 2) == 0);

      CHECK(nearest_sector(0, 0, 0, 0, s, 8, 2) == -1);        // null walls
      CHECK(nearest_sector(0, 0, 0, w, 0, 8, 2) == -1);        // null sectors
      CHECK(nearest_sector(0, 0, 0, w, s, 0, 2) == -1);        // no walls
      CHECK(nearest_sector(0, 0, 0, w, s, 8, 0) == -1);        // no sectors
      CHECK(nearest_sector(0, 0, 0, w, s, -5, 2) == -1);
      CHECK(nearest_sector(0, 0, 0, w, s, 8, -5) == -1);

      // A sector whose wall range is garbage is skipped, not read: a corrupt short out of live map memory could walk
      // the array.
      NearSectSector bad[2] = {
        { 9999, 4, -2000, 2000 },   // wallptr past the array
        { 4, 4, -2000, 2000 },      // the good one
      };
      CHECK(nearest_sector(-50, 50, 0, w, bad, 8, 2) == 1);    // the good sector, not 0, not -1
      NearSectSector bad2[1] = { { 0, 9999, -2000, 2000 } };   // wallnum past the array
      CHECK(nearest_sector(-50, 50, 0, w, bad2, 8, 1) == -1);
      NearSectSector bad3[1] = { { 0, 0, -2000, 2000 } };      // a sector with no walls
      CHECK(nearest_sector(-50, 50, 0, w, bad3, 8, 1) == -1);

      // A count past the static array is refused, not clamped: clamping would walk the caller's smaller array up to
      // MAX_SECTORS entries.
      CHECK(nearest_sector(-50, 50, 0, w, s, 8, 999999) == -1);
      CHECK(nearest_sector(-50, 50, 0, w, s, 999999, 2) == -1);

      // A point2 outside its own sector's loop is skipped, not followed: wall_range_ok bounds the sector, this bounds
      // the link in it.
      NearSectWall crosslink[8] = {
        {   0,   0, 7 }, { 100,   0, 2 }, { 100, 100, 3 }, {   0, 100, 0 },
        {1000,   0, 5 }, {1100,   0, 6 }, {1100, 100, 7 }, {1000, 100, 4 },
      };
      CHECK(nearest_sector(-50, 50, 0, crosslink, s, 8, 2) == 0);   // still answers

      // A cross-linked point2 that would win if followed: wall 4 (room 1) links back into room 0's wall 3. Guarded,
      // that edge is skipped and the tie falls to sector 0; unguarded, the false segment from (550,50) clamps closer
      // and wins for sector 1.
      NearSectWall crosslink2[8] = {
        {   0,   0, 1 }, { 100,   0, 2 }, { 100, 100, 3 }, {   0, 100, 0 },
        {1000,   0, 3 }, {1100,   0, 6 }, {1100, 100, 7 }, {1000, 100, 4 },
      };
      CHECK(nearest_sector(550, 50, 0, crosslink2, s, 8, 2) == 0);

      // A point2 that is not an index into the array at all (a corrupt short). Guarded, wall 0's edge is skipped;
      // unguarded this reads walls[9999] off an 8-element stack array.
      NearSectWall oob[8] = {
        {   0,   0, 9999 }, { 100,   0, 2 }, { 100, 100, 3 }, {   0, 100, 0 },
        {1000,   0, 5 },    {1100,   0, 6 }, {1100, 100, 7 }, {1000, 100, 4 },
      };
      CHECK(nearest_sector(-50, 50, 0, oob, s, 8, 2) == 0);
    }
    {
      // Config: freecamnearest.
      const int saved = g_cfg.freecamnearest;
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.freecamnearest == 0);       // every feature defaults off
      const char* t = "test_freecamnearest_5b2.ini";
      g_cfg.freecamnearest = 1;
      CHECK(config_save(t));
      g_cfg.freecamnearest = 0;               // poisoned before the reload
      config_load(t);
      CHECK(g_cfg.freecamnearest == 1);
      {
        FILE* f = nullptr;
        CHECK(fopen_s(&f, t, "w") == 0 && f);
        if (f) { fputs("freecamnearest=-7\n", f); fclose(f); }
        config_load(t);
        CHECK(g_cfg.freecamnearest == 1);     // any nonzero reads back as exactly 1
      }
      remove(t);
      g_cfg.freecamnearest = saved;
    }
    {
      const int savedTab = menu_active_tab();
      const int savedOn = g_cfg.freecamon, savedNear = g_cfg.freecamnearest;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      g_cfg.freecamon = 1;
      g_cfg.freecamnearest = 0;
      menu_set_tab(TAB_VIEW);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const CanvasOp* rNear = canvas_record_find_text("Nearest sector");
      const CanvasOp* rCull = canvas_record_find_text("Disable culling");
      CHECK(rNear);
      if (rNear && rCull) CHECK(rCull->y < rNear->y);
      // The hint says this has no effect while culling is on (a player with culling disabled saw no effect and no
      // explanation), read off the row's own tooltip.
      CHECK(streq(tooltip_for_row("Nearest sector", 3440, 1440),
                  "Out of bounds. No effect while Disable culling is on."));
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      }
      // The View tab's last row can be silently dropped at 1024x768 (a row without room draws nothing, so tab_text_fits
      // cannot see it). Checks the row is recorded at both sizes, off "Screenshot mode", this tab's real last row.
      CHECK(tab_bottom_row_fits(TAB_VIEW, "Screenshot mode", 3440, 1440));
      CHECK(tab_bottom_row_fits(TAB_VIEW, "Screenshot mode", 1024, 768));
      g_cfg.freecamon = savedOn; g_cfg.freecamnearest = savedNear;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // Debug log toggle and state logging.

    {
      // The switch table: every 0/1 field in Config, nothing else. Ranges (menukey, uiscale, fov, espdrawdist, aim* and
      // mouseaim* sliders, silentmaxangle, freecamspeed) are absent: a dragged slider would write a line per pixel.
      // aimtarget and aimpoint pick a mode but are 0/1 fields, so they are here. Count is 37.
      CHECK(config_switch_count() == 38);
      // Every name is non-null, non-empty and unique: a duplicate makes two switches log the same line, invisible by
      // eye.
      bool allNamed = true, anyDup = false;
      for (int i = 0; i < config_switch_count(); i++) {
        const char* n = config_switch_name(i);
        if (!n || !n[0]) allNamed = false;
        for (int j = i + 1; j < config_switch_count(); j++)
          if (streq(n, config_switch_name(j))) anyDup = true;
      }
      CHECK(allNamed);
      CHECK(!anyDup);
      CHECK(config_switch_name(-1) == nullptr);
      CHECK(config_switch_name(config_switch_count()) == nullptr);
      // Names are the ini keys, spot-checked against config_save's spellings.
      bool sawDebug = false, sawEnemies = false, sawExpander = false, sawNearest = false;
      for (int i = 0; i < config_switch_count(); i++) {
        const char* n = config_switch_name(i);
        if (streq(n, "debuglog"))       sawDebug = true;
        if (streq(n, "espenemies"))     sawEnemies = true;
        if (streq(n, "accexpander"))    sawExpander = true;
        if (streq(n, "freecamnearest")) sawNearest = true;
        CHECK(!streq(n, "uiscale") && !streq(n, "fov") && !streq(n, "espdrawdist"));
        CHECK(!streq(n, "menukey") && !streq(n, "freecamspeed") && !streq(n, "freecamsens"));
        CHECK(!streq(n, "mouseaimsens") && !streq(n, "mouseaimvscale"));
      }
      CHECK(sawDebug && sawEnemies && sawExpander && sawNearest);
      // The VALUES are read from the struct handed in, not g_cfg: a table that ignored its parameter would report the
      // live config for a snapshot.
      Config a; memset(&a, 0, sizeof(a));
      Config b; memset(&b, 0, sizeof(b));
      for (int i = 0; i < config_switch_count(); i++) {
        CHECK(config_switch_value(a, i) == 0);
        CHECK(config_switch_value(b, i) == 0);
      }
      CHECK(config_switch_value(a, -1) == 0);
      CHECK(config_switch_value(a, config_switch_count()) == 0);
      // Every switch is reachable from the table: set the struct's own field and exactly one entry changes. Driven
      // through the STRUCT, not a second list of names, so an entry pointing at the wrong member fails rather than
      // coincides.
      b.espenemies = 1;
      int changedCount = 0, changedIdx = -1;
      for (int i = 0; i < config_switch_count(); i++)
        if (config_switch_value(b, i) != config_switch_value(a, i)) { changedCount++; changedIdx = i; }
      CHECK(changedCount == 1);
      CHECK(streq(config_switch_name(changedIdx), "espenemies"));
      b.espenemies = 0; b.accshotgun = 1;
      changedCount = 0; changedIdx = -1;
      for (int i = 0; i < config_switch_count(); i++)
        if (config_switch_value(b, i) != config_switch_value(a, i)) { changedCount++; changedIdx = i; }
      CHECK(changedCount == 1);
      CHECK(streq(config_switch_name(changedIdx), "accshotgun"));
      // Any nonzero value reads back as exactly 1, so a hand-edited ini cannot put a 7 in a log line that claims to be
      // a switch.
      b.accshotgun = 7;
      CHECK(config_switch_value(b, changedIdx) == 1);
      // The renamed entry, by NAME and by FIELD. The count above is unchanged, so a rename that also dropped or
      // duplicated an entry would slip past it; this says the entry that was `triggervolume` is `secretvolume` and
      // points at the renamed member.
      b.accshotgun = 0; b.secretvolume = 1;
      changedCount = 0; changedIdx = -1;
      for (int i = 0; i < config_switch_count(); i++)
        if (config_switch_value(b, i) != config_switch_value(a, i)) { changedCount++; changedIdx = i; }
      CHECK(changedCount == 1);
      CHECK(streq(config_switch_name(changedIdx), "secretvolume"));
      b.secretvolume = 0;
      // And the OLD spelling is gone from the table, so a log line can never name a key config_save no longer writes.
      for (int i = 0; i < config_switch_count(); i++)
        CHECK(!streq(config_switch_name(i), "triggervolume"));
    }
    {
      // `triggervolume` -> `secretvolume` migration, the shape of `aimbutton` -> `aimbind` (tb_0066): the legacy key is
      // still read, the new key wins wherever both appear, and only the new one is written back.
      const char* t = "test_secretvolume_fwb.ini";
      FILE* f = nullptr;
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.secretvolume == 0);
      g_cfg.secretvolume = 1;
      CHECK(config_save(t));
      g_cfg.secretvolume = 0;                   // poisoned before the reload
      config_load(t);
      CHECK(g_cfg.secretvolume == 1);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("triggervolume=1\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.secretvolume == 1);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("triggervolume=0\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.secretvolume == 0);
      // New key wins in both orders.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("secretvolume=1\ntriggervolume=0\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.secretvolume == 1);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("triggervolume=0\nsecretvolume=1\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.secretvolume == 1);
      // Legacy key is never written back.
      g_cfg.secretvolume = 1;
      CHECK(config_save(t));
      {
        char line[CONFIG_LINE_BUFSIZE];
        bool sawOld = false, sawNew = false;
        CHECK(fopen_s(&f, t, "r") == 0 && f);
        if (f) {
          while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "triggervolume=")) sawOld = true;
            if (strstr(line, "secretvolume="))  sawNew = true;
          }
          fclose(f);
        }
        CHECK(sawNew);
        CHECK(!sawOld);
      }
      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));
    }
    {
      char buf[CONFIG_STATE_LINE_CAP];
      CHECK(streq(config_state_line("espenemies", 0, 1, buf, sizeof(buf)),
                  "state: espenemies 0 -> 1"));
      CHECK(streq(config_state_line("silenton", 1, 0, buf, sizeof(buf)),
                  "state: silenton 1 -> 0"));
      // No em dash; the arrow is the two-character ASCII form.
      CHECK(strstr(buf, "->") != nullptr);
      CHECK(strstr(buf, "\xE2\x80\x94") == nullptr);
      // Degenerate inputs produce an empty string rather than writing anywhere.
      CHECK(streq(config_state_line(0, 0, 1, buf, sizeof(buf)), ""));
      CHECK(streq(config_state_line("x", 0, 1, 0, 8), ""));
      char tiny[1];
      CHECK(streq(config_state_line("espenemies", 0, 1, tiny, 0), ""));
      // The cap really does hold the longest name this table can produce.
      int worst = 0;
      for (int i = 0; i < config_switch_count(); i++) {
        char b2[CONFIG_STATE_LINE_CAP];
        config_state_line(config_switch_name(i), 0, 1, b2, sizeof(b2));
        if ((int)strlen(b2) > worst) worst = (int)strlen(b2);
      }
      CHECK(worst < CONFIG_STATE_LINE_CAP - 1);   // never truncated
    }
    {
      Config prev; memset(&prev, 0, sizeof(prev));
      Config cur = prev;
      int idx[32];
      CHECK(config_state_diff(prev, cur, idx, 32) == 0);      // nothing changed
      cur.aimon = 1;
      CHECK(config_state_diff(prev, cur, idx, 32) == 1);
      CHECK(streq(config_switch_name(idx[0]), "aimon"));
      cur.silenton = 1;
      CHECK(config_state_diff(prev, cur, idx, 32) == 2);
      Config on; memset(&on, 0, sizeof(on));
      on.espitems = 1;
      Config off = on; off.espitems = 0;
      CHECK(config_state_diff(on, off, idx, 32) == 1);
      CHECK(streq(config_switch_name(idx[0]), "espitems"));
      // A 7 and a 1 are the same state: diff compares the normalised value.
      Config seven = on; seven.espitems = 7;
      CHECK(config_state_diff(on, seven, idx, 32) == 0);
      // Refusals: a null or zero-capacity out array returns 0 and writes nothing.
      CHECK(config_state_diff(prev, cur, 0, 32) == 0);
      CHECK(config_state_diff(prev, cur, idx, 0) == 0);
      // A capacity smaller than the number of changes reports what it could fit and never writes past the array.
      Config all; memset(&all, 0, sizeof(all));
      Config flipped = all;
      flipped.aimon = 1; flipped.silenton = 1; flipped.espitems = 1;
      int small[2] = { -1, -1 };
      CHECK(config_state_diff(all, flipped, small, 2) == 2);
      CHECK(small[0] >= 0 && small[1] >= 0);
    }
    {
      // Lines really are written, through the log recorder (the log shim once had empty bodies, making LOGI calls
      // review-only).
      Config prev; memset(&prev, 0, sizeof(prev));
      Config cur = prev;
      cur.espenemies = 1;
      cur.freecamon = 1;
      log_record_begin();
      config_log_state_changes(prev, cur);
      log_record_end();
      CHECK(!log_record_overflowed());
      CHECK(log_record_count() == 2);
      CHECK(log_record_find("state: espenemies 0 -> 1") != nullptr);
      CHECK(log_record_find("state: freecamon 0 -> 1") != nullptr);
      // Nothing changed, nothing logged: the log must not fill with lines every frame the config happens to be saved.
      log_record_begin();
      config_log_state_changes(cur, cur);
      log_record_end();
      CHECK(log_record_count() == 0);
      Config ranged = cur; ranged.fov = 110; ranged.uiscale = 150;
      log_record_begin();
      config_log_state_changes(cur, ranged);
      log_record_end();
      CHECK(log_record_count() == 0);
    }
    {
      // The debuglog OFF transition must log its own state line before the log is disabled, or "state: debuglog 1 -> 0"
      // is written into an already-disabled log and lost (log_write gates on the enabled flag, as the real log.cpp
      // does).
      Config onCfg; memset(&onCfg, 0, sizeof(onCfg));
      onCfg.debuglog = 1;
      Config offCfg = onCfg;
      offCfg.debuglog = 0;
      // Correct order: log the diff while still enabled, THEN disable. The line survives.
      log_set_enabled(true);
      log_record_begin();
      config_log_state_changes(onCfg, offCfg);
      log_set_enabled(false);
      log_record_end();
      CHECK(!log_record_overflowed());
      CHECK(log_record_find("state: debuglog 1 -> 0") != nullptr);
      // Wrong order: disable first, then log the same diff; the line never reaches the recorder.
      log_record_begin();
      log_set_enabled(false);
      config_log_state_changes(onCfg, offCfg);
      log_record_end();
      CHECK(log_record_find("state: debuglog 1 -> 0") == nullptr);
      log_set_enabled(true);   // restore, so later checks in this file are unaffected
    }
    {
      // Settings tab's "Debug log" row (in the DIAGNOSTICS group).
      const int savedTab = menu_active_tab();
      const int savedDbg = g_cfg.debuglog;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      g_cfg.debuglog = 0;
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const CanvasOp* rDbg = canvas_record_find_text("Debug log");
      const CanvasOp* gDiag = canvas_record_find_text("DIAGNOSTICS");
      CHECK(rDbg && gDiag);
      if (rDbg && gDiag) CHECK(gDiag->y < rDbg->y);
      // Always live, unlike a camera sub-row: not a sub-option of anything.
      const CanvasOp* rScale = canvas_record_find_text("UI scale");
      if (rDbg && rScale) CHECK(rScale->y < rDbg->y);
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
      }
      // Menu's own log lines, pinned here since input.cpp cannot link into this suite.
      CHECK(streq(MENU_LOG_OPEN, "menu: open"));
      CHECK(streq(MENU_LOG_CLOSE, "menu: close"));
      CHECK(!streq(MENU_LOG_OPEN, MENU_LOG_CLOSE));
      g_cfg.debuglog = savedDbg;
      menu_set_tab(savedTab);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }
  }

// Breakables split: espbreakables round trip, the gating rule (which switch a wall mark answers to), and the two
// Visuals rows drawn with their hints.
void tb_0129_breakables_split() {
  {
    // Breakable walls answer to espbreakables, switch walls to espswitches: the render-time filter esp.cpp applies per
    // wall mark, split out of esp_world_should_scan's OR.
    CHECK( esp_wall_mark_wanted(ESP_WKIND_BREAKABLE, 1, 0));
    CHECK(!esp_wall_mark_wanted(ESP_WKIND_BREAKABLE, 0, 1));   // switches on, breakables off
    CHECK(!esp_wall_mark_wanted(ESP_WKIND_SWITCH, 1, 0));      // breakables on, switches off
    CHECK( esp_wall_mark_wanted(ESP_WKIND_SWITCH, 0, 1));
  }
  {
    const char* t = "test_esp_rank18_breakables.ini";
    FILE* f = nullptr;
    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nespexplodables=1\nespbreakables=0\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.espexplodables == 1);
    CHECK(g_cfg.espbreakables == 0);
    CHECK(config_save(t));
    config_load(t);
    CHECK(g_cfg.espexplodables == 1);
    CHECK(g_cfg.espbreakables == 0);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nespexplodables=0\nespbreakables=1\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.espexplodables == 0);
    CHECK(g_cfg.espbreakables == 1);
    CHECK(config_save(t));
    config_load(t);
    CHECK(g_cfg.espexplodables == 0);
    CHECK(g_cfg.espbreakables == 1);

    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nespbreakables=7\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.espbreakables == 1);
    fopen_s(&f, t, "w");
    fputs("[nukepanel]\nespbreakables=-3\n", f);
    fclose(f);
    config_load(t);
    CHECK(g_cfg.espbreakables == 1);

    remove(t);
    config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults in place
  }
  {
    const int savedTab = menu_active_tab();
    const VisState saved = vis_snapshot();
    bool multi = false;
    const VisState boff = { 0, 1, 1, 1, 1, 0, 0, 6144, 0, 0, 1, 0, 0, 0, 0 };
    const int reached = vis_sweep(3440, 1440, boff, false, &multi);
    CHECK((reached & VF_BREAKABLES) != 0);
    CHECK(!multi);
    vis_restore(saved);

    // The two rows, by label (always drawn), and their hints, which only show on hover (tooltip_for_row, as every other
    // hint pin).
    menu_set_tab(TAB_VISUALS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    CHECK(canvas_record_find_text("Explodables") != nullptr);
    CHECK(canvas_record_find_text("Breakables") != nullptr);
    CHECK(streq(tooltip_for_row("Explodables", 3440, 1440), "Cracks that blow up when shot"));
    CHECK(streq(tooltip_for_row("Breakables", 3440, 1440), "Glass and props the engine breaks"));
    menu_set_tab(savedTab);
  }
}

void tb_0066_aimbind_default_round_trip_aimbutton() {
    // aimbind: default, round trip, aimbutton migration.
    const char* t = "test_aimbind_5b4.ini";
    config_load(tb_path("Z:|does|not|exist.ini"));
    // Default is the same physical button as before: Mouse 5, outer thumb.
    CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);
    CHECK(bind_mouse_sdl_button(g_cfg.aimbind) == MOUSE_BUTTON_MAX);
    g_cfg.aimbind = 0x71;                    // F2, a keyboard key on a mouse row
    CHECK(config_save(t));
    g_cfg.aimbind = 0;                       // poisoned before the reload
    config_load(t);
    CHECK(g_cfg.aimbind == 0x71);
    remove(t);
    // Migration: legacy `aimbutton` is in SDL BUTTON space (5 = Mouse 5); read straight into aimbind it would mean VK
    // 0x05 = Mouse 4, moving every binding by one button. Read through bind_from_sdl_mouse instead.
    {
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=5\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);              // Mouse 5, not Mouse 4
      CHECK(bind_mouse_sdl_button(g_cfg.aimbind) == 5);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=4\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.aimbind == VK_BIND_XBUTTON1);
      // An out-of-range legacy value leaves the default rather than binding 0.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=99\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);
      // A legacy `aimbutton=1` is not migrated: it resets to the default and logs one line, since carrying it into VK
      // space gave a row reading "Unbound" over a hold left click really did satisfy.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=1\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(!log_record_overflowed());
      CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);
      CHECK(bind_mouse_sdl_button(g_cfg.aimbind) == MOUSE_BUTTON_MAX);
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) != nullptr);
      CHECK(streq(CONFIG_LOG_AIMBUTTON1_RESET,
                  "aim button Mouse 1 is no longer bindable, reset to Mouse 5"));
      // Said only for that value: 2..5 and an out-of-range value migrate silently.
      for (int b = MOUSE_BUTTON_MIN + 1; b <= MOUSE_BUTTON_MAX; b++) {
        char body[32];
        sprintf_s(body, "aimbutton=%d\n", b);
        CHECK(fopen_s(&f, t, "w") == 0 && f);
        if (f) { fputs(body, f); fclose(f); }
        log_record_begin();
        config_load(t);
        log_record_end();
        CHECK(g_cfg.aimbind == bind_from_sdl_mouse(b));
        CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) == nullptr);
      }
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=99\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) == nullptr);
      // Not logged when an `aimbind` in the same file supersedes it either.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=1\naimbind=113\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(g_cfg.aimbind == 0x71);
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) == nullptr);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbind=113\naimbutton=1\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(g_cfg.aimbind == 0x71);
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) == nullptr);
      // A later legacy line still decides: the last `aimbutton` before any `aimbind` is what migrates.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=3\naimbutton=1\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) != nullptr);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=1\naimbutton=3\n", f); fclose(f); }
      log_record_begin();
      config_load(t);
      log_record_end();
      CHECK(g_cfg.aimbind == VK_BIND_RBUTTON);
      CHECK(log_record_find(CONFIG_LOG_AIMBUTTON1_RESET) == nullptr);
      // A file holding both keys: the new one wins wherever it appears.
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("aimbutton=2\naimbind=113\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.aimbind == 0x71);
      remove(t);
    }
    // Legacy key is never written back: config_save emits aimbind only.
    g_cfg.aimbind = VK_BIND_XBUTTON2;
    CHECK(config_save(t));
    {
      char line[CONFIG_LINE_BUFSIZE];
      bool sawOld = false, sawNew = false;
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "r") == 0 && f);
      if (f) {
        while (fgets(line, sizeof(line), f)) {
          if (strstr(line, "aimbutton=")) sawOld = true;
          if (strstr(line, "aimbind="))   sawNew = true;
        }
        fclose(f);
      }
      CHECK(sawNew);
      CHECK(!sawOld);
    }
    remove(t);
  }


void tb_0067_game_s_own_cfg() {
    // The game's own cfg: names read from the game's exe, driven by a sample cfg shaped like the real one.
    CHECK(streq(gamecfg_key_name(0x2D), "Insert"));
    CHECK(streq(gamecfg_key_name(0x2E), "Delete"));
    CHECK(streq(gamecfg_key_name(0x24), "Home"));
    CHECK(streq(gamecfg_key_name(0x23), "End"));
    CHECK(streq(gamecfg_key_name(0x21), "PgUp"));
    CHECK(streq(gamecfg_key_name(0x22), "PgDown"));
    CHECK(streq(gamecfg_key_name(0x09), "Tab"));
    CHECK(streq(gamecfg_key_name(0xC0), "~"));       // NOT a backtick
    CHECK(streq(gamecfg_key_name(0x54), "T"));
    CHECK(streq(gamecfg_key_name(0x56), "V"));
    CHECK(streq(gamecfg_key_name(0x70), "F1"));
    CHECK(streq(gamecfg_key_name(0x7B), "F12"));
    CHECK(streq(gamecfg_key_name(VK_BIND_RBUTTON), "Mouse 2"));
    CHECK(streq(gamecfg_key_name(VK_BIND_XBUTTON1), "Mouse 4"));
    // The game's own typo, kept verbatim: MOUSE 5 is uppercase in this build.
    CHECK(streq(gamecfg_key_name(VK_BIND_XBUTTON2), "MOUSE 5"));
    CHECK(streq(gamecfg_key_name(0x41), "A"));
    CHECK(gamecfg_key_name(0) == nullptr);
    // Every binding this panel can produce has a name in the game's table (gamecfg_key_name reads the game's exe,
    // gamecfg.h).
    for (int vk = 0; vk <= 255; vk++) {
      int sc = -1;
      if (vk_to_sdl_scancode(vk, &sc)) CHECK(gamecfg_key_name(vk) != nullptr);
    }
    // Every code the game names is one this panel can bind, Mouse 1 aside (the game binds it to Fire; this DLL never
    // does, bind.h).
    for (int vk = 0; vk <= 255; vk++) {
      if (vk == VK_BIND_LBUTTON) continue;
      int sc = -1;
      if (gamecfg_key_name(vk) != nullptr)
        CHECK(bind_is_mouse(vk) || vk_to_sdl_scancode(vk, &sc));
    }
    // The four codes this DLL ships bound are all named, so the warning is live for every default the player starts
    // with.
    CHECK(gamecfg_key_name(0x2D) != nullptr);                 // Insert, the panel key
    CHECK(gamecfg_key_name(0x56) != nullptr);                 // V, the camera toggle
    CHECK(gamecfg_key_name(0x54) != nullptr);                 // T, the teleport
    CHECK(gamecfg_key_name(VK_BIND_XBUTTON2) != nullptr);     // Mouse 5, the aim hold
    for (int b = MOUSE_BUTTON_MIN + 1; b <= MOUSE_BUTTON_MAX; b++)
      CHECK(gamecfg_key_name(bind_from_sdl_mouse(b)) != nullptr);
    // The game still binds Mouse 1 (Fire), so its name stays in this table though bind_from_sdl_mouse no longer
    // produces the code.
    CHECK(streq(gamecfg_key_name(VK_BIND_LBUTTON), "Mouse 1"));
    CHECK(!gamecfg_key_bound("[KeyDefinitions]\nFire = \"Mouse 1\" \"None\"\n",
                             bind_from_sdl_mouse(MOUSE_BUTTON_MIN)));

    static const char* SAMPLE =
      "[Controls]\n"
      "MouseButton0 = \"Move_Forward\"\n"
      "MouseSensitivity = 5767\n"
      "\n"
      "[KeyDefinitions]\n"
      "Move_Forward = \"W\" \"None\"\n"
      "Fire = \"Mouse 1\" \"GP RT\"\n"
      "Map = \"Tab\" \"GP Back\"\n"
      "Quick_Kick = \"Mouse 4\" \"GP LT\"\n"
      "Quick_Load = \"F9\" \"None\"\n"
      "SendMessage = \"T\" \"None\"\n"
      "Aim_Up = \"Home\" \"None\"\n"
      "Steroids = \"R\" \"MOUSE 5\"\n"
      "Turn_Left = \"None\" \"None\"\n"
      "\n"
      "[Comm Setup]\n"
      "PlayerName = \"Insert\"\n";
    CHECK( gamecfg_key_bound(SAMPLE, 0x09));               // Tab, primary
    CHECK( gamecfg_key_bound(SAMPLE, 0x78));               // F9, primary
    CHECK( gamecfg_key_bound(SAMPLE, 0x54));               // T, primary
    CHECK( gamecfg_key_bound(SAMPLE, 0x24));               // Home, primary
    CHECK( gamecfg_key_bound(SAMPLE, VK_BIND_XBUTTON1));   // Mouse 4
    CHECK( gamecfg_key_bound(SAMPLE, VK_BIND_XBUTTON2));   // MOUSE 5, secondary
    CHECK(!gamecfg_key_bound(SAMPLE, 0x71));               // F2 is not in this cfg
    CHECK(!gamecfg_key_bound(SAMPLE, 0x56));               // V
    CHECK(!gamecfg_key_bound(SAMPLE, VK_BIND_RBUTTON));    // Mouse 2
    CHECK(!gamecfg_key_bound(SAMPLE, 0x22));               // PgDown
    // Only [KeyDefinitions] is scanned: "Insert" appears in [Comm Setup] as a player name, and action names live in
    // [Controls].
    CHECK(!gamecfg_key_bound(SAMPLE, 0x2D));               // Insert, but not in the section
    // A substring is not a match: "T" must not match "GP RT", "F1" not "F12".
    static const char* TRICKY =
      "[KeyDefinitions]\n"
      "A = \"F12\" \"None\"\n"
      "B = \"None\" \"GP RT\"\n";
    CHECK( gamecfg_key_bound(TRICKY, 0x7B));               // F12 itself
    CHECK(!gamecfg_key_bound(TRICKY, 0x70));               // F1 is not F12
    CHECK(!gamecfg_key_bound(TRICKY, 0x54));               // T is not the T of GP RT
    // Case-insensitive: the game writes MOUSE 5 in capitals, Mouse 4 mixed.
    static const char* CASED = "[KeyDefinitions]\nA = \"mouse 5\" \"tab\"\n";
    CHECK(gamecfg_key_bound(CASED, VK_BIND_XBUTTON2));
    CHECK(gamecfg_key_bound(CASED, 0x09));
    // Degenerate inputs never warn and never read anything.
    CHECK(!gamecfg_key_bound(0, 0x09));
    CHECK(!gamecfg_key_bound("", 0x09));
    CHECK(!gamecfg_key_bound(SAMPLE, 0x41));               // an unnamed code
    CHECK(!gamecfg_key_bound("[Controls]\nx = \"Tab\"\n", 0x09));   // no section at all
    // A section running to end of file with no following header is still scanned to the end.
    CHECK(gamecfg_key_bound("[KeyDefinitions]\nA = \"Tab\" \"None\"\n", 0x09));
    // The header is a whole token, not a prefix: [KeyDefinitionsOld] is not scanned as the real section.
    CHECK(!gamecfg_key_bound("[KeyDefinitionsOld]\nA = \"Tab\"\n", 0x09));
    CHECK(!gamecfg_key_bound("[KeyDefinitions2]\nA = \"Tab\"\n", 0x09));
    CHECK(!gamecfg_key_bound("[KeyDefinitions]Old\nA = \"Tab\"\n", 0x09));
    // Trailing whitespace and a CRLF line ending are still the real section (cfg is read in binary; the game writes
    // CRLF).
    CHECK(gamecfg_key_bound("[KeyDefinitions]\r\nA = \"Tab\"\r\n", 0x09));
    CHECK(gamecfg_key_bound("[KeyDefinitions]  \nA = \"Tab\"\n", 0x09));
  }


void tb_0069_four_rows_panel_cons() {
    // The four rows on the panel.
    const int savedTab = menu_active_tab();
    const int savedMenu = g_cfg.menukey, savedAim = g_cfg.aimbind;
    const int savedCam = g_cfg.freecamkey, savedTp = g_cfg.freecamteleportkey;
    const int savedOn = g_cfg.freecamon, savedAimOn = g_cfg.aimon;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_keycap_cancel();
    menu_gamecfg_load(0);                     // no cfg: no warnings anywhere
    g_cfg.menukey = 0x2D; g_cfg.aimbind = VK_BIND_XBUTTON2;
    g_cfg.freecamkey = 0x56; g_cfg.freecamteleportkey = 0x54;
    g_cfg.freecamon = 1; g_cfg.aimon = 1;

    // All four rows are on the Settings tab, in one HOTKEYS group; every property below is the one the four-row block
    // always checked.
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    CHECK(canvas_record_find_text("Free camera key") != nullptr);
    CHECK(canvas_record_find_text("Teleport key") != nullptr);
    CHECK(canvas_record_find_text("V") != nullptr);          // the camera's readout
    CHECK(canvas_record_find_text("T") != nullptr);          // the teleport's
    CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) == nullptr);   // nothing armed
    CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) == nullptr);    // no cfg loaded

    // Armed: the row's readout becomes the prompt, only that row's.
    menu_keycap_cancel();
    {
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* row = canvas_record_find_text("Free camera key");
      CHECK(row);
      if (row) {
        const UiRect p = menu_panel_rect(3440, 1440);
        bool armedSomewhere = false;
        for (int x = p.x; x < p.x + p.w && !armedSomewhere; x++) {
          menu_keycap_cancel();
          menu_set_tab(TAB_SETTINGS);
          g_ui.mx = x; g_ui.my = row->y; g_ui.down = false; g_ui.pressed = true;
          menu_draw(3440, 1440);
          if (menu_keycap_armed()) armedSomewhere = true;
        }
        CHECK(armedSomewhere);
        // Cursor goes back off the panel: a field under the cursor draws the invitation rather than its key's name.
        g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
        // With that row armed, the prompt is on screen and the camera's own key name is not: exactly one row is in
        // capture.
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) != nullptr);
        CHECK(canvas_record_find_text("V") == nullptr);
        CHECK(canvas_record_find_text("T") != nullptr);      // the teleport row is untouched
        // Binding lands on the armed row and nowhere else.
        menu_keycap_bind(0x71);                              // F2
        CHECK(!menu_keycap_armed());
        CHECK(g_cfg.freecamkey == 0x71);
        CHECK(g_cfg.menukey == 0x2D);
        CHECK(g_cfg.freecamteleportkey == 0x54);
        CHECK(g_cfg.aimbind == VK_BIND_XBUTTON2);
        CHECK(g_cfgDirty);                                   // saved through the existing path
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        CHECK(canvas_record_find_text("F2") != nullptr);
        CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) == nullptr);
        g_ui.mx = -1; g_ui.my = -1; g_ui.pressed = false;
        menu_keycap_cancel();
        CHECK(!menu_keycap_armed());
        CHECK(g_cfg.freecamkey == 0x71);
        const int before = g_cfg.freecamkey;
        menu_keycap_bind(0x72);
        CHECK(g_cfg.freecamkey == before);
      }
    }
    // The "Also bound in the game" note, from a cfg the test writes itself.
    {
      const char* t = "test_gamecfg_5b4.cfg";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) {
        fputs("[KeyDefinitions]\nSave_Menu = \"F2\" \"None\"\n", f);
        fclose(f);
      }
      menu_gamecfg_load(t);
      g_cfg.freecamkey = 0x71;                 // F2, which that cfg binds
      g_cfg.freecamteleportkey = 0x54;         // T, which it does not
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) != nullptr);
      {
        const CanvasOp* note = canvas_record_find_text(MENU_GAMECFG_NOTE);
        const CanvasOp* tog  = canvas_record_find_text("Free camera key");
        const CanvasOp* tp   = canvas_record_find_text("Teleport key");
        CHECK(note && tog && tp);
        if (note && tog && tp) CHECK(tog->y < note->y && note->y < tp->y);
      }
      {
        int notes = 0;
        for (int i = 0; i < canvas_record_count(); i++) {
          const CanvasOp* o = canvas_record_op(i);
          if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_GAMECFG_NOTE)) notes++;
        }
        CHECK(notes == 1);
      }
      g_cfg.freecamkey = 0x56;                 // V
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) == nullptr);
      menu_gamecfg_load(tb_path("Z:|does|not|exist.cfg"));
      g_cfg.freecamkey = 0x71;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) == nullptr);
      remove(t);
      menu_gamecfg_load(0);
    }
    // The note consumes a layout row; this pins the Settings tab's bottom row with two notes showing at once (a real
    // duke3d.cfg can bind both the camera key and the teleport key), drawn at the cap (UI_SCALE_MAX clamps to 80 at
    // 1024x768, 150 at 3440x1440). The View tab is checked too, since it can no longer carry a note.
    {
      const int savedScale = g_cfg.uiscale;
      const char* t = "test_gamecfg_two_notes_5bF.cfg";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) {
        fputs("[KeyDefinitions]\nSave_Menu = \"F2\" \"None\"\nJump = \"T\" \"None\"\n", f);
        fclose(f);
      }
      menu_gamecfg_load(t);
      g_cfg.freecamon = 1;
      g_cfg.freecamkey         = 0x71;   // F2, which the cfg binds
      g_cfg.freecamteleportkey = 0x54;   // T, which it binds too
      g_cfg.uiscale = UI_SCALE_MAX;
      // DIAGNOSTICS/"Debug log" live on the Settings tab; hints are tooltips, so each tab's real last row is the switch
      // that carried one.
      static const char* BOTTOM_ROW = "Screenshot mode";
      static const char* SETTINGS_BOTTOM_ROW = "Debug log";
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      int notes = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_GAMECFG_NOTE)) notes++;
      }
      CHECK(notes == 2);                                     // both rows really are bound
      // Two gamecfg notes still leave room for the Settings tab's real last row, and the View tab, which carries none,
      // still has every camera hint and its own last row.
      CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM_ROW, 1024, 768));
      CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM_ROW, 3440, 1440));
      CHECK(tab_bottom_row_fits(TAB_VIEW, BOTTOM_ROW, 1024, 768));
      CHECK(tab_bottom_row_fits(TAB_VIEW, BOTTOM_ROW, 3440, 1440));
      CHECK(canvas_record_find_text(BOTTOM_ROW) != nullptr);
      menu_gamecfg_load(0);
      remove(t);
      g_cfg.uiscale = savedScale;
    }
    // Nothing on any affected tab runs off the panel edge with the widest key name bound.
    {
      int worst = 0;
      g_cfg.freecamkey = 0x22;                 // "Page down", the widest label
      g_cfg.aimbind = VK_BIND_XBUTTON2;
      CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
      CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
      CHECK(tab_text_fits(TAB_COMBAT, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_COMBAT, 1024, 768, 18, &worst));
    }
    g_cfg.menukey = savedMenu; g_cfg.aimbind = savedAim;
    g_cfg.freecamkey = savedCam; g_cfg.freecamteleportkey = savedTp;
    g_cfg.freecamon = savedOn; g_cfg.aimon = savedAimOn;
    menu_keycap_cancel();
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfgDirty = false;
  }


void tb_0070_view_tab_s_panel_key() {
    // Settings tab's "Panel key" row and the prompt's own fit. The panel key has no parent switch, so it must stay
    // reachable with every feature off (a fresh install's state).
    const int savedTab = menu_active_tab();
    const int savedMenu = g_cfg.menukey, savedOn = g_cfg.freecamon;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_keycap_cancel();
    menu_gamecfg_load(0);
    g_cfg.menukey = 0x2D;
    g_cfg.freecamon = 0;                       // every feature off
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const CanvasOp* rPanel = canvas_record_find_text("Panel key");
    const CanvasOp* gHot   = canvas_record_find_text("HOTKEYS");
    CHECK(rPanel && gHot);
    if (rPanel && gHot) {
      CHECK(gHot->y < rPanel->y);              // inside the HOTKEYS group
      CHECK(canvas_record_find_text("Insert") != nullptr);
      const UiRect p = menu_panel_rect(3440, 1440);
      bool armedSomewhere = false;
      for (int x = p.x; x < p.x + p.w && !armedSomewhere; x++) {
        menu_keycap_cancel();
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = x; g_ui.my = rPanel->y; g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
        if (menu_keycap_armed()) armedSomewhere = true;
      }
      CHECK(armedSomewhere);                   // live with every feature off
      g_ui.down = false; g_ui.pressed = false;
      // The prompt does not run off the panel edge either: it is the widest thing drawn.
      {
        int worst = 0;
        CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
        CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
      }
      // Cancelling while armed: disarms, binding untouched, readout goes back to the bound key's name.
      CHECK(menu_keycap_armed());
      menu_keycap_cancel();
      CHECK(!menu_keycap_armed());
      CHECK(g_cfg.menukey == 0x2D);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Insert") != nullptr);
      CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) == nullptr);
      // close_menu() disarms through menu_keycap_close() (KEYCAP_EV_CLOSE), not the ESCAPE arm; the binding is
      // untouched by either.
      {
        for (int x = p.x; x < p.x + p.w && !menu_keycap_armed(); x++) {
          menu_set_tab(TAB_SETTINGS);
          g_ui.mx = x; g_ui.my = rPanel->y; g_ui.down = false; g_ui.pressed = true;
          menu_draw(3440, 1440);
        }
        CHECK(menu_keycap_armed());
        g_ui.down = false; g_ui.pressed = false;
        menu_keycap_close();
        CHECK(!menu_keycap_armed());
        CHECK(g_cfg.menukey == 0x2D);
        menu_keycap_close();                   // idle: still a no-op
        CHECK(!menu_keycap_armed());
        CHECK(g_cfg.menukey == 0x2D);
      }
      // Re-arming takes a fresh click, not a leftover one.
      for (int x = p.x; x < p.x + p.w && !menu_keycap_armed(); x++) {
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = x; g_ui.my = rPanel->y; g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
      }
      CHECK(menu_keycap_armed());
      g_ui.down = false; g_ui.pressed = false;
      menu_keycap_bind(0x2E);                  // Delete
      CHECK(!menu_keycap_armed());
      CHECK(g_cfg.menukey == 0x2E);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text("Delete") != nullptr);
    }
    // The widened table's names, live under a real bind row. Also pins gamecfg_key_bound's token-exact compare: "W"
    // matches only the whole quoted token, and "KPad 1" must not be mistaken for "1".
    {
      static const char* WIDENED =
          "[KeyDefinitions]\n"
          "Move_Forward = \"W\" \"\"\n"
          "Crouch = \"KPad 1\" \"\"\n"
          "Quick_Save = \"Break\" \"\"\n"
          "Jetpack = \"Space\" \"\"\n";
      CHECK( gamecfg_key_bound(WIDENED, 0x57));   // W, primary
      CHECK(!gamecfg_key_bound(WIDENED, 0x41));   // A: named, but not in this cfg
      CHECK( gamecfg_key_bound(WIDENED, 0x61));   // KPad 1 (VK_NUMPAD1)
      CHECK(!gamecfg_key_bound(WIDENED, 0x31));   // '1' is not "KPad 1"
      CHECK( gamecfg_key_bound(WIDENED, 0x13));   // Break (VK_PAUSE)
      CHECK( gamecfg_key_bound(WIDENED, 0x20));   // Space
      static const char* JUMP_ON_A = "[KeyDefinitions]\nJump = \"A\" \"\"\n";
      CHECK(gamecfg_key_bound(JUMP_ON_A, 0x41));

      const char* t = "test_gamecfg_widened_panelkey.cfg";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs(WIDENED, f); fclose(f); }
      menu_gamecfg_load(t);
      g_cfg.menukey = 0x57;                       // W, which this cfg binds
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* wRow  = canvas_record_find_text("Panel key");
      const CanvasOp* wNote = canvas_record_find_text(MENU_GAMECFG_NOTE);
      CHECK(wRow && wNote);
      if (wRow && wNote) CHECK(wRow->y < wNote->y);   // under THAT row
      remove(t);
      menu_gamecfg_load(0);
    }
    g_cfg.menukey = savedMenu; g_cfg.freecamon = savedOn;
    menu_keycap_cancel();
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfgDirty = false;
  }




// UI scale.

// Scale arithmetic. The bound is 75..200, not the old 50..200: at 50% on a 620x900 panel the row hit boxes are ten
// pixels tall, unusable.
void tb_0072_phase_5b_task_5_ui() {
    CHECK(UI_SCALE_MIN == 75);
    CHECK(UI_SCALE_MAX == 200);
    CHECK(UI_SCALE_DEFAULT == 100);
    CHECK(ui_scale_clamped(100) == 100);
    CHECK(ui_scale_clamped(75) == 75);
    CHECK(ui_scale_clamped(200) == 200);
    CHECK(ui_scale_clamped(74) == UI_SCALE_MIN);
    CHECK(ui_scale_clamped(50) == UI_SCALE_MIN);     // the OLD floor is now clamped up
    CHECK(ui_scale_clamped(0) == UI_SCALE_MIN);
    CHECK(ui_scale_clamped(-1) == UI_SCALE_MIN);
    CHECK(ui_scale_clamped(201) == UI_SCALE_MAX);
    CHECK(ui_scale_clamped(999999) == UI_SCALE_MAX);

    // 100 is the exact identity, at every value including saturation extremes: everything already shipped is drawn at
    // 100.
    for (int px = -2000; px <= 2000; px += 7) CHECK(ui_scale_px(px, 100) == px);
    CHECK(ui_scale_px(2147483647, 100) == 2147483647);
    CHECK(ui_scale_px(-2147483647 - 1, 100) == -2147483647 - 1);
    for (int px = -2000; px <= 2000; px += 7) CHECK(ui_unscale_px(px, 100) == px);

    // Rounding rule: half away from zero, so a 1-pixel rule at 150% is 2 pixels rather than vanishing, and the signs
    // are symmetric.
    CHECK(ui_scale_px(10, 200) == 20);
    CHECK(ui_scale_px(10, 75) == 8);            // 750 + 50 = 800 / 100
    CHECK(ui_scale_px(1, 150) == 2);            // 150 + 50 = 200 / 100, never 1
    CHECK(ui_scale_px(1, 149) == 1);            // 149 + 50 = 199 / 100
    CHECK(ui_scale_px(-10, 75) == -8);
    CHECK(ui_scale_px(-1, 150) == -2);
    CHECK(ui_scale_px(0, 200) == 0);
    CHECK(ui_scale_px(0, 75) == 0);
    // A one-pixel line never disappears at any legal scale (bevels and sub-row rules are all 1px).
    for (int pct = UI_SCALE_MIN; pct <= UI_SCALE_MAX; pct++) CHECK(ui_scale_px(1, pct) >= 1);
    CHECK(ui_scale_px(10, 999999) == ui_scale_px(10, UI_SCALE_MAX));
    CHECK(ui_scale_px(10, -5) == ui_scale_px(10, UI_SCALE_MIN));
    // Widened before the multiply: a saturated coordinate times two must not wrap negative.
    CHECK(ui_scale_px(2147483647, 200) == 2147483647);
    CHECK(ui_scale_px(-2147483647 - 1, 200) == -2147483647 - 1);
    CHECK(ui_unscale_px(2147483647, 75) == 2147483647);

    // Unscale turns a device-space cursor into the panel's logical space. Not an exact inverse (both round), never off
    // by over a pixel.
    CHECK(ui_unscale_px(20, 200) == 10);
    CHECK(ui_unscale_px(8, 75) == 11);          // 800 + 37 = 837 / 75
    for (int px = 0; px <= 4000; px += 13) {
      for (int pct = UI_SCALE_MIN; pct <= UI_SCALE_MAX; pct += 5) {
        const int back = ui_unscale_px(ui_scale_px(px, pct), pct);
        CHECK(back >= px - 1 && back <= px + 1);
      }
    }
  }


void tb_0073_canvas_actually_spends() {
    // The canvas actually spends the scale factor, not just stores it (the recorder is what makes this testable).
    canvas_set_scale(100);
    canvas_record_begin();
    canvas_rect(10, 20, 30, 40, C_BONE);
    canvas_record_end();
    const CanvasOp* a = canvas_record_op(0);
    CHECK(a && a->x == 10 && a->y == 20 && a->w == 30 && a->h == 40);
    canvas_set_scale(200);
    CHECK(canvas_scale() == 200);
    canvas_record_begin();
    canvas_rect(10, 20, 30, 40, C_BONE);
    canvas_bevel(10, 20, 30, 40, C_PANEL, C_BEVELHI, C_BEVELLO);
    canvas_line(10, 20, 30, 40, C_BONE);
    canvas_hazard(10, 20, 30, 40, C_HAZARD, C_PANEL, 9);
    canvas_record_end();
    const CanvasOp* r2 = canvas_record_op(0);
    CHECK(r2 && r2->x == 20 && r2->y == 40 && r2->w == 60 && r2->h == 80);
    const CanvasOp* b2 = canvas_record_op(1);
    CHECK(b2 && b2->kind == CANVAS_OP_BEVEL && b2->x == 20 && b2->w == 60);
    const CanvasOp* l2 = canvas_record_op(2);
    CHECK(l2 && l2->kind == CANVAS_OP_LINE && l2->x == 20 && l2->y == 40);
    const CanvasOp* h2 = canvas_record_op(3);
    CHECK(h2 && h2->kind == CANVAS_OP_HAZARD && h2->x == 20 && h2->w == 60);
    // Text: origin and rendered width both scale, since the glyph quads do.
    canvas_set_scale(100);
    canvas_record_begin();
    canvas_text(10, 20, "Debug log", C_BONE, FONT_UI);
    canvas_record_end();
    const CanvasOp* t1 = canvas_record_op(0);
    CHECK(t1 && t1->x == 10 && t1->y == 20);
    const int w1 = t1 ? t1->w : 0;
    CHECK(w1 > 0);
    canvas_set_scale(200);
    canvas_record_begin();
    canvas_text(10, 20, "Debug log", C_BONE, FONT_UI);
    canvas_record_end();
    const CanvasOp* t2 = canvas_record_op(0);
    CHECK(t2 && t2->x == 20 && t2->y == 40);
    CHECK(t2 && t2->w == ui_scale_px(w1, 200));
    // canvas_text_width stays logical: menu.cpp measures with it to lay the panel out, and a moving measurement would
    // scale the layout twice.
    CHECK(canvas_text_width("Debug log", FONT_UI) == w1);
    canvas_set_scale(100);
    CHECK(canvas_scale() == 100);
    canvas_set_scale(-5);
    CHECK(canvas_scale() == UI_SCALE_MIN);
    canvas_set_scale(999999);
    CHECK(canvas_scale() == UI_SCALE_MAX);
    canvas_set_scale(100);
  }


void tb_0074_panel_75_100_200_cons() {
    const int savedTab = menu_active_tab();
    const int savedScale = g_cfg.uiscale;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_set_tab(TAB_VISUALS);

    // Panel grows: the bevel is twice as wide at 200 as at 100. Measured at 3840x2160: 1440 device pixels cannot hold a
    // 900-logical panel at 200% (it runs at the ceiling); the ceiling is pinned separately at 1440/1080 below.
    g_cfg.uiscale = 100;
    canvas_record_begin();
    menu_draw(3840, 2160);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const CanvasOp* p100 = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
    CHECK(p100);
    const int w100 = p100 ? p100->w : 0, x100 = p100 ? p100->x : 0;
    CHECK(w100 > 0);

    g_cfg.uiscale = 200;
    CHECK(menu_applied_scale(2160) == UI_SCALE_MAX);   // uncapped here, so 200 really is 200
    canvas_record_begin();
    menu_draw(3840, 2160);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const CanvasOp* p200 = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
    CHECK(p200);
    if (p100 && p200) {
      CHECK(p200->w > w100);
      CHECK(p200->w >= 2 * w100 - 4 && p200->w <= 2 * w100 + 4);
      // STILL ANCHORED at the left inset, in the same place relative to the screen edge: the panel grows, it does not
      // wander.
      CHECK(p200->x == ui_scale_px(x100, 200));
    }
    g_cfg.uiscale = 75;
    canvas_record_begin();
    menu_draw(3840, 2160);
    canvas_record_end();
    const CanvasOp* p75 = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
    CHECK(p75);
    if (p75 && p100) CHECK(p75->w < w100);

    // Still vertically centred at every scale: measured as an outcome (gap above equals gap below), two-pixel tolerance
    // since the gaps round independently.
    for (int i = 0; i < 3; i++) {
      g_cfg.uiscale = (i == 0) ? 75 : (i == 1) ? 100 : 200;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* pb = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
      CHECK(pb);
      if (pb) {
        const int above = pb->y, below = 1440 - (pb->y + pb->h);
        CHECK(pb->h > 0 && above >= 0 && below >= 0);
        CHECK(above >= below - 2 && above <= below + 2);
      }
    }

    for (int i = 0; i < 3; i++) {
      g_cfg.uiscale = (i == 0) ? 75 : (i == 1) ? 100 : 200;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_find_text("Show enemies") != nullptr);
      CHECK(canvas_record_find_text("Draw distance") != nullptr);
      CHECK(canvas_record_find_text("WORLD") != nullptr);
    }

    // Hit testing uses the same factor: pixel sweeps click in device coordinates, and at 200 the rows are twice as far
    // down.
    const VisState start = { 0, 1, 1, 1, 1, 0, 0, ESP_DRAWDIST_DEFAULT, 0, 0 };
    for (int i = 0; i < 3; i++) {
      g_cfg.uiscale = (i == 0) ? 75 : (i == 1) ? 100 : 200;
      bool multi = false;
      const int mask = vis_sweep(3440, 1440, start, false, &multi);
      CHECK(!multi);                                  // no two controls overlap at any scale
      CHECK((mask & VF_ENEMIES) != 0);
      CHECK((mask & VF_ITEMS) != 0);
      CHECK((mask & VF_SECRETS) != 0);
      CHECK((mask & VF_EXPLODABLES) != 0);
      CHECK((mask & VF_SWITCHES) != 0);
    }
    // Clickable where it is drawn: the row responds at the exact device y its label was drawn at, not just somewhere in
    // the sweep.
    for (int i = 0; i < 3; i++) {
      g_cfg.uiscale = (i == 0) ? 75 : (i == 1) ? 100 : 200;
      // Stored and applied are two different numbers (a stored 200 on a 1440p screen draws at the screen's ceiling).
      const int pct = menu_applied_scale(1440);
      vis_restore(start);
      menu_set_tab(TAB_VISUALS);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* row = canvas_record_find_text("Show enemies");
      CHECK(row);
      const UiRect lp = menu_panel_rect(ui_unscale_px(3440, pct), ui_unscale_px(1440, pct));
      int mask = 0;
      if (row) {
        for (int x = ui_scale_px(lp.x, pct); x < ui_scale_px(lp.x + lp.w, pct); x++) {
          vis_restore(start);
          menu_set_tab(TAB_VISUALS);
          g_ui.mx = x; g_ui.my = row->y; g_ui.down = false; g_ui.pressed = true;
          menu_draw(3440, 1440);
          mask |= vis_changed(start);
        }
      }
      CHECK(mask == VF_ENEMIES);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      vis_restore(start);
    }

    for (int i = 0; i < 3; i++) {
      g_cfg.uiscale = (i == 0) ? 75 : (i == 1) ? 100 : 200;
      const int pct = menu_applied_scale(1440);
      menu_set_tab(TAB_VISUALS);
      const UiRect p = menu_panel_rect(ui_unscale_px(3440, pct), ui_unscale_px(1440, pct));
      bool reachedView = false;
      for (int y = ui_scale_px(p.y, pct); y < ui_scale_px(p.y + p.h, pct) && !reachedView; y++) {
        menu_set_tab(TAB_VISUALS);
        g_ui.mx = ui_scale_px(p.x + RAIL_LABEL_X, pct); g_ui.my = y;
        g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
        if (menu_active_tab() == TAB_VIEW) reachedView = true;
      }
      CHECK(reachedView);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    }

    // Scale does not leak: menu_draw restores it, so ESP (drawn before the panel through the same canvas) is never
    // scaled with it.
    g_cfg.uiscale = 200;
    menu_draw(3440, 1440);
    CHECK(canvas_scale() == 100);

    // The "UI scale" row itself (Settings tab).
    g_cfg.uiscale = 100;
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("UI scale") != nullptr);
    // The row's `hi` is the screen's ceiling, not UI_SCALE_MAX: past the ceiling a step stored a number the panel would
    // not draw.
    CHECK(slider_row_reaches_ends(TAB_SETTINGS, "UI scale", 3440, 1440,
                                  UI_SCALE_MIN, ui_scale_max_for(1440, 900, 30),
                                  &g_cfg.uiscale, 100));
    g_cfg.uiscale = 100;
    {
      int worst = 0;
      CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
    }
    // 1024x768 cannot show a 900-logical-pixel panel at 100%, so the ceiling applies 75% there and every row draws
    // inside the screen. The guard stays: a fit check means something only if the rows were drawn. "Debug log" is this
    // tab's real last row.
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, "Debug log", 3440, 1440));
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, "Debug log", 1024, 768));
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, "UI scale", 1024, 768));
    g_cfg.uiscale = 100;
    // config: the floor is enforced on load, and an old ini holding 50 comes back as 75 rather than an unusable panel.
    {
      const char* t = "test_uiscale_5b5.ini";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("uiscale=50\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.uiscale == UI_SCALE_MIN);
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) { fputs("uiscale=999\n", f); fclose(f); }
      config_load(t);
      CHECK(g_cfg.uiscale == UI_SCALE_MAX);
      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.uiscale == UI_SCALE_DEFAULT);
    }
    g_cfg.uiscale = savedScale;
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_set_scale(100);
    g_cfgDirty = false;
  }


void tb_0076_c1_b_live_panel() {
    // Scale recovery on the live panel: the whole config is saved and restored, not just uiscale (config_load runs
    // set_defaults over the struct).
    const Config savedCfg = g_cfg;
    const int savedTab = menu_active_tab();
    const bool savedDirty = g_cfgDirty;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    // A recovery path exists at every scale on every screen: "UI scale" is the first row of its tab (it once clipped at
    // 156% on 1440p).
    {
      const int screens[3][2] = { { 1920, 1080 }, { 3440, 1440 }, { 1024, 768 } };
      const int stored[4] = { 75, 100, 150, 200 };
      for (int s = 0; s < 3; s++) {
        for (int v = 0; v < 4; v++) {
          g_cfg.uiscale = stored[v];
          menu_set_tab(TAB_SETTINGS);
          canvas_record_begin();
          menu_draw(screens[s][0], screens[s][1]);
          canvas_record_end();
          CHECK(!canvas_record_overflowed());
          CHECK(canvas_record_find_text("UI scale") != nullptr);
        }
      }
    }

    // Every other tab's last row too, at the applied scale: at the ceiling the panel is still at full logical height,
    // so no row is dropped.
    {
      const int screens[2][2] = { { 1920, 1080 }, { 3440, 1440 } };
      for (int s = 0; s < 2; s++) {
        const int sw = screens[s][0], sh = screens[s][1];
        g_cfg.uiscale = UI_SCALE_MAX;                     // ask for more than the screen allows
        const int applied = menu_applied_scale(sh);
        CHECK(applied == ui_scale_max_for(sh, 900, 30));  // the ceiling is what is applied
        CHECK(applied < UI_SCALE_MAX);                    // and both these screens do cap
        // Hints are tooltips, so each tab's real last row is the one a hint used to sit under. Combat's bottom row was
        // never a hint.
        CHECK(tab_bottom_row_fits(TAB_VIEW, "Screenshot mode", sw, sh));
        CHECK(tab_bottom_row_fits(TAB_VISUALS, "Fullbright", sw, sh));
        CHECK(tab_bottom_row_fits(TAB_COMBAT, "Max redirect", sw, sh));
        CHECK(tab_bottom_row_fits(TAB_WEAPONS, "Weapon switch speed", sw, sh));
        CHECK(tab_bottom_row_fits(TAB_SETTINGS, "UI scale", sw, sh));
        CHECK(tab_bottom_row_fits(TAB_SETTINGS, "Debug log", sw, sh));
      }
    }

    // Position is load-bearing too: below the ceiling, every bind row whose key the game's cfg also binds draws a 34px
    // note under it. Drawn first, "UI scale" survives whatever margin the tab spends; here one note (under "Panel
    // key"), the other three bound to a code the game's cfg has no name for.
    {
      const char* t = "test_uiscale_gamecfg_5b5.cfg";
      FILE* f = nullptr;
      CHECK(fopen_s(&f, t, "w") == 0 && f);
      if (f) fputs("[KeyDefinitions]\nSave_Menu = \"F2\" \"None\"\n", f);
      if (f) fclose(f);
      menu_gamecfg_load(t);
      g_cfg.uiscale = UI_SCALE_MAX;
      g_cfg.menukey = 0x71;   // F2
      g_cfg.freecamkey = 0x41;              // 'A': no name in the game's cfg,
      g_cfg.freecamteleportkey = 0x41;      // so these three draw no note
      g_cfg.aimbind = 0x41;
      menu_set_tab(TAB_SETTINGS);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      int notes = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_GAMECFG_NOTE)) notes++;
      }
      CHECK(notes == 1);                                       // the one bind row left here really is bound
      CHECK(canvas_record_find_text("UI scale") != nullptr);
      CHECK(canvas_record_find_text("Debug log") != nullptr);
      menu_gamecfg_load(0);
      remove(t);
    }

    // Panel always fits the screen, top and bottom, whatever is stored.
    {
      const int screens[3][2] = { { 1024, 768 }, { 1920, 1080 }, { 3440, 1440 } };
      const int stored[3] = { 100, 150, 200 };
      for (int s = 0; s < 3; s++) {
        for (int v = 0; v < 3; v++) {
          g_cfg.uiscale = stored[v];
          menu_set_tab(TAB_VIEW);
          canvas_record_begin();
          menu_draw(screens[s][0], screens[s][1]);
          canvas_record_end();
          const CanvasOp* pb = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
          CHECK(pb);
          if (pb) CHECK(pb->y >= 0 && pb->y + pb->h <= screens[s][1]);
        }
      }
    }

    // The readout shows what is applied, never what is stored (no " max" suffix: the cap is the track's own right end).
    g_cfg.uiscale = UI_SCALE_MAX;
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      char want[24];
      sprintf_s(want, "%d%%", menu_applied_scale(1440));
      CHECK(canvas_record_find_text(want) != nullptr);
    }
    g_cfg.uiscale = 100;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("100%") != nullptr);
    CHECK(canvas_record_find_text("100% max") == nullptr);

    // A multi-frame hold at a fixed device point: the cursor does not move between frames, and neither should the
    // panel. If the layout tracked the value, frame 2 would draw at the new scale, the grab box would leave the pointer
    // and the value would freeze at one jump or slam between the ends; this catches both.
    g_cfg.uiscale = 100;
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    const CanvasOp* rowOp = canvas_record_find_text("UI scale");
    const CanvasOp* pb0 = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
    CHECK(rowOp && pb0);
    const int wBefore = pb0 ? pb0->w : 0;
    const int rowY = rowOp ? rowOp->y : 0;
    // Where on the row the track's high end is, found by asking rather than restating menu.cpp's track geometry.
    int holdX = -1;
    if (rowOp) {
      const UiRect lp = menu_panel_rect(3440, 1440);   // applied is 100 here, so logical == device
      for (int x = lp.x; x < lp.x + lp.w && holdX < 0; x++) {
        g_cfg.uiscale = 100;
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = x; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
        menu_draw(3440, 1440);
        // The track's right end stores the screen's ceiling, not UI_SCALE_MAX; compared against ui_scale_max_for, not
        // menu_applied_scale (which would echo whatever the draw stored).
        if (g_cfg.uiscale == ui_scale_max_for(1440, 900, 30)) holdX = x;
      }
    }
    CHECK(holdX >= 0);
    // Settle back at 100 with the button up, then hold.
    g_cfg.uiscale = 100;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(3440, 1440);
    if (holdX >= 0) {
      bool frozen = true, settled = true;
      int prev = -1;
      for (int f = 0; f < 5; f++) {
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = holdX; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
        canvas_record_begin();
        menu_draw(3440, 1440);
        canvas_record_end();
        const CanvasOp* pbf = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
        if (!pbf || pbf->w != wBefore) frozen = false;       // the panel did not move
        if (!canvas_record_find_text("UI scale")) frozen = false;
        if (f > 0 && g_cfg.uiscale != prev) settled = false; // and the value did not oscillate
        prev = g_cfg.uiscale;
      }
      CHECK(frozen);
      CHECK(settled);
      CHECK(g_cfg.uiscale == ui_scale_max_for(1440, 900, 30));
      // Release: only now does the new value apply, capped, on screen.
      g_ui.down = false; g_ui.pressed = false;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* pbAfter = canvas_record_find_kind(CANVAS_OP_BEVEL, C_PANEL);
      CHECK(pbAfter);
      if (pbAfter) {
        CHECK(pbAfter->w > wBefore);
        CHECK(pbAfter->y >= 0 && pbAfter->y + pbAfter->h <= 1440);
      }
      CHECK(canvas_record_find_text("UI scale") != nullptr);
    }

    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg = savedCfg;
    menu_set_tab(savedTab);
    g_cfgDirty = savedDirty;
    canvas_set_scale(100);
    menu_draw(3440, 1440);   // one settled draw, so nothing after this inherits a latch
  }



// Initialisation runs exactly once. DllMain runs under the loader lock, so init moves to the first poll on the game's
// own thread, where nothing is held. A three-value state machine, not a bool, because "running" is real (hooked_poll's
// init installs hooks other threads could reach): PENDING -> RUNNING -> DONE, only PENDING runs anything.
void tb_0077_phase_5b_task_6_initialisation() {
    CHECK(INIT_PENDING == 0);
    CHECK(INIT_RUNNING == 1);
    CHECK(INIT_DONE == 2);
    CHECK( init_should_run(INIT_PENDING));
    CHECK(!init_should_run(INIT_RUNNING));    // already inside it: never re-enter
    CHECK(!init_should_run(INIT_DONE));
    // Garbage is never "run it": must not re-run one-shot .text patches.
    CHECK(!init_should_run(-1));
    CHECK(!init_should_run(3));
    CHECK(!init_should_run(99999));
    CHECK(init_next_state(INIT_PENDING) == INIT_RUNNING);
    CHECK(init_next_state(INIT_RUNNING) == INIT_DONE);
    CHECK(init_next_state(INIT_DONE) == INIT_DONE);     // terminal
    CHECK(init_next_state(-1) == INIT_DONE);            // garbage settles at DONE
    CHECK(init_next_state(77) == INIT_DONE);
    // Driven as the hook drives it: a thousand polls, body runs once.
    int state = INIT_PENDING;
    int ran = 0;
    for (int i = 0; i < 1000; i++) {
      if (init_should_run(state)) {
        state = init_next_state(state);          // RUNNING, before the body
        ran++;
        // Re-entrancy: the body itself polls again.
        if (init_should_run(state)) ran++;
        state = init_next_state(state);          // DONE, after the body
      }
    }
    CHECK(ran == 1);
    CHECK(state == INIT_DONE);
    // From DONE, nothing ever runs again, however many times it is asked.
    for (int i = 0; i < 100; i++) {
      if (init_should_run(state)) ran++;
      state = init_next_state(state);
    }
    CHECK(ran == 1);
    CHECK(state == INIT_DONE);
  }



// The entry hook can fail. hook_iat has four real refusal paths (uncommitted slot, guard/unreadable page, a slot inside
// our own image, VirtualProtect failure); if it fails, DllMain runs the init body inline under the loader lock rather
// than silently taking the whole mod down.
void tb_0078_phase_5b_task_6_fix() {
    CHECK(INIT_ENTRY_DEFERRED == 0);
    CHECK(INIT_ENTRY_INLINE == 1);
    CHECK(init_entry_outcome(true)  == INIT_ENTRY_DEFERRED);
    CHECK(init_entry_outcome(false) == INIT_ENTRY_INLINE);

    // Driven as DllMain drives it. Hook went in: DllMain runs no body and leaves state PENDING so the first poll runs
    // it.
    int st = INIT_PENDING;
    int ranInline = 0;
    if (init_entry_outcome(true) == INIT_ENTRY_INLINE) { st = INIT_DONE; ranInline++; }
    CHECK(ranInline == 0);
    CHECK(st == INIT_PENDING);
    CHECK(init_should_run(st));          // the poll still has the body to run

    // Hook failed: DllMain marks DONE first, then runs the body inline, so a poll through an unpatched slot cannot run
    // it twice.
    st = INIT_PENDING;
    ranInline = 0;
    if (init_entry_outcome(false) == INIT_ENTRY_INLINE) {
      st = INIT_DONE;                    // input_mark_init_done(), before the body
      ranInline++;                       // mod_deferred_init(), inline
    }
    CHECK(ranInline == 1);
    CHECK(st == INIT_DONE);
    CHECK(!init_should_run(st));         // nothing can ever re-run it
    // ... and it stays DONE however many polls arrive afterwards.
    for (int i = 0; i < 100; i++) {
      if (init_should_run(st)) ranInline++;
      st = init_next_state(st);
    }
    CHECK(ranInline == 1);
    CHECK(st == INIT_DONE);
  }
