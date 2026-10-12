#include "../tooltip.h"
#include "../anim.h"
#include "test_common.h"
void tb_0120_tooltip_rule() {
  // Checks are written in terms of TOOLTIP_DELAY_MS so it can move; the one absolute bound is 100-200 ms.
  CHECK(TOOLTIP_DELAY_MS >= 100 && TOOLTIP_DELAY_MS <= 200);
  TooltipState s = tooltip_begin();
  s = tooltip_step(s, 7, false, 1000, ANIM_OFF, 0.0);
  CHECK(s.rowKey == 7 && !s.shown && s.alpha == 0.0);
  s = tooltip_step(s, 7, false, 1000 + TOOLTIP_DELAY_MS - 1, ANIM_OFF, 0.0);
  CHECK(!s.shown);
  s = tooltip_step(s, 7, false, 1000 + TOOLTIP_DELAY_MS, ANIM_OFF, 0.0);
  CHECK(s.shown && s.alpha == 1.0);             // Off: full at once
  s = tooltip_step(s, -1, false, 2000, ANIM_OFF, 0.0);
  CHECK(!s.shown && s.alpha == 0.0 && s.rowKey == -1);
  s = tooltip_step(s, 3, false, 3000, ANIM_OFF, 0.0);
  s = tooltip_step(s, 4, false, 3000 + TOOLTIP_DELAY_MS, ANIM_OFF, 0.0);
  CHECK(!s.shown);                               // the key changed at the delay: restarted
  s = tooltip_step(s, 4, false, 3000 + 2 * TOOLTIP_DELAY_MS, ANIM_OFF, 0.0);
  CHECK(s.shown);
  s = tooltip_step(s, 4, true, 4000, ANIM_OFF, 0.0);
  CHECK(!s.shown && s.pressedHide);
  s = tooltip_step(s, 4, false, 5000, ANIM_OFF, 0.0);
  CHECK(!s.shown);                               // still hidden on the same key
  s = tooltip_step(s, -1, false, 5001, ANIM_OFF, 0.0);
  s = tooltip_step(s, 4, false, 5002, ANIM_OFF, 0.0);
  s = tooltip_step(s, 4, false, 5002 + TOOLTIP_DELAY_MS, ANIM_OFF, 0.0);
  CHECK(s.shown);
  // Simple: alpha rises over ANIM_HOVER_IN_MS after the delay, never past 1. Delta is milliseconds, as at the panel's
  // call site.
  s = tooltip_begin();
  s = tooltip_step(s, 1, false, 0, ANIM_SIMPLE, 0.0);
  s = tooltip_step(s, 1, false, TOOLTIP_DELAY_MS, ANIM_SIMPLE, 0.0);
  CHECK(s.shown && s.alpha == 0.0);
  s = tooltip_step(s, 1, false, TOOLTIP_DELAY_MS + 40, ANIM_SIMPLE, 40.0);
  CHECK(s.alpha > 0.45 && s.alpha < 0.55);       // half of ANIM_HOVER_IN_MS (80)
  s = tooltip_step(s, 1, false, TOOLTIP_DELAY_MS + 1000, ANIM_SIMPLE, 960.0);
  CHECK(s.alpha == 1.0);
  s = tooltip_step(s, -1, false, 9999, ANIM_SIMPLE, 16.0);
  CHECK(s.alpha == 0.0);                         // no fade out: instant
  UiRect b = tooltip_box_place(100, 100, 200, 14, 1920, 1080);
  CHECK(b.x == 100 + TOOLTIP_DX && b.y == 100 + TOOLTIP_DY);
  CHECK(b.w == 200 + 2 * TOOLTIP_PAD && b.h == 14 + 2 * TOOLTIP_PAD);
  b = tooltip_box_place(1900, 100, 200, 14, 1920, 1080);
  CHECK(b.x + b.w <= 1920 && b.x == 1900 - TOOLTIP_DX - b.w);   // flipped left
  b = tooltip_box_place(100, 1075, 200, 14, 1920, 1080);
  CHECK(b.y + b.h <= 1080 && b.y == 1075 - TOOLTIP_DY - b.h);   // flipped up
  b = tooltip_box_place(-50, -50, 200, 14, 1920, 1080);
  CHECK(b.x >= 0 && b.y >= 0);                    // clamped, never negative
  b = tooltip_box_place(10, 10, 5000, 14, 1920, 1080);
  CHECK(b.x == 0 && b.w == 5000 + 2 * TOOLTIP_PAD); // wider than the screen: left edge, not narrowed
}

