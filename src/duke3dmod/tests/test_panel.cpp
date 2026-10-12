#include "../panel_layout.h"
#include "../anim.h"  // ANIM_OFF: presentation colour and canvas transform are the identity, so a recorded op is logical layout times applied scale
#include "test_common.h"

// Pins panel_layout.h's layout and drag rules, plus the four ini keys that
// carry a stored panel rect across a restart.
void tb_0122_panel_rules() {
  const PanelBox d = { -1, -1, 0, 0 };
  PanelBox b = panel_layout(d, 2293, 960);        // 3440x1440 at UI scale 150 (logical)
  CHECK(b.x == PANEL_INSET && b.w == PANEL_W && b.h == PANEL_H && b.y == (960 - PANEL_H) / 2);
  b = panel_layout(d, 1920, 1080);
  CHECK(b.x == PANEL_INSET && b.w == PANEL_W && b.h == PANEL_H && b.y == (1080 - PANEL_H) / 2);
  // Too short for the full PANEL_H: panel_default_box still takes PANEL_VINSET
  // off each side.
  const PanelBox shortDef = panel_default_box(1280, 720);
  CHECK(shortDef.h == 720 - 2 * PANEL_VINSET && shortDef.y == PANEL_VINSET);
  // panel_layout's minimum clamp raises that height toward PANEL_MIN_H but never past the screen: PANEL_MIN_H is 740
  // and this screen is 720 tall, so the box fills the screen.
  b = panel_layout(d, 1280, 720);
  CHECK(b.h == 720 && b.y == 0);
  // A screen tall enough for the minimum but not PANEL_H: the clamp raises 720 to PANEL_MIN_H, and y keeps its centred
  // inset rather than being recentred.
  b = panel_layout(d, 1280, 780);
  CHECK(b.h == PANEL_MIN_H && b.y == PANEL_VINSET);
  const PanelBox s = { 300, 200, 700, 800 };
  b = panel_layout(s, 1920, 1080); CHECK(b.x == 300 && b.y == 200 && b.w == 700 && b.h == 800);
  b = panel_layout(PanelBox{ 1800, 1000, 700, 800 }, 1920, 1080);
  CHECK(b.x + b.w == 1920 && b.y + b.h == 1080);   // pushed back on screen
  b = panel_layout(PanelBox{ -500, -500, 700, 800 }, 1920, 1080); CHECK(b.x == 0 && b.y == 0);
  b = panel_layout(PanelBox{ 0, 0, 100, 100 }, 1920, 1080);
  CHECK(b.w == PANEL_MIN_W && b.h == PANEL_MIN_H);  // never below the minimums
  b = panel_layout(PanelBox{ 0, 0, 9000, 9000 }, 1920, 1080);
  CHECK(b.w == 1920 && b.h == 1080);               // never larger than the screen
  b = panel_layout(PanelBox{ 0, 0, 9000, 9000 }, 300, 300);
  CHECK(b.w == 300 && b.h == 300);                 // a screen smaller than the minimum: the screen wins
  PanelDrag g = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
  PanelBox cur = { 100, 100, 620, 900 }; PanelBox out; bool ended = false;
  g = panel_drag_step(g, false, false, true, false, 150, 110, cur, 1920, 1080, &out, &ended);
  CHECK(g.phase == PDRAG_IDLE && out.x == 100 && !ended);        // hover is not a drag
  g = panel_drag_step(g, true, true, true, false, 150, 110, cur, 1920, 1080, &out, &ended);
  CHECK(g.phase == PDRAG_MOVE && g.grabDx == 50 && g.grabDy == 10 && !ended);
  g = panel_drag_step(g, false, true, false, false, 400, 300, cur, 1920, 1080, &out, &ended);
  // moves, keeps size -- y clamps to 180 (0..screenH-h with h=900 on a
  // 1080-tall screen), not the raw grab-offset 290.
  CHECK(out.x == 350 && out.y == 180 && out.w == 620 && out.h == 900 && !ended);
  g = panel_drag_step(g, false, true, false, false, 5000, 5000, cur, 1920, 1080, &out, &ended);
  CHECK(out.x + out.w == 1920 && out.y + out.h == 1080);                           // clamped
  g = panel_drag_step(g, false, false, false, false, 5000, 5000, out, 1920, 1080, &out, &ended);
  CHECK(g.phase == PDRAG_IDLE && ended);                                            // released
  g = panel_drag_step(g, false, false, false, false, 5000, 5000, out, 1920, 1080, &out, &ended);
  CHECK(!ended);                                                                    // fires once
  cur = PanelBox{ 100, 100, 620, 900 };
  g = panel_drag_step(g, true, true, false, true, 715, 995, cur, 1920, 1080, &out, &ended);
  CHECK(g.phase == PDRAG_RESIZE);
  g = panel_drag_step(g, false, true, false, false, 815, 1045, cur, 1920, 1080, &out, &ended);
  CHECK(out.x == 100 && out.y == 100 && out.w == 720 && out.h == 950);
  g = panel_drag_step(g, false, true, false, false, 0, 0, cur, 1920, 1080, &out, &ended);
  CHECK(out.w == PANEL_MIN_W && out.h == PANEL_MIN_H);
  g = panel_drag_step(PanelDrag{ PDRAG_IDLE, 0, 0, {0,0,0,0} }, true, true, false, false, 5, 5, cur, 1920, 1080, &out, &ended);
  CHECK(g.phase == PDRAG_IDLE);
  const UiRect grip = panel_grip_rect(cur);
  CHECK(grip.w == 16 && grip.h == 16 && grip.x + grip.w == cur.x + cur.w && grip.y + grip.h == cur.y + cur.h);
  const UiRect title = panel_title_rect(cur, 15, 30);
  CHECK(title.x == cur.x && title.y == cur.y && title.w == cur.w && title.h == 1 + 15 + 30);
  config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.panelx == -1 && g_cfg.panely == -1 && g_cfg.panelw == 0 && g_cfg.panelh == 0);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\npanelx=300\npanely=200\npanelw=700\npanelh=800\n", f);
  fclose(f);
  config_load(tb_path("Z:|does|not|exist.ini"));   // poisoned before the reload
  config_load(tmp);
  CHECK(g_cfg.panelx == 300 && g_cfg.panely == 200 && g_cfg.panelw == 700 && g_cfg.panelh == 800);
  remove(tmp);
  // config_load only refuses what could never be a rect: panelx/panely clamp up to -1 (unset), panelw/panelh up to 0
  // (unset); panel_layout raises a too-small size or caps a too-large one every frame.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\npanelx=300\npanely=200\npanelw=99999\npanelh=800\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.panelw == 99999);   // stored as read; the layout rule clamps it, not config_load
  remove(tmp);
  // A positive width/height under PANEL_MIN is stored as read; only negatives are refused.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\npanelx=300\npanely=200\npanelw=10\npanelh=10\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.panelw == 10 && g_cfg.panelh == 10);
  remove(tmp);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\npanelx=300\npanely=200\npanelw=-5\npanelh=-1\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.panelw == 0 && g_cfg.panelh == 0);
  remove(tmp);
  // panelx/panely below -1 clamp up to -1; -1 and anything above pass through.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\npanelx=-5\npanely=-1\npanelw=0\npanelh=0\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.panelx == -1 && g_cfg.panely == -1);
  remove(tmp);
}

