// The numeric entry field's rules: parse, format, the state machine and the scancode table, pure so every decision is
// pinned here.
#include "../entry.h"
#include "../tooltip.h"   // TOOLTIP_DELAY_MS/ANIM_OFF for tb_0125's cursor-rest wait
#include "test_common.h"

void tb_0124_entry_rules() {
  bool ok = false;
  CHECK(entry_parse("90", EK_INT, 70, 140, &ok) == 90 && ok);
  CHECK(entry_parse("999", EK_INT, 70, 140, &ok) == 140 && ok);     // clamped
  CHECK(entry_parse("", EK_INT, 70, 140, &ok) == 0 && !ok);          // empty refuses
  CHECK(entry_parse("-", EK_INT, -10, 10, &ok) == 0 && !ok);
  CHECK(entry_parse("-5", EK_INT, -10, 10, &ok) == -5 && ok);
  CHECK(entry_parse("0.040", EK_MILLI, 5, 200, &ok) == 40 && ok);
  CHECK(entry_parse("0.04", EK_MILLI, 5, 200, &ok) == 40 && ok);
  CHECK(entry_parse(".2", EK_MILLI, 5, 200, &ok) == 200 && ok);
  CHECK(entry_parse("1", EK_MILLI, 5, 200, &ok) == 200 && ok);       // 1.000 clamps to the max
  CHECK(entry_parse("0.0004", EK_MILLI, 5, 200, &ok) == 5 && ok);    // the fourth decimal is dropped, then clamped
  CHECK(entry_parse("12", EK_METRES, 512, 32768, &ok) == 12 * BUILD_UNITS_PER_METRE && ok);
  CHECK(entry_parse("0", EK_METRES, 512, 32768, &ok) == 512 && ok);
  CHECK(entry_parse("99999999999", EK_INT, 0, 100, &ok) == 100 && ok); // no overflow: saturates
  CHECK(entry_parse("9999999999999999999999999", EK_INT, 0, 100, &ok) == 100 && ok);
  CHECK(entry_parse("9999999999999999999999999", EK_MILLI, 5, 200, &ok) == 200 && ok);
  CHECK(entry_parse("9999999999999999999999999", EK_METRES, 512, 32768, &ok) == 32768 && ok);
  CHECK(entry_parse("3000000", EK_INT, 0, 100000000, &ok) == (int)ENTRY_WHOLE_CAP && ok);
  CHECK(ENTRY_WHOLE_CAP > (long long)ESP_DRAWDIST_MAX);
  char f[16];
  entry_format(f, sizeof f, 90, EK_INT);    CHECK(streq(f, "90"));
  entry_format(f, sizeof f, 40, EK_MILLI);  CHECK(streq(f, "0.040"));
  entry_format(f, sizeof f, 6144, EK_METRES); CHECK(streq(f, "12"));
  entry_format(f, sizeof f, -7, EK_INT);    CHECK(streq(f, "-7"));
  EntryState s = entry_begin(); CHECK(!s.open);
  EntryResult r = entry_step(s, ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "90");
  CHECK(r.state.open && r.state.rowKey == 5 && streq(r.state.text, "90") && !r.commit);
  r = entry_step(r.state, ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0); CHECK(streq(r.state.text, "9"));
  r = entry_step(r.state, ENTRY_EV_CHAR, '1', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "91"));
  r = entry_step(r.state, ENTRY_EV_CHAR, 'x', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "91"));   // refused
  r = entry_step(r.state, ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "91"));   // no '.' for EK_INT
  r = entry_step(r.state, ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "91"));   // '-' only first, only when lo < 0
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);     CHECK(r.commit && r.value == 91 && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "90");
  r = entry_step(r.state, ENTRY_EV_ESCAPE, 0, 0, 0, 0, 0, 0);    CHECK(!r.commit && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "9");
  r = entry_step(r.state, ENTRY_EV_CHAR, '1', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CLICKAWAY, 0, 0, 0, 0, 0, 0); CHECK(r.commit && r.value == 91 && !r.state.open);
  // An untouched field commits nothing: EK_METRES rounds its display (5000 units shows "9", reads back as 4608), so an
  // untyped Enter or click-away must not move the row.
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "90");
  r = entry_step(r.state, ENTRY_EV_CLICKAWAY, 0, 0, 0, 0, 0, 0); CHECK(!r.commit && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, "9");
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);     CHECK(!r.commit && !r.state.open);
  // The commit compares text against the prefill, not the `edited` flag alone: a keystroke pair that restores the
  // original text must not commit.
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, "9");
  r = entry_step(r.state, ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);       // "99"
  r = entry_step(r.state, ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);    // ... and back to "9"
  CHECK(streq(r.state.text, "9") && r.state.edited == 1);           // the flag still says so
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
  CHECK(!r.commit && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, "9");
  r = entry_step(r.state, ENTRY_EV_SELECT_ALL, 0, 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
  CHECK(streq(r.state.text, "9") && r.state.edited == 1);
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
  CHECK(!r.commit && !r.state.open);
  {
    char pre[13];
    entry_format(pre, sizeof pre, 5000, EK_METRES);
    CHECK(streq(pre, "9"));
    EntryResult rt = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, pre);
    rt = entry_step(rt.state, ENTRY_EV_SELECT_ALL, 0, 0, 0, 0, 0, 0);
    rt = entry_step(rt.state, ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    rt = entry_step(rt.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
    CHECK(!rt.commit);                                  // so 5000 stays 5000
  }
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, "9");
  r = entry_step(r.state, ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CHAR, '8', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
  CHECK(r.commit && r.value == 8 * BUILD_UNITS_PER_METRE && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 9, EK_METRES, 512, 32768, "9");
  r = entry_step(r.state, ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CHAR, '1', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CLICKAWAY, 0, 0, 0, 0, 0, 0);
  CHECK(r.commit && r.value == 1 * BUILD_UNITS_PER_METRE && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "90");
  r = entry_step(r.state, ENTRY_EV_CHAR, 'x', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);     CHECK(!r.commit && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "9");
  r = entry_step(r.state, ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);     CHECK(!r.commit && !r.state.open);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 0, 100, "");
  for (int i = 0; i < 12; i++) r = entry_step(r.state, ENTRY_EV_CHAR, '1', 0, 0, 0, 0, 0);
  CHECK(r.state.len == ENTRY_TEXT_CAP);
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, -10, 10, "");
  r = entry_step(r.state, ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "-"));
  r = entry_step(r.state, ENTRY_EV_CHAR, '5', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "-5"));  // '-' only first
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_MILLI, 5, 200, "");
  r = entry_step(r.state, ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);    CHECK(streq(r.state.text, "."));
  r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_NONE, 0, 1, "Closest"); CHECK(!r.state.open);
  CHECK(entry_char_of_scancode(30) == '1' && entry_char_of_scancode(38) == '9' && entry_char_of_scancode(39) == '0');
  CHECK(entry_char_of_scancode(89) == '1' && entry_char_of_scancode(97) == '9' && entry_char_of_scancode(98) == '0');
  CHECK(entry_char_of_scancode(45) == '-' && entry_char_of_scancode(86) == '-');
  CHECK(entry_char_of_scancode(55) == '.' && entry_char_of_scancode(99) == '.');
  CHECK(entry_char_of_scancode(4) == 0 && entry_char_of_scancode(41) == 0 && entry_char_of_scancode(-1) == 0);

  // The format/parse round trip is exact for EK_INT and EK_MILLI, but EK_METRES rounds DOWN to the metre; stepping by 1
  // rather than 512 exercises the loss.
  const int kinds[3] = { EK_INT, EK_MILLI, EK_METRES };
  const int los[3]   = { 70, 5, 512 };
  const int his[3]   = { 140, 200, 32768 };
  for (int k = 0; k < 3; k++) {
    for (int v = los[k]; v <= his[k]; v++) {
      char t[13];
      entry_format(t, sizeof t, v, kinds[k]);
      bool rok = false;
      const int back = entry_parse(t, kinds[k], los[k], his[k], &rok);
      const int want = (kinds[k] == EK_METRES)
                     ? (v / BUILD_UNITS_PER_METRE) * BUILD_UNITS_PER_METRE
                     : v;
      CHECK(back == want && rok);
    }
  }
  for (int m = 1; m <= 64; m++) {
    char t[13];
    entry_format(t, sizeof t, m * BUILD_UNITS_PER_METRE, EK_METRES);
    bool rok = false;
    CHECK(entry_parse(t, EK_METRES, 512, 32768, &rok) == m * BUILD_UNITS_PER_METRE && rok);
  }

  // input.cpp can't link into test_host (it hooks live SDL), so the suite drives its poll hook's pair,
  // menu_entry_open()/menu_entry_key().
  int prow = -1, pval = 0;
  CHECK(!menu_entry_open());
  CHECK(!menu_entry_pending(&prow, &pval));
  menu_entry_key(30);                     // a key with no field open does nothing at all
  CHECK(!menu_entry_open() && !menu_entry_pending(&prow, &pval));
  menu_entry_begin_edit(7, EK_INT, 70, 140, "90");
  CHECK(menu_entry_open());
  menu_entry_key(42);                     // Backspace -> "9"
  menu_entry_key(30);                     // '1'       -> "91"
  CHECK(menu_entry_open() && !menu_entry_pending(&prow, &pval));
  menu_entry_key(4);                      // 'A': not a field key, refused, field still open
  CHECK(menu_entry_open());
  menu_entry_key(40);                     // Return commits and closes
  CHECK(!menu_entry_open());
  CHECK(menu_entry_pending(&prow, &pval) && prow == 7 && pval == 91);
  menu_entry_begin_edit(9, EK_METRES, 512, 32768, "12");
  menu_entry_key(41);
  CHECK(!menu_entry_open());
  CHECK(menu_entry_pending(&prow, &pval) && prow == 7 && pval == 91);
  menu_entry_begin_edit(9, EK_METRES, 512, 32768, "1");
  menu_entry_key(31);                     // '2' -> "12"
  menu_entry_key(88);
  CHECK(menu_entry_pending(&prow, &pval) && prow == 9 && pval == 12 * BUILD_UNITS_PER_METRE);
  menu_entry_begin_edit(9, EK_INT, 0, 10, "5");
  CHECK(menu_entry_open());
  menu_entry_close();
  CHECK(!menu_entry_open());
  // The pending commit goes too: left behind, the slider helper would apply it next panel session over whatever the row
  // holds.
  CHECK(!menu_entry_pending(&prow, &pval));
  menu_entry_begin_edit(3, EK_NONE, 0, 1, "Closest");
  CHECK(!menu_entry_open());

  g_ui.rdown = false; g_ui.rpressed = false;
  CHECK(!ui_press_edge(g_ui.rdown, false));
  g_ui.rdown = true;
  CHECK(ui_press_edge(g_ui.rdown, false) && !ui_press_edge(g_ui.rdown, true));
  g_ui.rdown = false; g_ui.rpressed = false;
}