// The tooltip rule wired into the panel: no hint is a row any more, each shows beside the cursor after resting on its
// row.
void tb_0121_tooltips_drawn() {
  const int savedTab   = menu_active_tab();
  const int savedAnim  = g_cfg.menuanim;
  const int savedScale = g_cfg.uiscale;
  const int savedMenuKey = g_cfg.menukey;
  // Recorder x is device space, g_ui.mx logical: kept equal here. Off, so alpha is full the frame it appears.
  g_cfg.uiscale = UI_SCALE_DEFAULT;
  g_cfg.menuanim = ANIM_OFF;
  menu_set_now_ms(1000);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_set_tab(TAB_VISUALS);
  canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
  CHECK(!canvas_record_overflowed());
  CHECK(canvas_record_find_text("Markers stop at this range.") == nullptr);
  CHECK(canvas_record_find_text("Draw distance") != nullptr);
  CHECK(menu_tooltip_text() == nullptr);
  {
    static const char* const GONE[5] = {
      "No shading or fog. Textures reload once.",          // Visuals
      "Fires by itself while the condition below holds",   // Combat
      "Raw input, equal axes, no dead zone.",              // Weapons
      "Hides weapon, crosshair, HUD and markers while flying.",  // View
      "Writes duke3dmod.log beside the game. Off by default."    // Settings
    };
    const int tabs[5] = { TAB_VISUALS, TAB_COMBAT, TAB_WEAPONS, TAB_VIEW, TAB_SETTINGS };
    for (int i = 0; i < 5; i++) {
      menu_set_tab(tabs[i]);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      CHECK(canvas_record_find_text(GONE[i]) == nullptr);
    }
    menu_set_tab(TAB_VISUALS);
  }

  canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
  const CanvasOp* row = canvas_record_find_text("Draw distance");
  CHECK(row != nullptr);
  if (row) {
    const int rowX = row->x, rowY = row->y;
    g_ui.mx = rowX + 10; g_ui.my = rowY + 4;
    menu_set_now_ms(1016); menu_draw(3440, 1440);                       // arrival frame
    menu_set_now_ms(1016 + TOOLTIP_DELAY_MS - 10);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    CHECK(canvas_record_find_text("Markers stop at this range.") == nullptr);
    CHECK(menu_tooltip_text() == nullptr);
    menu_set_now_ms(1016 + TOOLTIP_DELAY_MS + 1);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    const CanvasOp* tip = canvas_record_find_text("Markers stop at this range.");
    CHECK(tip != nullptr);
    CHECK(streq(menu_tooltip_text(), "Markers stop at this range."));
    if (tip) CHECK(tip->x > g_ui.mx && tip->y > g_ui.my);
    if (tip) CHECK(canvas_record_op(canvas_record_count() - 1)->kind == CANVAS_OP_TEXT);
    if (tip) {
      g_ui.pressed = true;
      menu_set_now_ms(1016 + TOOLTIP_DELAY_MS + 20);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      CHECK(menu_tooltip_text() == nullptr);
      g_ui.pressed = false;
      menu_set_now_ms(1016 + 4 * TOOLTIP_DELAY_MS);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      CHECK(menu_tooltip_text() == nullptr);   // still away: the cursor never left
    }
    g_ui.mx = -1; g_ui.my = -1;
    menu_set_now_ms(1016 + 5 * TOOLTIP_DELAY_MS);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    CHECK(canvas_record_find_text("Markers stop at this range.") == nullptr);
    CHECK(menu_tooltip_text() == nullptr);
  }
  // Every hint, every tab, under 63 characters: rest the cursor on each row and read back what the panel shows. A row
  // label is a FONT_UI text op past the tab rail (RAIL_W), found without restating menu.cpp's row pitch or group order.
  {
    int hintsSeen = 0;
    bool allShort = true, allNonEmpty = true;
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      menu_set_tab(tab);
      g_ui.mx = -1; g_ui.my = -1;
      unsigned t = 10000u + (unsigned)tab * 100000u;
      menu_set_now_ms(t);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const UiRect p = menu_panel_rect(3440, 1440);
      int lx[64], ly[64], rows = 0;
      for (int i = 0; i < canvas_record_count() && rows < 64; i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && o->font == FONT_UI &&
            o->x > p.x + RAIL_W) { lx[rows] = o->x; ly[rows] = o->y; rows++; }
      }
      CHECK(rows > 0);
      for (int i = 0; i < rows; i++) {
        g_ui.mx = -1; g_ui.my = -1;
        t += 4 * TOOLTIP_DELAY_MS;
        menu_set_now_ms(t);
        menu_draw(3440, 1440);                    // the cursor leaves: nothing rested
        g_ui.mx = lx[i] + 4; g_ui.my = ly[i] + 4;
        t += 16;
        menu_set_now_ms(t);
        menu_draw(3440, 1440);                    // arrival
        t += TOOLTIP_DELAY_MS + 1;
        menu_set_now_ms(t);
        menu_draw(3440, 1440);                    // past the rest delay
        const char* hint = menu_tooltip_text();
        if (hint) {
          hintsSeen++;
          if (strlen(hint) >= 63) allShort = false;
          if (hint[0] == 0) allNonEmpty = false;
        }
      }
    }
    CHECK(allShort);
    CHECK(allNonEmpty);
    // Counted exactly: 8 Visuals, 1 Combat, 7 Weapons, 9 View, 3 Settings. A hint dropped by a future edit fails here.
    CHECK(hintsSeen == 31);
  }

  // MENU_GAMECFG_NOTE warns about this row's key, so it must be visible without hovering.
  {
    const char* t = "test_gamecfg_tooltip.cfg";
    FILE* fc = nullptr;
    CHECK(fopen_s(&fc, t, "w") == 0 && fc);
    if (fc) {
      fputs("[KeyDefinitions]\nMap = \"Insert\" \"None\"\n", fc);
      fclose(fc);
    }
    menu_gamecfg_load(t);
    g_cfg.menukey = 0x2D;                      // Insert, which that cfg binds
    menu_set_tab(TAB_SETTINGS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_set_now_ms(900000);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) != nullptr);
    CHECK(menu_tooltip_text() == nullptr);     // drawn as a row, with no hover at all
    menu_gamecfg_load(0);
    remove(t);
  }

  // Hints only checked absent above get their words pinned. The FOV hint is not a literal (names FOV_DEFAULT), so
  // row_hint copies it into s_hintBuf; reading it back through tooltip_for_row proves the copy happened.
  {
    menu_set_tab(TAB_VIEW);
    CHECK(streq(tooltip_for_row("Screenshot mode", 3440, 1440),
                "Hides weapon, crosshair, HUD and markers while flying."));
    char fovHint[64];
    sprintf_s(fovHint, "Updates as you drag. Default is %d degrees.", FOV_DEFAULT);
    menu_set_tab(TAB_VIEW);
    CHECK(streq(tooltip_for_row("Field of view", 3440, 1440), fovHint));
    menu_set_tab(TAB_SETTINGS);
    CHECK(streq(tooltip_for_row("Debug log", 3440, 1440),
                "Writes duke3dmod.log beside the game. Off by default."));
  }

  // Pressing a mouse button on the row hides it, either button. Driven on a switch row (takes no right press), so "no
  // field opened" keeps the hide attributable to the press itself.
  {
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    const unsigned t = 600000u;
    menu_set_now_ms(t); menu_draw(3440, 1440);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    const CanvasOp* dn = canvas_record_find_text("Damage numbers");
    CHECK(dn != nullptr);
    if (dn) {
      g_ui.mx = dn->x + 4; g_ui.my = dn->y + 4;
      menu_set_now_ms(t + 16); menu_draw(3440, 1440);                    // arrival
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS + 1); menu_draw(3440, 1440);
      CHECK(menu_tooltip_text() != nullptr);
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS + 21); menu_draw(3440, 1440);
      CHECK(menu_tooltip_text() == nullptr);
      CHECK(!menu_entry_open());
      g_ui.rdown = false; g_ui.rpressed = false;
      menu_set_now_ms(t + 16 + 5 * TOOLTIP_DELAY_MS); menu_draw(3440, 1440);
      CHECK(menu_tooltip_text() == nullptr);   // still away: the cursor never left
    }
    g_ui.mx = -1; g_ui.my = -1;
    menu_set_now_ms(t + 50000u); menu_draw(3440, 1440);
  }

  // Fade in, end to end, under Simple: tooltip_step's delta is milliseconds (anim_dt_clamped's unit), so this goes
  // through menu_draw and reads the alpha the text op drew with. The preceding OFF frame snaps the open spring to 1, so
  // Simple starts settled.
  {
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    g_cfg.menuanim = ANIM_SIMPLE;
    const unsigned t = 700000u;
    menu_set_now_ms(t); menu_draw(3440, 1440);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const CanvasOp* dd = canvas_record_find_text("Draw distance");
    CHECK(dd != nullptr);
    if (dd) {
      const unsigned full = (C_BONE >> 24) & 0xFFu;
      g_ui.mx = dd->x + 4; g_ui.my = dd->y + 4;
      menu_set_now_ms(t + 16); menu_draw(3440, 1440);            // arrival
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS - 20);
      menu_draw(3440, 1440);                                     // still resting
      CHECK(menu_tooltip_text() == nullptr);
      // First frame shown, 20 ms after the one before: a quarter of ANIM_HOVER_IN_MS, so a quarter of C_BONE's alpha.
      unsigned now = t + 16 + TOOLTIP_DELAY_MS;
      menu_set_now_ms(now);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      const CanvasOp* tip = canvas_record_find_text("Markers stop at this range.");
      CHECK(tip != nullptr);
      if (tip) {
        const unsigned a0 = (tip->colour >> 24) & 0xFFu;
        CHECK(a0 > 0u && a0 < full);
      }
      for (int i = 0; i < 2; i++) {
        now += 20;
        menu_set_now_ms(now);
        canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      }
      const CanvasOp* mid = canvas_record_find_text("Markers stop at this range.");
      CHECK(mid != nullptr);
      if (mid) CHECK(((mid->colour >> 24) & 0xFFu) < full);
      now += 20;
      menu_set_now_ms(now);
      canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
      const CanvasOp* done = canvas_record_find_text("Markers stop at this range.");
      CHECK(done != nullptr);
      if (done) CHECK(((done->colour >> 24) & 0xFFu) == full);
    }
    g_cfg.menuanim = ANIM_OFF;
    g_ui.mx = -1; g_ui.my = -1;
    menu_set_now_ms(t + 100000u); menu_draw(3440, 1440);
  }

  // Nothing is shown while the panel is closing: overlay.cpp keeps calling menu_draw through the fade-out with the
  // cursor resting, so the rest timer would reach its end first. The tooltip pass refuses unless the panel is logically
  // open.
  {
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    const unsigned t = 800000u;
    menu_set_now_ms(t); menu_draw(3440, 1440);
    canvas_record_begin(); menu_draw(3440, 1440); canvas_record_end();
    const CanvasOp* dd = canvas_record_find_text("Draw distance");
    CHECK(dd != nullptr);
    if (dd) {
      g_ui.mx = dd->x + 4; g_ui.my = dd->y + 4;
      menu_set_now_ms(t + 16); menu_draw(3440, 1440);
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS + 1); menu_draw(3440, 1440);
      CHECK(menu_tooltip_text() != nullptr);        // shown, with the panel open
      menu_keycap_close();
      menu_stub_set_open(false);
      for (int i = 1; i <= 4; i++) {
        menu_set_now_ms(t + 16 + (unsigned)(i + 1) * (TOOLTIP_DELAY_MS + 1));
        menu_draw(3440, 1440);
        CHECK(menu_tooltip_text() == nullptr);
      }
      menu_stub_set_open(true);
    }
    g_ui.mx = -1; g_ui.my = -1;
    menu_set_now_ms(t + 100000u); menu_draw(3440, 1440);
  }

  g_cfg.menukey = savedMenuKey;
  g_cfg.menuanim = savedAnim;
  g_cfg.uiscale = savedScale;
  menu_set_tab(savedTab);
  menu_set_now_ms(0);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_ui.rdown = false; g_ui.rpressed = false;
}