// Pins the panel as drawn: menu_draw lays out through panel_layout, title
// band drags/grip resizes, no row acts mid-drag, UI scale ceiling follows
// stored height, and the minimum box drops no row the default one draws.

// Bottom of the lowest thing drawn in the content column, in logical pixels (recorded device coordinates unscaled via
// ui_unscale_px, a TEXT op's height from font_height[font]); skips left of box.x + RAIL_W.
static int panel_content_bottom(int pct, const PanelBox& box) {
  const UiRect gripLogical = panel_grip_rect(box);
  int bottom = 0;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (!o) continue;
    // The grip is not content (its diagonals sit in the last 16px by construction); skipped by position, not kind, so a
    // future line-drawn row still counts.
    if (o->kind == CANVAS_OP_LINE) {
      const int lx0 = ui_unscale_px(o->x, pct), ly0 = ui_unscale_px(o->y, pct);
      const int lx1 = ui_unscale_px(o->x + o->w, pct), ly1 = ui_unscale_px(o->y + o->h, pct);
      if (lx0 >= gripLogical.x && lx0 <= gripLogical.x + gripLogical.w &&
          ly0 >= gripLogical.y && ly0 <= gripLogical.y + gripLogical.h &&
          lx1 >= gripLogical.x && lx1 <= gripLogical.x + gripLogical.w &&
          ly1 >= gripLogical.y && ly1 <= gripLogical.y + gripLogical.h) continue;
    }
    if (ui_unscale_px(o->x, pct) < box.x + RAIL_W) continue;
    const int y = ui_unscale_px(o->y, pct);
    const int h = (o->kind == CANVAS_OP_TEXT)
                    ? ((o->font >= 0 && o->font < FONT_COUNT) ? font_height[o->font] : 0)
                    : ui_unscale_px(o->h, pct);
    if (y + h > bottom) bottom = y + h;
  }
  return bottom;
}