// The reset button's well or rim inside a row's band, found by SHAPE (20x20, the only such box any row draws), since
// colour is under test.
static const CanvasOp* find_reset_box(int kind, int rowY) {
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (!o || o->kind != kind) continue;
    if (o->w != 20 || o->h != 20) continue;
    if (o->y < rowY - 10 || o->y > rowY + 28) continue;
    return o;
  }
  return nullptr;
}

// Ops the open field draws, told from the row's by POSITION not colour (C_HAZARDDIM and C_BONE are reused); w == -1
// means any width.
static int field_ops(int kind, unsigned colour, int fromX, int y0, int y1, int w,
                     const CanvasOp** first) {
  int n = 0;
  if (first) *first = nullptr;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (!o || o->kind != kind || o->colour != colour) continue;
    if (o->x < fromX) continue;
    if (o->y < y0 || o->y + o->h > y1) continue;
    if (w >= 0 && o->w != w) continue;
    if (first && !*first) *first = o;
    n++;
  }
  return n;
}

// The field and its glyph, drawn: the right-click that opens a field on a real slider row, its well/caret, keys, the
// reset glyph, and what an open field takes away (the row's drag and the tooltip). Rows are found by label or readout,
// so a layout change moves the check.
void tb_0125_slider_entry_drawn() {
  const int savedTab = menu_active_tab();
  const Config savedCfg = g_cfg;
  const int  savedMx = g_ui.mx, savedMy = g_ui.my;
  const bool savedDown = g_ui.down, savedPressed = g_ui.pressed;
  const bool savedDirty = g_cfgDirty;
  const int SW = 3440, SH = 1440;

  g_cfg.uiscale = UI_SCALE_DEFAULT;
  g_cfg.menuanim = ANIM_OFF;
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
  menu_set_now_ms(200000);
  menu_entry_close();
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_ui.rdown = false; g_ui.rpressed = false;

  menu_set_tab(TAB_VIEW);
  g_cfg.fovon = 1;
  g_cfg.fov = FOV_DEFAULT;
  menu_draw(SW, SH);
  canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
  CHECK(!canvas_record_overflowed());
  const CanvasOp* fovTrack = find_track_at_label("Field of view");
  CHECK(fovTrack != nullptr);
  int ftx = 0, fty = 0, ftw = 0;
  if (fovTrack) { ftx = fovTrack->x; fty = fovTrack->y; ftw = fovTrack->w; }
  if (fovTrack) {
    g_ui.mx = ftx + ftw / 2; g_ui.my = fty + 2;
    g_ui.rdown = true; g_ui.rpressed = true;
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    g_ui.rpressed = false;
    CHECK(menu_entry_open());
    CHECK(streq(menu_entry_text(), "90"));
    CHECK(canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD) != nullptr);
    CHECK(canvas_record_find_text("90") != nullptr);
    // The track no longer takes a drag: a left press at its far LEFT end would store FOV_MIN; it is a click away, so
    // the field closes.
    const int fovBefore = g_cfg.fov;
    g_ui.mx = ftx + 2; g_ui.my = fty + 2;
    g_ui.down = true; g_ui.pressed = true;
    menu_draw(SW, SH);
    CHECK(!menu_entry_open());
    CHECK(g_cfg.fov == fovBefore);
    g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
    // A press INSIDE the field's well is not a drag either: the well is drawn over the track's right end, which would
    // read it as a drag to the top.
    g_ui.mx = ftx + ftw / 2; g_ui.my = fty + 2;
    g_ui.rpressed = true; menu_draw(SW, SH); g_ui.rpressed = false;
    CHECK(menu_entry_open());
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* well = canvas_record_find_kind(CANVAS_OP_ROUNDRECT, C_WELL);
    CHECK(well != nullptr);
    if (well) {
      const int wx = well->x + 3, wy = well->y + well->h / 2;
      g_cfg.fov = FOV_MIN;
      g_ui.mx = wx; g_ui.my = wy;
      g_ui.down = true; g_ui.pressed = true;
      menu_draw(SW, SH);
      CHECK(menu_entry_open());
      CHECK(g_cfg.fov == FOV_MIN);
      g_ui.pressed = false;
      menu_draw(SW, SH);
      CHECK(g_cfg.fov == FOV_MIN);
      g_ui.down = false;
      menu_draw(SW, SH);
    }
    menu_entry_close();
    g_cfg.fov = FOV_DEFAULT;
    menu_draw(SW, SH);
  }

  // The keys, through the scancodes the poll hook forwards; commits are applied by the row on its next drawn frame.
  if (fovTrack) {
    g_ui.mx = ftx + ftw / 2; g_ui.my = fty + 2;
    g_ui.rpressed = true; menu_draw(SW, SH); g_ui.rpressed = false;
    CHECK(menu_entry_open());
    menu_entry_key(42); menu_entry_key(42);          // Backspace, Backspace
    menu_entry_key(30); menu_entry_key(39); menu_entry_key(39);   // 1, 0, 0
    menu_entry_key(40);                              // Return
    menu_draw(SW, SH);
    CHECK(g_cfg.fov == 100);
    g_ui.rpressed = true; menu_draw(SW, SH); g_ui.rpressed = false;
    for (int i = 0; i < 4; i++) menu_entry_key(42);
    menu_entry_key(38); menu_entry_key(38); menu_entry_key(38);   // 999
    menu_entry_key(40);
    menu_draw(SW, SH);
    CHECK(g_cfg.fov == FOV_MAX);
    g_ui.rpressed = true; menu_draw(SW, SH); g_ui.rpressed = false;
    for (int i = 0; i < 4; i++) menu_entry_key(42);
    menu_entry_key(34); menu_entry_key(39);                       // 50
    menu_entry_key(40);
    menu_draw(SW, SH);
    CHECK(g_cfg.fov == FOV_MIN);
    const int fovKept = g_cfg.fov;
    g_ui.rpressed = true; menu_draw(SW, SH); g_ui.rpressed = false;
    for (int i = 0; i < 4; i++) menu_entry_key(42);
    menu_entry_key(31); menu_entry_key(39);                       // 20, below FOV_MIN
    menu_entry_key(41);                                           // Escape
    menu_draw(SW, SH);
    CHECK(!menu_entry_open());
    CHECK(g_cfg.fov == fovKept);
  }
  g_ui.rdown = false; g_ui.rpressed = false;

  // A click away onto the tab rail still lands: menu_draw defers the rail's tab switch to its foot, so the owning row
  // takes the value first.
  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1;
    g_cfg.fov = FOV_DEFAULT;
    menu_entry_close();
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    const CanvasOp* combatTab = canvas_record_find_text("Combat");
    CHECK(fovLab != nullptr && combatTab != nullptr);
    if (fovLab && combatTab) {
      g_ui.mx = fovLab->x + 4; g_ui.my = fovLab->y + 4;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      for (int i = 0; i < 4; i++) menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(31); menu_entry_key(39);   // "120"
      CHECK(streq(menu_entry_text(), "120"));
      g_cfgDirty = false;
      g_ui.mx = combatTab->x + 4; g_ui.my = combatTab->y + 4;
      g_ui.down = true; g_ui.pressed = true;
      menu_draw(SW, SH);
      g_ui.down = false; g_ui.pressed = false;
      CHECK(!menu_entry_open());
      CHECK(g_cfg.fov == 120);          // the row took it on the frame of the click
      CHECK(g_cfgDirty);                // ... and it is on its way to the ini
      CHECK(menu_active_tab() == TAB_COMBAT);
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      CHECK(canvas_record_find_text("Field of view") == nullptr);
      CHECK(canvas_record_find_text("Pick target") != nullptr);
    }
    menu_entry_close();
  }

  // A press on the title band commits the field too: click-away runs BEFORE the drag machine reads the press.
  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1; g_cfg.fov = FOV_DEFAULT;
    g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
    menu_entry_close();
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    CHECK(fovLab != nullptr);
    if (fovLab) {
      g_ui.mx = fovLab->x + 4; g_ui.my = fovLab->y + 4;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      for (int i = 0; i < 4; i++) menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(30); menu_entry_key(39);   // "110"
      const UiRect pr = menu_panel_rect(SW, SH);
      g_cfgDirty = false;
      g_ui.mx = pr.x + pr.w / 2; g_ui.my = pr.y + 2;
      g_ui.down = true; g_ui.pressed = true;
      menu_draw(SW, SH);
      CHECK(!menu_entry_open());
      CHECK(g_cfg.fov == 110);
      CHECK(g_cfgDirty);
      g_ui.pressed = false;
      menu_draw(SW, SH);
      g_ui.down = false;
      menu_draw(SW, SH);
      CHECK(g_cfg.panelx == -1 && g_cfg.panelw == 0);
    }
    menu_entry_close();
    g_cfg.fov = FOV_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1;
    menu_draw(SW, SH);
  }

  // A right press outside the field commits it too: another numeric row, or nothing (rail, switch, empty space).
  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1; g_cfg.freecamon = 1;
    g_cfg.fov = FOV_DEFAULT;
    g_cfg.freecamspeed = FREECAM_SPEED_DEFAULT;
    g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
    menu_entry_close();
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    const CanvasOp* spdLab = canvas_record_find_text("Speed");
    CHECK(fovLab != nullptr && spdLab != nullptr);
    if (fovLab && spdLab) {
      const int fx = fovLab->x + 4, fy = fovLab->y + 4;
      const int sx = spdLab->x + 4, sy = spdLab->y + 4;
      char want[16];
      sprintf_s(want, "%d", g_cfg.freecamspeed);
      g_ui.mx = fx; g_ui.my = fy;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      for (int i = 0; i < 4; i++) menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(31); menu_entry_key(39);   // "120"
      g_ui.mx = sx; g_ui.my = sy;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(g_cfg.fov == 120);
      CHECK(menu_entry_open());
      CHECK(streq(menu_entry_text(), want));
      menu_entry_close();
      g_ui.mx = fx; g_ui.my = fy;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      for (int i = 0; i < 4; i++) menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(30); menu_entry_key(39);   // "110"
      const UiRect pr = menu_panel_rect(SW, SH);
      const int tabWas = menu_active_tab();
      g_ui.mx = pr.x + 4; g_ui.my = pr.y + pr.h / 2;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(!menu_entry_open());
      CHECK(g_cfg.fov == 110);
      CHECK(menu_active_tab() == tabWas);   // a right press moves no tab
    }
    menu_entry_close();
    g_cfg.fov = FOV_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1;
    menu_draw(SW, SH);
  }

  {
    menu_set_tab(TAB_COMBAT);
    g_cfg.aimon = 1; g_cfg.silenton = 1;
    g_ui.mx = -1; g_ui.my = -1;
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* pick = canvas_record_find_text("Pick target");
    CHECK(pick != nullptr);
    if (pick) {
      g_ui.mx = pick->x + 4; g_ui.my = pick->y + 4;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(!menu_entry_open());
    }
  }

  {
    menu_set_tab(TAB_VIEW);
    // At the widest readout on purpose: at FOV_MAX the readout and the glyph's reserve are the same width, so "left of
    // the readout" is a real bound.
    g_cfg.fovon = 1; g_cfg.fov = FOV_MAX;
    g_ui.mx = -1; g_ui.my = -1;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* lab = canvas_record_find_text("Field of view");
    const CanvasOp* trkOff = find_track_at_label("Field of view");
    CHECK(lab != nullptr && trkOff != nullptr);
    int rowY = lab ? lab->y : 0;
    const int labX = lab ? lab->x : 0;
    const int trackWOff = trkOff ? trkOff->w : 0;
    if (lab) { g_ui.mx = labX + 4; g_ui.my = rowY + 4; }
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* readout = find_readout_at_y(rowY);
    const CanvasOp* trkOn = find_track_at_label("Field of view");
    CHECK(readout != nullptr && trkOn != nullptr);
    const int readoutX = readout ? readout->x : 0;
    const int trkOnRight = trkOn ? trkOn->x + trkOn->w : 0;
    // The glyph's room is reserved every frame, so hovering must not shorten the track (which would move the knob under
    // the hand).
    CHECK(trkOn != nullptr && trkOn->w == trackWOff);
    // The icon's ops: every CANVAS_OP_LINE in the row's band plus the arrowhead's 3x3 rect, folded into the ink box so
    // centring measures the whole icon.
    int lines = 0, heads = 0;
    int gx0 = 1 << 30, gy0 = 1 << 30, gx1 = -(1 << 30), gy1 = -(1 << 30);
    for (int i = 0; i < canvas_record_count(); i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o) continue;
      if (o->y < rowY - 10 || o->y > rowY + 28) continue;
      int ax, ay, bx, by;
      if (o->kind == CANVAS_OP_LINE) {
        lines++;
        ax = o->x; bx = o->x + o->w; ay = o->y; by = o->y + o->h;
      } else if (o->kind == CANVAS_OP_RECT && o->w == 3 && o->h == 3) {
        heads++;
        ax = o->x; bx = o->x + o->w - 1; ay = o->y; by = o->y + o->h - 1;
      } else continue;
      if (ax < gx0) gx0 = ax;
      if (bx < gx0) gx0 = bx;
      if (ay < gy0) gy0 = ay;
      if (by < gy0) gy0 = by;
      if (ax > gx1) gx1 = ax;
      if (bx > gx1) gx1 = bx;
      if (ay > gy1) gy1 = ay;
      if (by > gy1) gy1 = by;
    }
    // 15 line ops: the ring drawn twice (radius 6 and 5, 7 segments each, the 8th omitted so it isn't closed) plus one
    // barb.
    CHECK(lines == 15);
    CHECK(heads == 1);       // one filled head
    CHECK(readout != nullptr && gx1 < readoutX);      // left of the readout column
    CHECK(gx0 > trkOnRight);                          // ... and right of the track's end
    CHECK(gx1 - gx0 == 13 && gy1 - gy0 == 12);
    const int bx0 = gx0 - 4, by0 = gy0 - 4;
    bool ringClosed = false;
    for (int i = 0; i < canvas_record_count(); i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o || o->kind != CANVAS_OP_LINE) continue;
      const int ax = o->x, ay = o->y, bx = o->x + o->w, by = o->y + o->h;
      const bool a6End = (ax == bx0 + 11 && ay == by0 + 16);  // RING6[7]
      const bool b6End = (bx == bx0 + 11 && by == by0 + 16);
      const bool a6Tip = (ax == bx0 + 16 && ay == by0 + 10);  // RING6[0]
      const bool b6Tip = (bx == bx0 + 16 && by == by0 + 10);
      if ((a6End && b6Tip) || (a6Tip && b6End)) ringClosed = true;
      const bool a5End = (ax == bx0 + 11 && ay == by0 + 15);  // RING5[7]
      const bool b5End = (bx == bx0 + 11 && by == by0 + 15);
      const bool a5Tip = (ax == bx0 + 15 && ay == by0 + 10);  // RING5[0]
      const bool b5Tip = (bx == bx0 + 15 && by == by0 + 10);
      if ((a5End && b5Tip) || (a5Tip && b5End)) ringClosed = true;
    }
    CHECK(!ringClosed);
    // The button it sits in: a well shaped like the hotkey field (4 px rounded box, 1 px rim) at the switch box's 20x20
    // size, in its reserved room.
    int wellX = 0, wellY = 0;
    {
      const CanvasOp* wellOp = find_reset_box(CANVAS_OP_ROUNDRECT, rowY);
      const CanvasOp* rimOp  = find_reset_box(CANVAS_OP_ROUNDOUTLINE, rowY);
      CHECK(wellOp != nullptr);
      CHECK(rimOp != nullptr);
      if (wellOp) { wellX = wellOp->x; wellY = wellOp->y; }
      if (wellOp && rimOp) {
        CHECK(rimOp->x == wellOp->x && rimOp->y == wellOp->y);
        CHECK(rimOp->w == wellOp->w && rimOp->h == wellOp->h);
        CHECK(wellOp->colour == C_WELL);
        CHECK(rimOp->colour == C_RAISED);
        CHECK(wellOp->x > trkOnRight);
        CHECK(wellOp->x + wellOp->w < readoutX);
        CHECK(gx0 - wellOp->x == (wellOp->x + wellOp->w - 1) - gx1);
        CHECK(gy0 - wellOp->y == (wellOp->y + wellOp->h - 1) - gy1 + 1);
        CHECK(wellOp->y == rowY - 1);
      }
      int headAt = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (!o || o->kind != CANVAS_OP_RECT) continue;
        if (o->w != 3 || o->h != 3) continue;
        if (o->x == bx0 + 15 && o->y == by0 + 9) headAt++;
      }
      CHECK(headAt == 1);
    }
    // A left press inside the glyph restores the default, tested at the button's left edge where the track's grab box
    // overlaps it (it reaches past the track's end to keep the range top reachable): "reset" and "drag to the top"
    // disagree there.
    if (lines == 15) {
      g_cfg.fov = FOV_MAX;
      g_cfgDirty = false;
      g_ui.mx = wellX; g_ui.my = wellY + 10;
      g_ui.down = true; g_ui.pressed = true;
      menu_draw(SW, SH);
      CHECK(g_cfg.fov == FOV_DEFAULT);
      CHECK(g_cfgDirty);                  // a reset that MOVED the row is a write
      g_ui.pressed = false;
      menu_draw(SW, SH);
      CHECK(g_cfg.fov == FOV_DEFAULT);
      g_ui.down = false;
      menu_draw(SW, SH);
      // A reset on a row already at its default is not a write: it must leave the row and the ini alone rather than
      // rewrite identical contents.
      g_cfgDirty = false;
      g_ui.down = true; g_ui.pressed = true;
      menu_draw(SW, SH);
      CHECK(g_cfg.fov == FOV_DEFAULT);
      CHECK(!g_cfgDirty);
      g_ui.down = false; g_ui.pressed = false;
      menu_draw(SW, SH);
    }
    if (lines == 15) {
      const int gcx = (gx0 + gx1) / 2, gcy = (gy0 + gy1) / 2;
      unsigned t = 300000u;
      g_ui.mx = -1; g_ui.my = -1;
      menu_set_now_ms(t); menu_draw(SW, SH);
      g_ui.mx = gcx; g_ui.my = gcy;
      menu_set_now_ms(t + 16); menu_draw(SW, SH);
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS + 1); menu_draw(SW, SH);
      CHECK(streq(menu_tooltip_text(), MENU_RESET_HINT));
      CHECK(strlen(MENU_RESET_HINT) < 63);
      // The spec's copy, pinned as the literal it is: the string is named in the spec, so it isn't the panel's to
      // reword.
      CHECK(streq(MENU_RESET_HINT, "Reset to default"));
    }
    // The button lights under the cursor under Simple (ANIM_OFF is the hover-blend identity): fill to C_RAISED, rim to
    // C_BONEDIM, as row_bind's hotkey field.
    if (lines == 15) {
      const int savedAnimHere = g_cfg.menuanim;
      g_cfg.menuanim = ANIM_SIMPLE;
      unsigned t = 340000u;
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_set_now_ms(t); menu_draw(SW, SH);
      g_ui.mx = labX + 4; g_ui.my = rowY + 4;
      for (int i = 0; i < 6; i++) { t += 50; menu_set_now_ms(t); menu_draw(SW, SH); }
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      const CanvasOp* coldFill = find_reset_box(CANVAS_OP_ROUNDRECT, rowY);
      const CanvasOp* coldRim  = find_reset_box(CANVAS_OP_ROUNDOUTLINE, rowY);
      CHECK(coldFill != nullptr);
      CHECK(coldRim != nullptr);
      if (coldFill) CHECK(coldFill->colour == C_WELL);
      if (coldRim)  CHECK(coldRim->colour == C_RAISED);
      g_ui.mx = wellX + 10; g_ui.my = wellY + 10;
      for (int i = 0; i < 6; i++) { t += 50; menu_set_now_ms(t); menu_draw(SW, SH); }
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      const CanvasOp* hotFill = find_reset_box(CANVAS_OP_ROUNDRECT, rowY);
      const CanvasOp* hotRim  = find_reset_box(CANVAS_OP_ROUNDOUTLINE, rowY);
      CHECK(hotFill != nullptr);
      CHECK(hotRim != nullptr);
      if (hotFill) CHECK(hotFill->colour == C_RAISED);
      if (hotRim)  CHECK(hotRim->colour == C_BONEDIM);
      if (coldFill && hotFill) {
        CHECK(hotFill->x == coldFill->x && hotFill->y == coldFill->y);
        CHECK(hotFill->w == coldFill->w && hotFill->h == coldFill->h);
      }
      g_cfg.menuanim = savedAnimHere;
      g_ui.mx = -1; g_ui.my = -1;
      menu_set_now_ms(t + 10000u); menu_draw(SW, SH);
    }
  }

  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1; g_cfg.freecamon = 1;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    unsigned t = 400000u;
    menu_set_now_ms(t); menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    const CanvasOp* cull = canvas_record_find_text("Disable culling");
    CHECK(fovLab != nullptr && cull != nullptr);
    if (fovLab && cull) {
      const int fx = fovLab->x + 4, fy = fovLab->y + 4;
      const int cx = cull->x + 4, cy = cull->y + 4;
      g_ui.mx = fx; g_ui.my = fy;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_set_now_ms(t + 16); menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      g_ui.mx = cx; g_ui.my = cy;
      menu_set_now_ms(t + 32); menu_draw(SW, SH);
      menu_set_now_ms(t + 32 + TOOLTIP_DELAY_MS + 1); menu_draw(SW, SH);
      CHECK(menu_entry_open());
      CHECK(menu_tooltip_text() == nullptr);
      menu_entry_close();
      g_ui.mx = -1; g_ui.my = -1;
      menu_set_now_ms(t + 5000); menu_draw(SW, SH);
      g_ui.mx = cx; g_ui.my = cy;
      menu_set_now_ms(t + 5016); menu_draw(SW, SH);
      menu_set_now_ms(t + 5016 + TOOLTIP_DELAY_MS + 1); menu_draw(SW, SH);
      CHECK(menu_tooltip_text() != nullptr);
    }
  }

  // A reopen shows no tooltip before the delay: close_menu() leaves g_ui.mx/my, so an unreset rest timer would show it
  // over a panel still fading in.
  {
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    unsigned t = 500000u;
    menu_set_now_ms(t); menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* dd = canvas_record_find_text("Draw distance");
    CHECK(dd != nullptr);
    if (dd) {
      g_ui.mx = dd->x + 4; g_ui.my = dd->y + 4;
      menu_set_now_ms(t + 16); menu_draw(SW, SH);
      menu_set_now_ms(t + 16 + TOOLTIP_DELAY_MS + 1); menu_draw(SW, SH);
      CHECK(menu_tooltip_text() != nullptr);          // shown, with the panel open
      menu_keycap_close();                            // the panel really closes
      menu_set_now_ms(t + 16 + 20 * TOOLTIP_DELAY_MS);
      menu_draw(SW, SH);                              // the reopen's first frame
      CHECK(menu_tooltip_text() == nullptr);
    }
  }

  // A commit is clamped to the row's bounds AS THEY ARE NOW, not those the field opened with: "UI scale"'s hi depends
  // on the panel's current height.
  {
    menu_set_tab(TAB_SETTINGS);
    g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
    g_cfg.uiscale = UI_SCALE_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* uiLab = canvas_record_find_text("UI scale");
    CHECK(uiLab != nullptr);
    if (uiLab) {
      const int ceilingNow = ui_scale_max_for(SH, PANEL_H, PANEL_VINSET);
      CHECK(ceilingNow > UI_SCALE_DEFAULT);      // room to type above the default
      g_ui.mx = uiLab->x + 4; g_ui.my = uiLab->y + 4;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      for (int i = 0; i < 4; i++) menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(33); menu_entry_key(39);   // "140"
      g_cfg.panelh = 1400;
      const int ceilingTall = ui_scale_max_for(SH, 1400, PANEL_VINSET);
      CHECK(ceilingTall < UI_SCALE_DEFAULT);
      g_ui.mx = -1; g_ui.my = -1;
      menu_entry_key(40);                                           // Return
      menu_draw(SW, SH);
      // 140 clamps to ceilingTall on the frame the value lands, so nothing changes and the player's own 100 stays in
      // the ini.
      CHECK(g_cfg.uiscale == UI_SCALE_DEFAULT);
    }
    menu_entry_close();
    g_cfg.panelh = 0;
    g_cfg.uiscale = UI_SCALE_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1;
    menu_draw(SW, SH);
  }

  // Every numeric slider on every tab: walk each right-aligned FONT_MONO readout starting with a digit; right-pressing
  // must open a field showing that number, unit stripped.
  {
    g_cfg.fovon = 1; g_cfg.freecamon = 1;
    g_cfg.aimon = 1; g_cfg.silenton = 1; g_cfg.tboton = 1;
    g_cfg.mouseaimon = 1;
    g_ui.down = false; g_ui.pressed = false;
    int found = 0, matched = 0;
    for (int tab = 0; tab < TAB_COUNT; tab++) {
      menu_set_tab(tab);
      menu_entry_close();
      g_ui.mx = -1; g_ui.my = -1; g_ui.rdown = false; g_ui.rpressed = false;
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      CHECK(!canvas_record_overflowed());
      const UiRect pr = menu_panel_rect(SW, SH);
      const int valueRight = pr.x + pr.w - PAD - 1 - VALUE_MARGIN;
      int rx[24], ry[24], n = 0;
      char want[24][24];
      for (int i = 0; i < canvas_record_count() && n < 24; i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (!o || o->kind != CANVAS_OP_TEXT || o->font != FONT_MONO) continue;
        if (o->x + o->w != valueRight) continue;
        if (o->text[0] < '0' || o->text[0] > '9') continue;   // a NAMED readout is a word
        int k = 0;
        while (k < 23 && ((o->text[k] >= '0' && o->text[k] <= '9') || o->text[k] == '.')) {
          want[n][k] = o->text[k]; k++;
        }
        want[n][k] = 0;
        rx[n] = o->x + 2; ry[n] = o->y; n++;
      }
      for (int i = 0; i < n; i++) {
        found++;
        menu_set_tab(tab);
        menu_entry_close();
        g_ui.mx = rx[i]; g_ui.my = ry[i];
        g_ui.rdown = true; g_ui.rpressed = true;
        menu_draw(SW, SH);
        g_ui.rdown = false; g_ui.rpressed = false;
        if (menu_entry_open() && streq(menu_entry_text(), want[i])) matched++;
      }
      menu_entry_close();
    }
    CHECK(found == 14);
    CHECK(matched == found);
  }

  // Select, copy, cut, paste: entry.h holds the rules (tb_0126); this pins the mouse landing on a real field, the
  // selection drawn, and scancodes with modifiers.
  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1; g_cfg.fov = FOV_DEFAULT;
    g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
    menu_entry_close();
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    CHECK(fovLab != nullptr);
    if (fovLab) {
      g_ui.mx = fovLab->x + 4; g_ui.my = fovLab->y + 4;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      menu_entry_key(42); menu_entry_key(42);
      menu_entry_key(30); menu_entry_key(31); menu_entry_key(32);
      menu_entry_key(33); menu_entry_key(34);
      CHECK(streq(menu_entry_text(), "12345"));
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      const CanvasOp* fieldText = canvas_record_find_text("12345");
      const CanvasOp* fieldRim  = canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD);
      CHECK(fieldText != nullptr && fieldRim != nullptr);
      if (fieldText && fieldRim) {
        const int textX = fieldText->x;
        const int adv   = fieldText->w / 5;
        const int midY  = fieldRim->y + fieldRim->h / 2;
        const int band0 = fieldRim->y, band1 = fieldRim->y + fieldRim->h;
        CHECK(adv > 0 && fieldText->w == 5 * adv);
        g_ui.mx = textX + 3 * adv; g_ui.my = midY;
        g_ui.down = true; g_ui.pressed = true;
        canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
        g_ui.pressed = false;
        CHECK(menu_entry_open());                       // a press INSIDE is not a click away
        CHECK(field_ops(CANVAS_OP_RECT, C_HAZARDDIM, textX, band0, band1, -1, nullptr) == 0);
        const CanvasOp* caret = nullptr;
        CHECK(field_ops(CANVAS_OP_RECT, C_BONE, textX, band0, band1, 1, &caret) == 1);
        CHECK(caret != nullptr && caret->x == textX + 3 * adv + 1);
        g_ui.mx = textX;
        canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
        const CanvasOp* sel = nullptr;
        CHECK(field_ops(CANVAS_OP_RECT, C_HAZARDDIM, textX, band0, band1, -1, &sel) == 1);
        CHECK(sel != nullptr);
        if (sel) {
          CHECK(sel->x == textX);
          CHECK(sel->w == 3 * adv);
          CHECK(sel->y > fieldRim->y && sel->y + sel->h < fieldRim->y + fieldRim->h);
        }
        caret = nullptr;
        CHECK(field_ops(CANVAS_OP_RECT, C_BONE, textX, band0, band1, 1, &caret) == 1);
        CHECK(caret != nullptr && caret->x == textX + 1);
        g_ui.down = false;
        menu_draw(SW, SH);
        menu_entry_key(42);
        CHECK(streq(menu_entry_text(), "45"));
      }
      menu_entry_key_mod(SDL_SCANCODE_A, false, true);
      menu_entry_key(36);                                  // '7'
      CHECK(streq(menu_entry_text(), "7"));
      menu_entry_key(30); menu_entry_key(31); menu_entry_key(32);
      menu_entry_key(33); menu_entry_key(34);              // "712345"
      CHECK(streq(menu_entry_text(), "712345"));
      menu_entry_key_mod(SDL_SCANCODE_LEFT, true, false);
      menu_entry_key_mod(SDL_SCANCODE_LEFT, true, false);
      menu_entry_key(42);
      CHECK(streq(menu_entry_text(), "7123"));
      menu_entry_key_mod(SDL_SCANCODE_HOME, false, false);
      menu_entry_key(38);                                  // '9'
      CHECK(streq(menu_entry_text(), "97123"));
      menu_entry_key_mod(SDL_SCANCODE_DELETE, false, false);
      CHECK(streq(menu_entry_text(), "9123"));
      // A paste replaces the selection. menu_entry_paste is what input.cpp calls once clipboard_read_text has answered
      // (DLL-only, clipboard.h).
      menu_entry_key_mod(SDL_SCANCODE_A, false, true);
      menu_entry_paste("99");
      CHECK(streq(menu_entry_text(), "99"));
      // COPY AND CUT hand the text back for input.cpp to put on the system clipboard, and they alone do: any other key
      // answers nullptr, so a keystroke that copied nothing can't overwrite the player's clipboard.
      CHECK(menu_entry_key_mod(30, false, false) == nullptr);
      CHECK(menu_entry_key_mod(SDL_SCANCODE_V, false, true) == nullptr);
      CHECK(streq(menu_entry_text(), "991"));              // ... and the digit still typed
      menu_entry_key(42);
      const char* copied = menu_entry_key_mod(SDL_SCANCODE_C, false, true);
      CHECK(copied != nullptr && streq(copied, "99"));     // nothing selected: the whole field
      CHECK(streq(menu_entry_text(), "99"));               // ... and a copy is not an edit
      menu_entry_key_mod(SDL_SCANCODE_A, false, true);
      const char* cut = menu_entry_key_mod(SDL_SCANCODE_X, false, true);
      CHECK(cut != nullptr && streq(cut, "99"));
      CHECK(streq(menu_entry_text(), ""));
      menu_entry_paste("99");
      menu_entry_key(40);
      menu_draw(SW, SH);
      CHECK(g_cfg.fov == 99);
    }
    menu_entry_close();
    g_cfg.fov = FOV_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
  }

  // A caret drag does not survive into the next field: a right press opening another row's field while the left button
  // is still held must not fire a stray SET_CARET. The drag latch is cleared where a field opens.
  {
    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1; g_cfg.freecamon = 1;
    g_cfg.fov = FOV_DEFAULT;
    g_cfg.freecamspeed = FREECAM_SPEED_DEFAULT;
    g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
    menu_entry_close();
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    g_ui.rdown = false; g_ui.rpressed = false;
    menu_draw(SW, SH);
    canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
    const CanvasOp* fovLab = canvas_record_find_text("Field of view");
    const CanvasOp* spdLab = canvas_record_find_text("Speed");
    CHECK(fovLab != nullptr && spdLab != nullptr);
    if (fovLab && spdLab) {
      const int fx = fovLab->x + 4, fy = fovLab->y + 4;
      const int sx = spdLab->x + 4, sy = spdLab->y + 4;
      g_ui.mx = fx; g_ui.my = fy;
      g_ui.rdown = true; g_ui.rpressed = true;
      menu_draw(SW, SH);
      g_ui.rdown = false; g_ui.rpressed = false;
      CHECK(menu_entry_open());
      canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
      const CanvasOp* rim = canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD);
      CHECK(rim != nullptr);
      if (rim) {
        g_ui.mx = rim->x + rim->w / 2; g_ui.my = rim->y + rim->h / 2;
        g_ui.down = true; g_ui.pressed = true;
        menu_draw(SW, SH);
        g_ui.pressed = false;
        CHECK(menu_entry_open());
        char want[16];
        sprintf_s(want, "%d", g_cfg.freecamspeed);
        g_ui.mx = sx; g_ui.my = sy;
        g_ui.rdown = true; g_ui.rpressed = true;
        canvas_record_begin(); menu_draw(SW, SH); canvas_record_end();
        g_ui.rdown = false; g_ui.rpressed = false;
        CHECK(menu_entry_open());
        CHECK(streq(menu_entry_text(), want));
        const CanvasOp* newText = canvas_record_find_text(want);
        const CanvasOp* newRim  = canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD);
        CHECK(newText != nullptr && newRim != nullptr);
        if (newText && newRim) {
          const int n     = (int)strlen(want);
          const int textX = newText->x;
          const int adv   = newText->w / n;
          const int band0 = newRim->y, band1 = newRim->y + newRim->h;
          CHECK(adv > 0 && newText->w == n * adv);
          CHECK(field_ops(CANVAS_OP_RECT, C_HAZARDDIM, textX, band0, band1, -1, nullptr) == 0);
          const CanvasOp* caret = nullptr;
          CHECK(field_ops(CANVAS_OP_RECT, C_BONE, textX, band0, band1, 1, &caret) == 1);
          CHECK(caret != nullptr && caret->x == textX + n * adv + 1);
        }
        g_ui.down = false;
        menu_draw(SW, SH);
      }
    }
    menu_entry_close();
    g_cfg.fov = FOV_DEFAULT;
    g_ui.mx = -1; g_ui.my = -1;
    g_ui.down = false; g_ui.pressed = false;
    menu_draw(SW, SH);
  }

  menu_entry_close();
  menu_set_now_ms(0);
  menu_set_tab(savedTab);
  g_cfg = savedCfg;
  g_cfgDirty = savedDirty;
  g_ui.mx = savedMx; g_ui.my = savedMy;
  g_ui.down = savedDown; g_ui.pressed = savedPressed;
  g_ui.rdown = false; g_ui.rpressed = false;
}