// Lines of writing the tab drew in the content column: one per row label (FONT_UI), one per group heading/placeholder
// (FONT_SMALL), none per readout. A row without cursor height draws nothing, so comparing counts at the minimum and
// default box catches a silently dropped row.
static int panel_content_lines(int pct, const PanelBox& box) {
  int n = 0;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (!o || o->kind != CANVAS_OP_TEXT) continue;
    if (ui_unscale_px(o->x, pct) < box.x + RAIL_W) continue;
    if (o->font == FONT_UI || o->font == FONT_SMALL) n++;
  }
  return n;
}

// Narrowest gap any row's label leaves before its slider track (large if none); sound only at applied scale 100. Unlike
// tab_text_fits, which checks right-edge overflow only, this is what sets PANEL_MIN_W.
static int panel_label_track_gap(int tab, int screenW, int screenH) {
  menu_set_tab(tab);
  canvas_record_begin();
  menu_draw(screenW, screenH);
  canvas_record_end();
  int worst = 9999;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* track = canvas_record_op(i);
    if (!track || track->kind != CANVAS_OP_BEVEL || track->colour != C_WELL) continue;
    for (int j = 0; j < canvas_record_count(); j++) {
      const CanvasOp* label = canvas_record_op(j);
      if (!label || label->kind != CANVAS_OP_TEXT || label->font != FONT_UI) continue;
      if (label->y < track->y - 8 || label->y > track->y + 2) continue;
      const int gap = track->x - (label->x + label->w);
      if (gap < worst) worst = gap;
    }
  }
  return worst;
}

static int panel_grip_lines(unsigned colour) {
  int n = 0;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (o && o->kind == CANVAS_OP_LINE && o->colour == colour) n++;
  }
  return n;
}