// A field open on `text` with caret/anchor set through the machine's own events, so every pin rests on a reachable
// state.
static EntryState opened(const char* text, int kind, int lo, int hi,
                         int caret, int anchor) {
  EntryResult r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, kind, lo, hi, text);
  r = entry_step(r.state, ENTRY_EV_SET_CARET, anchor, 0, EK_NONE, 0, 0, 0);
  r = entry_step(r.state, ENTRY_EV_SET_CARET, caret, 1, EK_NONE, 0, 0, 0);
  return r.state;
}

// Selection, caret and clipboard, all pure. tb_0124 pins the first six machine events; this pins the other ten. Caret
// and anchor are text indices; caret == anchor means no selection.
void tb_0126_entry_selection() {
  // The same three glyphs select whichever direction the sweep runs; direction is remembered so Shift+Left after a
  // rightward sweep shrinks it.
  {
    const EntryState fwd = opened("12345", EK_INT, 0, 99999, 4, 1);
    const EntryState rev = opened("12345", EK_INT, 0, 99999, 1, 4);
    CHECK(entry_sel_lo(fwd) == 1 && entry_sel_hi(fwd) == 4);
    CHECK(entry_sel_lo(rev) == 1 && entry_sel_hi(rev) == 4);
    CHECK(fwd.caret == 4 && fwd.anchor == 1);
    CHECK(rev.caret == 1 && rev.anchor == 4);
    CHECK(fwd.edited == 0 && rev.edited == 0);
  }

  // Open puts the caret at the end and selects nothing: selecting the prefill would cost the first keystroke of an
  // append.
  {
    EntryResult r = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 5, EK_INT, 70, 140, "90");
    CHECK(r.state.caret == 2 && r.state.anchor == 2 && r.state.len == 2);
    r = entry_step(r.state, ENTRY_EV_CHAR, '1', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "901"));      // a first keystroke still APPENDS
    CHECK(r.state.caret == 3 && r.state.anchor == 3 && r.state.edited == 1);
  }

  {
    EntryResult r = entry_step(opened("12345", EK_INT, 0, 99999, 3, 3),
                               ENTRY_EV_LEFT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 2 && r.state.anchor == 2 && r.state.edited == 0);
    r = entry_step(r.state, ENTRY_EV_RIGHT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 3 && r.state.anchor == 3);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 0, 0), ENTRY_EV_LEFT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 0 && r.state.anchor == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 5, 5), ENTRY_EV_RIGHT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 5 && r.state.anchor == 5);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 3, 3), ENTRY_EV_LEFT, 1, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 2 && r.state.anchor == 3);
    r = entry_step(r.state, ENTRY_EV_LEFT, 1, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 1 && r.state.anchor == 3);
    CHECK(entry_sel_lo(r.state) == 1 && entry_sel_hi(r.state) == 3);
    r = entry_step(r.state, ENTRY_EV_RIGHT, 1, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 2 && r.state.anchor == 3);
    CHECK(r.state.edited == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 4, 1), ENTRY_EV_LEFT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 1 && r.state.anchor == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 4, 1), ENTRY_EV_RIGHT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 4 && r.state.anchor == 4);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_LEFT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 1 && r.state.anchor == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_RIGHT, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 4 && r.state.anchor == 4);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_HOME, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 0 && r.state.anchor == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_HOME, 1, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 0 && r.state.anchor == 2);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_END, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 5 && r.state.anchor == 5);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_END, 1, 0, 0, 0, 0, 0);
    CHECK(r.state.caret == 5 && r.state.anchor == 2);
    CHECK(r.state.edited == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_SELECT_ALL, 0, 0, 0, 0, 0, 0);
    CHECK(r.state.anchor == 0 && r.state.caret == 5 && r.state.edited == 0);
    r = entry_step(r.state, ENTRY_EV_SET_CARET, 99, 0, EK_NONE, 0, 0, 0);
    CHECK(r.state.caret == 5 && r.state.anchor == 5);
    r = entry_step(r.state, ENTRY_EV_SET_CARET, -4, 0, EK_NONE, 0, 0, 0);
    CHECK(r.state.caret == 0 && r.state.anchor == 0);
    r = entry_step(r.state, ENTRY_EV_SET_CARET, 3, 1, EK_NONE, 0, 0, 0);
    CHECK(r.state.caret == 3 && r.state.anchor == 0);
  }

  {
    EntryResult r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4),
                               ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "195") && r.state.len == 3);
    CHECK(r.state.caret == 2 && r.state.anchor == 2 && r.state.edited == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 4, 1), ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "195") && r.state.caret == 2);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 0, 0), ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "912345") && r.state.caret == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_CHAR, 'x', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    CHECK(r.state.caret == 1 && r.state.anchor == 4);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    r = entry_step(opened("1.234", EK_MILLI, 0, 99999, 1, 3), ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "1.34"));
    r = entry_step(opened("1.234", EK_MILLI, 0, 99999, 4, 4), ENTRY_EV_CHAR, '.', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "1.234") && r.state.edited == 0);
  }

  {
    EntryResult r = entry_step(opened("5", EK_INT, -10, 10, 0, 0),
                               ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "-5") && r.state.caret == 1 && r.state.edited == 1);
    r = entry_step(opened("5", EK_INT, -10, 10, 1, 1), ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "5") && r.state.edited == 0);
    r = entry_step(opened("-5", EK_INT, -10, 10, 0, 0), ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "-5") && r.state.edited == 0);
    r = entry_step(opened("-5", EK_INT, -10, 10, 0, 1), ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "-5") && r.state.edited == 1);
    r = entry_step(opened("5", EK_INT, 0, 10, 0, 0), ENTRY_EV_CHAR, '-', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "5") && r.state.edited == 0);
  }

  {
    EntryResult r = entry_step(opened("12345678901", EK_INT, 0, 99999, 11, 11),
                               ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(r.state.len == ENTRY_TEXT_CAP && streq(r.state.text, "12345678901"));
    CHECK(r.state.edited == 0);                       // a full field refuses, as it always did
    r = entry_step(opened("12345678901", EK_INT, 0, 99999, 11, 0),
                   ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "9") && r.state.len == 1 && r.state.edited == 1);
  }

  {
    EntryResult r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4),
                               ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "15") && r.state.caret == 1 && r.state.edited == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_DELETE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "15") && r.state.caret == 1 && r.state.edited == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "1345") && r.state.caret == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_DELETE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "1245") && r.state.caret == 2);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 0, 0), ENTRY_EV_BACKSPACE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0 && r.state.caret == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 5, 5), ENTRY_EV_DELETE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0 && r.state.caret == 5);
    r = entry_step(r.state, ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
    CHECK(!r.commit && !r.state.open);
  }

  {
    EntryResult r = entry_step(opened("", EK_INT, 0, 99999, 0, 0),
                               ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "1a2.3");
    CHECK(streq(r.state.text, "123") && r.state.edited == 1 && r.state.caret == 3);
    r = entry_step(opened("", EK_MILLI, 0, 99999, 0, 0), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "1a2.3");
    CHECK(streq(r.state.text, "12.3"));
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "99");
    CHECK(streq(r.state.text, "1995") && r.state.caret == 3);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 0, 0), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "99");
    CHECK(streq(r.state.text, "9912345") && r.state.caret == 2);
    r = entry_step(opened("", EK_INT, 0, 99999, 0, 0), ENTRY_EV_PASTE, 0, 0, 0, 0, 0,
                   "123456789012345");
    CHECK(streq(r.state.text, "12345678901") && r.state.len == ENTRY_TEXT_CAP);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "abc");
    CHECK(streq(r.state.text, "15") && r.state.edited == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "abc");
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    CHECK(r.state.caret == 2 && r.state.anchor == 2);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "");
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_PASTE, 0, 0, 0, 0, 0, 0);
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
  }

  {
    EntryResult r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4),
                               ENTRY_EV_COPY, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 1 && streq(r.clip, "234"));
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    CHECK(r.state.caret == 1 && r.state.anchor == 4);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_COPY, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 1 && streq(r.clip, "12345") && r.state.edited == 0);
    r = entry_step(opened("", EK_INT, 0, 99999, 0, 0), ENTRY_EV_COPY, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 0 && streq(r.clip, ""));
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_CUT, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 1 && streq(r.clip, "234"));
    CHECK(streq(r.state.text, "15") && r.state.caret == 1 && r.state.edited == 1);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 2, 2), ENTRY_EV_CUT, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 1 && streq(r.clip, "12345"));
    CHECK(streq(r.state.text, "12345") && r.state.edited == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_CHAR, '9', 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 0 && streq(r.clip, ""));
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_SELECT_ALL, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 0);
    r = entry_step(opened("12345", EK_INT, 0, 99999, 1, 4), ENTRY_EV_ENTER, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 0);
  }

  {
    const EntryState shut = entry_begin();
    EntryResult r = entry_step(shut, ENTRY_EV_SELECT_ALL, 0, 0, 0, 0, 0, 0);
    CHECK(!r.state.open && r.state.len == 0 && r.hasClip == 0);
    r = entry_step(shut, ENTRY_EV_PASTE, 0, 0, 0, 0, 0, "123");
    CHECK(!r.state.open && r.state.len == 0);
    r = entry_step(shut, ENTRY_EV_COPY, 0, 0, 0, 0, 0, 0);
    CHECK(r.hasClip == 0);
    r = entry_step(shut, ENTRY_EV_DELETE, 0, 0, 0, 0, 0, 0);
    CHECK(!r.state.open && r.state.edited == 0);
  }

  // entry_index_at: a pixel back to a caret index, the mouse's half of the geometry menu.cpp draws with; rounds to each
  // glyph's middle.
  {
    const int x0 = 100, adv = 10, len = 5;
    CHECK(entry_index_at(len, x0, x0, adv) == 0);
    CHECK(entry_index_at(len, x0, 109, adv) == 1);
    CHECK(entry_index_at(len, x0, 110, adv) == 1);
    CHECK(entry_index_at(len, x0, 111, adv) == 1);
    CHECK(entry_index_at(len, x0, 104, adv) == 0);
    CHECK(entry_index_at(len, x0, 105, adv) == 1);
    CHECK(entry_index_at(len, x0, 114, adv) == 1);
    CHECK(entry_index_at(len, x0, 115, adv) == 2);
    CHECK(entry_index_at(len, x0, 0, adv) == 0);
    CHECK(entry_index_at(len, x0, -500, adv) == 0);
    CHECK(entry_index_at(len, x0, 1000, adv) == len);
    CHECK(entry_index_at(len, x0, x0 + len * adv, adv) == len);
    CHECK(entry_index_at(0, x0, 1000, adv) == 0);
    CHECK(entry_index_at(-3, x0, 1000, adv) == 0);
    CHECK(entry_index_at(len, x0, 1000, 0) == 0);
    CHECK(entry_index_at(len, x0, 1000, -4) == 0);
  }
}