void tb_0123_panel_drawn() {
  const int savedTab = menu_active_tab();
  const int savedScale = g_cfg.uiscale, savedAnim = g_cfg.menuanim;
  const int savedX = g_cfg.panelx, savedY = g_cfg.panely,
            savedW = g_cfg.panelw, savedH = g_cfg.panelh;
  const bool savedDirty = g_cfgDirty;
  // ANIM_OFF and a fixed clock for the whole block: colour and transform are the identity, so no fade/slide moves a
  // pinned coordinate.
  g_cfg.menuanim = ANIM_OFF;
  menu_set_now_ms(100000);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

  // 1. The minimum height is what the tallest tab needs, measured at the default box on 3440x1440 with scale at its
  // ceiling (150%, the tightest logical screen a real player reaches: 2293x960).
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
  g_cfg.uiscale = UI_SCALE_MAX;
  const int pct = menu_applied_scale(1440);
  CHECK(pct == ui_scale_max_for(1440, PANEL_H, PANEL_VINSET));
  const int logicalW = ui_unscale_px(3440, pct), logicalH = ui_unscale_px(1440, pct);
  CHECK(logicalW == 2293 && logicalH == 960);
  const PanelBox def = panel_layout(PanelBox{ -1, -1, 0, 0 }, logicalW, logicalH);
  int tallest = 0;
  for (int t = 0; t < TAB_COUNT; t++) {
    menu_set_tab(t);
    menu_draw(3440, 1440);                 // settle the held scale, button up
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const PanelBox drawn = menu_panel_box(logicalW, logicalH);
    CHECK(drawn.x == def.x && drawn.y == def.y && drawn.w == def.w && drawn.h == def.h);
    const int bottom = panel_content_bottom(pct, def);
    if (bottom > tallest) tallest = bottom;
  }
  // Panel height the tallest tab needs: from the panel's top to the lowest drawn thing, plus the PAD under the content
  // column.
  const int need = tallest - def.y + PAD;
  CHECK(PANEL_MIN_H >= need);
  CHECK(PANEL_MIN_H <= need + ROW_H);      // and not padded upward silently

  // 2. Nothing is dropped at the minimum box: at PANEL_MIN_W x PANEL_MIN_H every tab draws the lines it draws at the
  // default box, inside the right edge.
  g_cfg.uiscale = UI_SCALE_DEFAULT;        // device == logical, which tab_text_fits assumes
  for (int t = 0; t < TAB_COUNT; t++) {
    g_cfg.panelx = 100; g_cfg.panely = 60; g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_H;
    menu_set_tab(t);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(1920, 1080);
    canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const int full = panel_content_lines(UI_SCALE_DEFAULT,
                                         PanelBox{ 100, 60, PANEL_W, PANEL_H });
    g_cfg.panelw = PANEL_MIN_W; g_cfg.panelh = PANEL_MIN_H;
    menu_draw(1920, 1080);
    canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
    CHECK(!canvas_record_overflowed());
    const int least = panel_content_lines(UI_SCALE_DEFAULT,
                                          PanelBox{ 100, 60, PANEL_MIN_W, PANEL_MIN_H });
    CHECK(full > 0);
    CHECK(least == full);
    int worst = 0;
    CHECK(tab_text_fits(t, 1920, 1080, 18, &worst));
    CHECK(worst == 0);
    // No label runs under its own slider track (what sets PANEL_MIN_W); 8px is row_slider_at's readout-side gap.
    CHECK(panel_label_track_gap(t, 1920, 1080) >= 8);
  }

  // 3. A click that moved nothing stores nothing: a press/release with no travel must not replace the default box with
  // one computed for this screen.
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  g_cfgDirty = false;
  {
    const PanelBox d0 = menu_panel_box(1920, 1080);
    g_ui.mx = d0.x + PAD + 18; g_ui.my = d0.y + 1 + STRIP_H + STATUS_H / 2;
    g_ui.pressed = true; g_ui.down = true;
    menu_draw(1920, 1080);                                 // press
    g_ui.pressed = false; g_ui.down = false;
    menu_draw(1920, 1080);                                 // release, no travel
    CHECK(g_cfg.panelx == -1 && g_cfg.panely == -1);
    CHECK(g_cfg.panelw == 0 && g_cfg.panelh == 0);
    CHECK(!g_cfgDirty);
    const PanelBox d1 = menu_panel_box(1920, 1080);
    CHECK(d1.x == d0.x && d1.y == d0.y && d1.w == d0.w && d1.h == d0.h);
  }

  // The "Ins closes" text moves the panel now: any pixel no control claims is a grab (the foot-of-menu_draw rule).
  {
    g_cfg.panelx = 100; g_cfg.panely = 100;
    g_cfg.panelw = PANEL_MIN_W; g_cfg.panelh = PANEL_MIN_H;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(1920, 1080);
    canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
    const CanvasOp* ins = canvas_record_find_text("Ins closes");
    CHECK(ins != nullptr);
    if (ins) {
      g_ui.mx = ins->x + ins->w / 2;
      g_ui.my = ins->y + font_height[FONT_SMALL] / 2;
      CHECK(ui_hit(panel_title_rect(menu_panel_box(1920, 1080), STRIP_H, STATUS_H),
                   g_ui.mx, g_ui.my));
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 200; g_ui.my += 60;
      menu_draw(1920, 1080);
      const PanelBox moved = menu_panel_box(1920, 1080);
      CHECK(moved.x == 300 && moved.y == 160);
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 300 && g_cfg.panely == 160);
    }
  }

  // 4. A drag on the title band moves the rows.
  g_cfg.panelx = 100; g_cfg.panely = 100;
  g_cfg.panelw = PANEL_MIN_W; g_cfg.panelh = PANEL_MIN_H;
  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  const CanvasOp* key0 = canvas_record_find_text("Panel key");
  CHECK(key0 != nullptr);
  const int keyX0 = key0 ? key0->x : 0, keyY0 = key0 ? key0->y : 0;
  const PanelBox box0 = menu_panel_box(1920, 1080);
  CHECK(box0.x == 100 && box0.y == 100 && box0.w == PANEL_MIN_W && box0.h == PANEL_MIN_H);
  // Press lands on the status line, inside panel_title_rect and clear of the
  // "Ins closes" text.
  const int pressX = box0.x + PAD + 18, pressY = box0.y + 1 + STRIP_H + STATUS_H / 2;
  CHECK(ui_hit(panel_title_rect(box0, STRIP_H, STATUS_H), pressX, pressY));
  g_ui.mx = pressX; g_ui.my = pressY; g_ui.pressed = true; g_ui.down = true;
  menu_draw(1920, 1080);
  CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);   // the press alone moves nothing
  g_ui.pressed = false; g_ui.down = true;
  g_ui.mx = pressX + 200; g_ui.my = pressY + 100;
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  const CanvasOp* key1 = canvas_record_find_text("Panel key");
  CHECK(key1 != nullptr);
  CHECK(key1 && key1->x == keyX0 + 200 && key1->y == keyY0 + 100);
  CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);   // nothing is stored mid-drag
  const PanelBox box1 = menu_panel_box(1920, 1080);
  CHECK(box1.x == 300 && box1.y == 200 && box1.w == PANEL_MIN_W && box1.h == PANEL_MIN_H);
  g_cfgDirty = false;
  g_ui.pressed = false; g_ui.down = false;
  menu_draw(1920, 1080);                                // the release
  CHECK(g_cfg.panelx == 300 && g_cfg.panely == 200);
  CHECK(g_cfg.panelw == PANEL_MIN_W && g_cfg.panelh == PANEL_MIN_H);
  CHECK(g_cfgDirty);                                    // and it is saved
  menu_draw(1920, 1080);
  const PanelBox box2 = menu_panel_box(1920, 1080);
  CHECK(box2.x == 300 && box2.y == 200);

  // 4b. A press no control took moves the panel, from anywhere, not just the title bar; surfaces that move it and
  // controls that must not are checked below.
  {
    const VisState origVis = vis_snapshot();
    g_cfg.panelx = 100; g_cfg.panely = 100;
    g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_H;
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(1920, 1080);
    canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
    const CanvasOp* ddLab = canvas_record_find_text("Draw distance");
    const CanvasOp* ddTrk = find_track_at_label("Draw distance");
    const CanvasOp* seLab = canvas_record_find_text("Show enemies");
    const CanvasOp* combatTab = canvas_record_find_text("Combat");
    CHECK(ddLab != nullptr);
    CHECK(ddTrk != nullptr);
    CHECK(seLab != nullptr);
    CHECK(combatTab != nullptr);
    const int ddLabX     = ddLab ? ddLab->x : 0;
    const int ddLabY     = ddLab ? ddLab->y : 0;
    const int ddLabRight = ddLab ? ddLab->x + ddLab->w : 0;
    const int ddTrkX     = ddTrk ? ddTrk->x : 0;
    const int seLabY     = seLab ? seLab->y : 0;
    const int tabX       = combatTab ? combatTab->x : 0;
    const int tabY       = combatTab ? combatTab->y : 0;
    int lastRowY = 0;
    for (int i = 0; i < canvas_record_count(); i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o || o->kind != CANVAS_OP_TEXT) continue;
      if (o->x < 100 + RAIL_W) continue;
      if (o->y > lastRowY) lastRowY = o->y;
    }

    // (a) A press on a row's label (unspent space, so a grab); the row is a
    //     slider, whose value must not move while the panel does.
    if (ddLab && ddTrk) {
      CHECK(ddLabRight < ddTrkX);         // the label really is left of its own track
      g_cfg.espdrawdist = 6144;
      g_cfgDirty = false;
      g_ui.mx = ddLabX + 4; g_ui.my = ddLabY + 4;
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      CHECK(menu_panel_box(1920, 1080).x == 100);   // the press frame moves nothing
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 150; g_ui.my += 40;
      menu_draw(1920, 1080);
      const PanelBox mv = menu_panel_box(1920, 1080);
      CHECK(mv.x == 250 && mv.y == 140);            // ... and the frame after moves it
      CHECK(g_cfg.espdrawdist == 6144);             // the slider under the hand stood still
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 250 && g_cfg.panely == 140);
      CHECK(g_cfgDirty);
    }

    {
      g_cfg.panelx = 100; g_cfg.panely = 100;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(1920, 1080);
      const PanelBox pb = menu_panel_box(1920, 1080);
      const int ex = pb.x + RAIL_W + 40, ey = pb.y + pb.h - 40;
      CHECK(ey > lastRowY + ROW_H);                        // really below every row
      CHECK(!ui_hit(panel_grip_rect(pb), ex, ey));         // and not on the grip
      g_ui.mx = ex; g_ui.my = ey;
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 60; g_ui.my -= 30;
      menu_draw(1920, 1080);
      const PanelBox mv = menu_panel_box(1920, 1080);
      CHECK(mv.x == 160 && mv.y == 70);
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 160 && g_cfg.panely == 70);
    }

    // (c) a switch box is the switch's, not also a grab.
    {
      g_cfg.panelx = 100; g_cfg.panely = 100;
      g_cfg.espenemies = 0;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(1920, 1080);
      const PanelBox pb = menu_panel_box(1920, 1080);
      g_ui.mx = pb.x + pb.w - 21; g_ui.my = seLabY + 9;
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      CHECK(g_cfg.espenemies == 1);                 // the switch took the press
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 150; g_ui.my += 40;
      menu_draw(1920, 1080);
      const PanelBox after = menu_panel_box(1920, 1080);
      CHECK(after.x == 100 && after.y == 100);      // ... so nothing was armed
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);
    }

    // (d) the rail's click applies at the foot of menu_draw like the move rule, so a row-helpers-only rule would miss
    // it.
    {
      g_cfg.panelx = 100; g_cfg.panely = 100;
      menu_set_tab(TAB_VISUALS);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(1920, 1080);
      g_ui.mx = tabX + 4; g_ui.my = tabY + 4;
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      CHECK(menu_active_tab() == TAB_COMBAT);       // the rail took the press
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 150; g_ui.my += 40;
      menu_draw(1920, 1080);
      const PanelBox after = menu_panel_box(1920, 1080);
      CHECK(after.x == 100 && after.y == 100);
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);
    }

    // (e) a greyed row's label moves the panel: disabled rows are inert
    //     (ui_row_accepts_click refuses the click), so it's unspent space.
    {
      g_cfg.panelx = 100; g_cfg.panely = 100;
      g_cfg.espenemies = 0;                     // its four sub-rows go grey
      menu_set_tab(TAB_VISUALS);                // (d) above left the rail on Combat
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(1920, 1080);
      canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
      const CanvasOp* subLab = canvas_record_find_text("Bounding boxes");
      CHECK(subLab != nullptr);
      if (subLab) {
        const int boxesWas = g_cfg.espboxes;
        g_ui.mx = subLab->x + 4; g_ui.my = subLab->y + 4;
        g_ui.pressed = true; g_ui.down = true;
        menu_draw(1920, 1080);
        CHECK(g_cfg.espboxes == boxesWas);      // the greyed row took nothing
        CHECK(menu_panel_box(1920, 1080).x == 100);   // the press frame moves nothing
        g_ui.pressed = false; g_ui.down = true;
        g_ui.mx += 120; g_ui.my += 50;
        menu_draw(1920, 1080);
        const PanelBox mv = menu_panel_box(1920, 1080);
        CHECK(mv.x == 220 && mv.y == 150);      // ... and the frame after moves it
        CHECK(g_cfg.espboxes == boxesWas);
        g_ui.down = false;
        menu_draw(1920, 1080);
        CHECK(g_cfg.panelx == 220 && g_cfg.panely == 150);
      }
    }

    // (f) a press that moved nothing stores nothing here either: arming stamps s_dragBox, which a no-travel release
    // reads back; reset to the default box first so a stale stamp from an earlier drag would show.
    {
      g_cfg.panelx = 100; g_cfg.panely = 100;
      g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_H;
      menu_set_tab(TAB_VISUALS);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_draw(1920, 1080);
      const PanelBox pb = menu_panel_box(1920, 1080);
      g_ui.mx = pb.x + PAD + 18; g_ui.my = pb.y + 1 + STRIP_H + STATUS_H / 2;
      g_ui.pressed = true; g_ui.down = true;
      menu_draw(1920, 1080);
      g_ui.pressed = false; g_ui.down = true;
      g_ui.mx += 90; g_ui.my += 30;
      menu_draw(1920, 1080);
      g_ui.down = false;
      menu_draw(1920, 1080);
      CHECK(g_cfg.panelx == 190 && g_cfg.panely == 130);
      g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
      g_ui.mx = -1; g_ui.my = -1;
      menu_draw(1920, 1080);
      canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
      const CanvasOp* seLab2 = canvas_record_find_text("Show enemies");
      CHECK(seLab2 != nullptr);
      if (seLab2) {
        g_cfgDirty = false;
        g_ui.mx = seLab2->x + 4; g_ui.my = seLab2->y + 4;
        g_ui.pressed = true; g_ui.down = true;
        menu_draw(1920, 1080);                  // the press arms the move
        g_ui.pressed = false; g_ui.down = false;
        menu_draw(1920, 1080);                  // released exactly where it was pressed
        CHECK(g_cfg.panelx == -1 && g_cfg.panely == -1);
        CHECK(g_cfg.panelw == 0 && g_cfg.panelh == 0);
        CHECK(!g_cfgDirty);
      }
    }

    vis_restore(origVis);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(1920, 1080);
  }

  // 5. The grip: three diagonals, lit while the cursor is on them.
  g_cfg.panelx = 100; g_cfg.panely = 100; g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_H;
  menu_set_tab(TAB_VISUALS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  CHECK(panel_grip_lines(C_BONEFNT) == 3);
  CHECK(panel_grip_lines(C_BONEDIM) == 0);
  const PanelBox gbox = menu_panel_box(1920, 1080);
  const UiRect grip = panel_grip_rect(gbox);
  {
    bool inside = true, diagonal = true;
    for (int i = 0; i < canvas_record_count(); i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o || o->kind != CANVAS_OP_LINE) continue;
      const int x1 = o->x + o->w, y1 = o->y + o->h;
      if (o->x < grip.x || o->x > grip.x + grip.w ||
          o->y < grip.y || o->y > grip.y + grip.h ||
          x1  < grip.x || x1  > grip.x + grip.w ||
          y1  < grip.y || y1  > grip.y + grip.h) inside = false;
      if (!(o->w > 0 && o->h < 0)) diagonal = false;
    }
    CHECK(inside);
    CHECK(diagonal);
  }
  g_ui.mx = grip.x + grip.w / 2; g_ui.my = grip.y + grip.h / 2;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  CHECK(panel_grip_lines(C_BONEDIM) == 3);
  CHECK(panel_grip_lines(C_BONEFNT) == 0);

  // 6. A grip resize carries the rows: the track's left edge moves, not its width (row_slider_at anchors track and
  // readout to the row's right edge, so width cancels below the 114px fixed-offset cutoff every row here uses).
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  const CanvasOp* tr0 = find_track_at_label("Draw distance");
  CHECK(tr0 != nullptr);
  const int trX0 = tr0 ? tr0->x : 0, trW0 = tr0 ? tr0->w : 0;
  const UiRect grip2 = panel_grip_rect(menu_panel_box(1920, 1080));
  const int gripX = grip2.x + 6, gripY = grip2.y + 6;
  g_ui.mx = gripX; g_ui.my = gripY; g_ui.pressed = true; g_ui.down = true;
  menu_draw(1920, 1080);
  g_ui.pressed = false; g_ui.down = true; g_ui.mx = gripX + 200;
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  const CanvasOp* tr1 = find_track_at_label("Draw distance");
  CHECK(tr1 != nullptr);
  CHECK(tr1 && tr1->x == trX0 + 200);      // the column travelled with the edge
  CHECK(tr1 && tr1->w == trW0);            // and kept its width; see above
  CHECK(menu_panel_box(1920, 1080).w == PANEL_W + 200);

  // No row acts while a drag is in flight: dragging the grip past the minimum stops the panel while the cursor keeps
  // going, landing over a content row with the button held. Both button states are checked: a slider acts on HELD, a
  // switch on the PRESS EDGE.
  {
    const VisState orig = vis_snapshot();
    g_ui.pressed = false; g_ui.down = true;
    g_ui.mx = 350; g_ui.my = 400;
    menu_draw(1920, 1080);
    const PanelBox small = menu_panel_box(1920, 1080);
    CHECK(small.w == PANEL_MIN_W && small.h == PANEL_MIN_H);   // stopped at the minimum
    CHECK(g_ui.mx > small.x + RAIL_W && g_ui.mx < small.x + small.w);  // over the content
    CHECK(g_ui.my > small.y + 1 + STRIP_H + STATUS_H && g_ui.my < small.y + small.h);
    canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
    const CanvasOp* dd = find_track_at_label("Draw distance");
    const CanvasOp* se = canvas_record_find_text("Show enemies");
    CHECK(dd != nullptr);
    CHECK(se != nullptr);
    // Slider: button held, at the track's far right end, value parked at the
    // other end of its range so a landing drag would move it.
    g_cfg.espdrawdist = ESP_DRAWDIST_MIN;
    const VisState before = vis_snapshot();
    if (dd) { g_ui.mx = dd->x + dd->w; g_ui.my = dd->y + 4; }
    g_ui.pressed = false; g_ui.down = true;
    menu_draw(1920, 1080);
    CHECK(menu_panel_box(1920, 1080).w == PANEL_MIN_W);   // still clamped, still dragging
    CHECK(vis_changed(before) == 0);
    // Switch: press edge on a top-level row, always live so it would flip on
    // any click that reached it.
    if (se) { g_ui.mx = small.x + small.w - 21; g_ui.my = se->y + 9; }
    g_ui.pressed = true; g_ui.down = true;
    menu_draw(1920, 1080);
    CHECK(menu_panel_box(1920, 1080).w == PANEL_MIN_W);
    CHECK(vis_changed(before) == 0);
    vis_restore(orig);
    g_ui.pressed = false; g_ui.down = false;
    menu_draw(1920, 1080);                                     // the release
    CHECK(g_cfg.panelw == PANEL_MIN_W && g_cfg.panelh == PANEL_MIN_H);
    CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);          // only the size moved
  }

  // 7. The UI scale ceiling follows the stored height.
  g_cfg.uiscale = UI_SCALE_MAX;
  g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_H;
  const int tallCeiling = menu_applied_scale(1440);
  CHECK(tallCeiling == ui_scale_max_for(1440, PANEL_H, PANEL_VINSET));
  g_cfg.panelh = PANEL_MIN_H;
  const int shortCeiling = menu_applied_scale(1440);
  CHECK(shortCeiling == ui_scale_max_for(1440, PANEL_MIN_H, PANEL_VINSET));
  CHECK(shortCeiling > tallCeiling);       // a shorter panel can be drawn bigger
  // An unset height reads as PANEL_H, so a fresh config's ceiling is the
  // same one this panel has always had.
  g_cfg.panelh = 0;
  CHECK(menu_applied_scale(1440) == tallCeiling);

  g_cfg.panelx = 100; g_cfg.panely = 30;
  g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_MIN_H;
  g_cfg.uiscale = UI_SCALE_DEFAULT;
  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  const CanvasOp* ut = find_track_at_label("UI scale");
  CHECK(ut != nullptr);
  if (ut) {
    g_ui.mx = ut->x + ut->w + 4; g_ui.my = ut->y + 4;
    g_ui.down = true; g_ui.pressed = false;
    menu_draw(1920, 1080);
    CHECK(g_cfg.uiscale == ui_scale_max_for(1080, PANEL_MIN_H, PANEL_VINSET));
    CHECK(g_cfg.uiscale != ui_scale_max_for(1080, PANEL_H, PANEL_VINSET));
  }

  // 8. The ceiling is floored at PANEL_MIN_H: a hand-edited panelh below the minimum must not ask for a lower ceiling,
  // since panel_layout raises it when it draws.
  g_cfg.uiscale = UI_SCALE_MAX;
  g_cfg.panelw = PANEL_W; g_cfg.panelh = PANEL_MIN_H;
  const int minCeiling = menu_applied_scale(1080);
  g_cfg.panelh = 1;
  CHECK(menu_applied_scale(1080) == minCeiling);
  // ...and a draw at that ceiling still shows Combat's last row: the panel
  // actually drawn is PANEL_MIN_H tall regardless of the stored 1.
  g_cfg.uiscale = minCeiling;
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0;
  menu_set_tab(TAB_COMBAT);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  canvas_record_begin(); menu_draw(1920, 1080); canvas_record_end();
  CHECK(!canvas_record_overflowed());
  CHECK(canvas_record_find_text("Max redirect") != nullptr);

  // 9. A close mid-drag discards the drag: the button never comes up while the panel is shut (overlay.cpp stops calling
  // menu_draw), so without menu_note_real_close resetting s_drag/s_dragLive the next open would commit the stale drag
  // on its first frame.
  g_cfg.uiscale = UI_SCALE_DEFAULT;   // undo section 8's minCeiling or the stored
                                      // y=100 clamps against a smaller logical screen
  g_cfg.panelx = 100; g_cfg.panely = 100;
  g_cfg.panelw = PANEL_MIN_W; g_cfg.panelh = PANEL_MIN_H;
  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(1920, 1080);
  {
    const PanelBox before = menu_panel_box(1920, 1080);
    const int pressX = before.x + PAD + 18, pressY = before.y + 1 + STRIP_H + STATUS_H / 2;
    g_ui.mx = pressX; g_ui.my = pressY; g_ui.pressed = true; g_ui.down = true;
    menu_draw(1920, 1080);                                // press: arms the move
    g_ui.pressed = false; g_ui.down = true;
    g_ui.mx = pressX + 200; g_ui.my = pressY + 100;
    menu_draw(1920, 1080);                                // mid-drag, button still held
    CHECK(menu_panel_box(1920, 1080).x == before.x + 200); // live, moved
    g_cfgDirty = false;
    menu_keycap_close();                                  // the panel closes mid-drag
    g_ui.pressed = false; g_ui.down = false;
    g_ui.mx = -1; g_ui.my = -1;
    menu_draw(1920, 1080);                                // the reopen's first frame
    CHECK(g_cfg.panelx == 100 && g_cfg.panely == 100);     // the drag never committed
    CHECK(!g_cfgDirty);
    const PanelBox after = menu_panel_box(1920, 1080);
    CHECK(after.x == before.x && after.y == before.y);     // back to the stored box
  }

  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_cfg.uiscale = savedScale; g_cfg.menuanim = savedAnim;
  g_cfg.panelx = savedX; g_cfg.panely = savedY;
  g_cfg.panelw = savedW; g_cfg.panelh = savedH;
  menu_set_tab(savedTab);
  menu_set_now_ms(0);
  menu_draw(1920, 1080);              // settle the drag machine back to idle
  g_cfgDirty = savedDirty;
}
