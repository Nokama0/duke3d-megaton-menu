#include "test_common.h"
// TOOLTIP_DELAY_MS, for tooltip_for_row below; tooltip.h also pulls in ANIM_OFF via anim.h.
#include "../tooltip.h"

// The prelude's definitions, with external linkage so the area files share one copy of the state.

VisState vis_snapshot() {
  VisState v = { g_cfg.espenemies, g_cfg.espboxes, g_cfg.esphealth, g_cfg.espnames,
                 g_cfg.espdistance, g_cfg.espitems, g_cfg.espsecrets, g_cfg.espdrawdist,
                 g_cfg.espexplodables, g_cfg.espswitches, g_cfg.fullbright,
                 g_cfg.dmgnumbers, g_cfg.secrettriggers, g_cfg.secretvolume,
                 g_cfg.espbreakables };
  return v;
}
void vis_restore(const VisState& v) {
  g_cfg.espenemies = v.enemies; g_cfg.espboxes = v.boxes; g_cfg.esphealth = v.health;
  g_cfg.espnames = v.names; g_cfg.espdistance = v.distance; g_cfg.espitems = v.items;
  g_cfg.espsecrets = v.secrets; g_cfg.espdrawdist = v.drawdist;
  g_cfg.espexplodables = v.explodables; g_cfg.espswitches = v.switches;
  g_cfg.fullbright = v.fullbright;
  g_cfg.dmgnumbers = v.dmgnumbers;
  g_cfg.secrettriggers = v.secrettriggers;
  g_cfg.secretvolume = v.secretvolume;
  g_cfg.espbreakables = v.breakables;
}
int vis_changed(const VisState& before) {
  int m = 0;
  if (g_cfg.espenemies  != before.enemies)  m |= VF_ENEMIES;
  if (g_cfg.espboxes    != before.boxes)    m |= VF_BOXES;
  if (g_cfg.esphealth   != before.health)   m |= VF_HEALTH;
  if (g_cfg.espnames    != before.names)    m |= VF_NAMES;
  if (g_cfg.espdistance != before.distance) m |= VF_DISTANCE;
  if (g_cfg.espitems    != before.items)    m |= VF_ITEMS;
  if (g_cfg.espsecrets  != before.secrets)  m |= VF_SECRETS;
  if (g_cfg.espdrawdist != before.drawdist) m |= VF_DRAWDIST;
  if (g_cfg.espexplodables != before.explodables) m |= VF_EXPLODABLES;
  if (g_cfg.espswitches != before.switches) m |= VF_SWITCHES;
  if (g_cfg.fullbright != before.fullbright) m |= VF_FULLBRIGHT;
  if (g_cfg.dmgnumbers != before.dmgnumbers) m |= VF_DMGNUMBERS;
  if (g_cfg.secrettriggers != before.secrettriggers) m |= VF_SECRETTRIGGERS;
  if (g_cfg.secretvolume != before.secretvolume) m |= VF_SECRETVOLUME;
  if (g_cfg.espbreakables != before.breakables) m |= VF_BREAKABLES;
  return m;
}
int vis_sweep(int screenW, int screenH, const VisState& start, bool held,
                     bool* multi) {
  // The panel is laid out in logical pixels, drawn and clicked in device ones, so the sweep covers the applied scale
  // (not the stored one, capped by ui_scale_max_for). One settling draw with the button up comes first: the applied
  // scale holds its pre-drag value while the button is down.
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(screenW, screenH);
  const int pct = menu_applied_scale(screenH);
  const UiRect lp = menu_panel_rect(ui_unscale_px(screenW, pct), ui_unscale_px(screenH, pct));
  const UiRect p = { ui_scale_px(lp.x, pct), ui_scale_px(lp.y, pct),
                     ui_scale_px(lp.w, pct), ui_scale_px(lp.h, pct) };
  int all = 0;
  if (multi) *multi = false;
  for (int y = p.y; y < p.y + p.h; y += SWEEP_STRIDE) {
    for (int x = p.x; x < p.x + p.w; x += SWEEP_STRIDE) {
      vis_restore(start);
      menu_set_tab(TAB_VISUALS);      // a click in the rail would move it
      g_ui.mx = x; g_ui.my = y;
      g_ui.down = held; g_ui.pressed = !held;
      menu_draw(screenW, screenH);
      const int m = vis_changed(start);
      all |= m;
      if (multi && m && (m & (m - 1))) *multi = true;
    }
  }
  g_ui.down = false; g_ui.pressed = false;
  vis_restore(start);
  return all;
}
CombatState combat_snapshot() {
  CombatState c = { g_cfg.aimon, g_cfg.aimhold, g_cfg.aimbind, g_cfg.aimtarget, g_cfg.aimpoint,
                    g_cfg.aimsmooth, g_cfg.aimcone, g_cfg.aimlead, g_cfg.aimdebug,
                    g_cfg.aimleadscale, g_cfg.silenton, g_cfg.silentmaxangle, g_cfg.aimrange };
  return c;
}
void combat_restore(const CombatState& c) {
  g_cfg.aimon = c.aimon; g_cfg.aimhold = c.aimhold; g_cfg.aimbind = c.aimbind;
  g_cfg.aimtarget = c.aimtarget; g_cfg.aimpoint = c.aimpoint; g_cfg.aimsmooth = c.aimsmooth;
  g_cfg.aimcone = c.aimcone; g_cfg.aimlead = c.aimlead; g_cfg.aimdebug = c.aimdebug;
  g_cfg.aimleadscale = c.aimleadscale;
  g_cfg.silenton = c.silenton; g_cfg.silentmaxangle = c.silentmaxangle;
  g_cfg.aimrange = c.aimrange;
  // The aimbot's button row is a capture row: an arm left from a previous sweep pixel would change what the next draw
  // shows, so it is disarmed once here, not at each restore point.
  menu_keycap_cancel();
}
int combat_changed(const CombatState& before) {
  int m = 0;
  if (g_cfg.aimon         != before.aimon)         m |= CF_AIMON;
  if (g_cfg.aimhold       != before.aimhold)       m |= CF_AIMHOLD;
  if (g_cfg.aimbind       != before.aimbind)       m |= CF_AIMBUTTON;
  if (g_cfg.aimtarget     != before.aimtarget)     m |= CF_AIMTARGET;
  if (g_cfg.aimpoint      != before.aimpoint)      m |= CF_AIMPOINT;
  if (g_cfg.aimsmooth     != before.aimsmooth)     m |= CF_AIMSMOOTH;
  if (g_cfg.aimcone       != before.aimcone)       m |= CF_AIMCONE;
  if (g_cfg.aimlead       != before.aimlead)       m |= CF_AIMLEAD;
  if (g_cfg.aimdebug      != before.aimdebug)      m |= CF_AIMDEBUG;
  if (g_cfg.aimleadscale  != before.aimleadscale)  m |= CF_AIMLEADSCALE;
  if (g_cfg.silenton      != before.silenton)      m |= CF_SILENTON;
  if (g_cfg.silentmaxangle != before.silentmaxangle) m |= CF_SILENTMAXANGLE;
  if (g_cfg.aimrange      != before.aimrange)      m |= CF_AIMRANGE;
  return m;
}
int combat_sweep(int screenW, int screenH, const CombatState& start, bool held,
                        bool* multi) {
  // Same applied-scale sweep and settling draw as above.
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(screenW, screenH);
  const int pct = menu_applied_scale(screenH);
  const UiRect lp = menu_panel_rect(ui_unscale_px(screenW, pct), ui_unscale_px(screenH, pct));
  const UiRect p = { ui_scale_px(lp.x, pct), ui_scale_px(lp.y, pct),
                     ui_scale_px(lp.w, pct), ui_scale_px(lp.h, pct) };
  int all = 0;
  if (multi) *multi = false;
  for (int y = p.y; y < p.y + p.h; y += SWEEP_STRIDE) {
    for (int x = p.x; x < p.x + p.w; x += SWEEP_STRIDE) {
      combat_restore(start);
      menu_set_tab(TAB_COMBAT);
      g_ui.mx = x; g_ui.my = y;
      g_ui.down = held; g_ui.pressed = !held;
      menu_draw(screenW, screenH);
      const int m = combat_changed(start);
      all |= m;
      if (multi && m && (m & (m - 1))) *multi = true;
    }
  }
  g_ui.down = false; g_ui.pressed = false;
  combat_restore(start);
  return all;
}
int combat_row_click_mask(int rowY, int screenW, int screenH, const CombatState& start) {
  const UiRect p = menu_panel_rect(screenW, screenH);
  int mask = 0;
  for (int x = p.x; x < p.x + p.w; x++) {
    combat_restore(start);
    menu_set_tab(TAB_COMBAT);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = false; g_ui.pressed = true;
    menu_draw(screenW, screenH);
    mask |= combat_changed(start);

    combat_restore(start);
    menu_set_tab(TAB_COMBAT);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
    menu_draw(screenW, screenH);
    mask |= combat_changed(start);
  }
  g_ui.down = false; g_ui.pressed = false;
  combat_restore(start);
  return mask;
}
int vis_row_click_mask(int rowY, int screenW, int screenH, const VisState& start) {
  const UiRect p = menu_panel_rect(screenW, screenH);
  int mask = 0;
  for (int x = p.x; x < p.x + p.w; x++) {
    vis_restore(start);
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = false; g_ui.pressed = true;
    menu_draw(screenW, screenH);
    mask |= vis_changed(start);

    vis_restore(start);
    menu_set_tab(TAB_VISUALS);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
    menu_draw(screenW, screenH);
    mask |= vis_changed(start);
  }
  g_ui.down = false; g_ui.pressed = false;
  vis_restore(start);
  return mask;
}
WeapState weap_snapshot() {
  WeapState w = { g_cfg.accpistol, g_cfg.accchaingun, g_cfg.accshotgun, g_cfg.accexpander,
                  g_cfg.mouseaimon, g_cfg.mouseaimsens, g_cfg.mouseaimvscale,
                  g_cfg.firerate };
  return w;
}
void weap_restore(const WeapState& w) {
  g_cfg.accpistol = w.pistol; g_cfg.accchaingun = w.chaingun;
  g_cfg.accshotgun = w.shotgun; g_cfg.accexpander = w.expander;
  g_cfg.mouseaimon = w.mouseaimon; g_cfg.mouseaimsens = w.mouseaimsens;
  g_cfg.mouseaimvscale = w.mouseaimvscale;
  g_cfg.firerate = w.firerate;
}
int weap_changed(const WeapState& before) {
  int m = 0;
  if (g_cfg.accpistol   != before.pistol)   m |= WF_PISTOL;
  if (g_cfg.accchaingun != before.chaingun) m |= WF_CHAINGUN;
  if (g_cfg.accshotgun  != before.shotgun)  m |= WF_SHOTGUN;
  if (g_cfg.accexpander != before.expander) m |= WF_EXPANDER;
  if (g_cfg.mouseaimon     != before.mouseaimon)     m |= WF_MOUSEAIMON;
  if (g_cfg.mouseaimsens   != before.mouseaimsens)   m |= WF_MOUSEAIMSENS;
  if (g_cfg.mouseaimvscale != before.mouseaimvscale) m |= WF_MOUSEAIMVSCALE;
  if (g_cfg.firerate       != before.firerate)       m |= WF_FIRERATE;
  return m;
}
int weap_sweep(int screenW, int screenH, const WeapState& start, bool held,
                      bool* multi) {
  // Same applied-scale sweep and settling draw as above.
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_draw(screenW, screenH);
  const int pct = menu_applied_scale(screenH);
  const UiRect lp = menu_panel_rect(ui_unscale_px(screenW, pct), ui_unscale_px(screenH, pct));
  const UiRect p = { ui_scale_px(lp.x, pct), ui_scale_px(lp.y, pct),
                     ui_scale_px(lp.w, pct), ui_scale_px(lp.h, pct) };
  int all = 0;
  if (multi) *multi = false;
  for (int y = p.y; y < p.y + p.h; y += SWEEP_STRIDE) {
    for (int x = p.x; x < p.x + p.w; x += SWEEP_STRIDE) {
      weap_restore(start);
      menu_set_tab(TAB_WEAPONS);
      g_ui.mx = x; g_ui.my = y;
      g_ui.down = held; g_ui.pressed = !held;
      menu_draw(screenW, screenH);
      const int m = weap_changed(start);
      all |= m;
      if (multi && m && (m & (m - 1))) *multi = true;
    }
  }
  g_ui.down = false; g_ui.pressed = false;
  weap_restore(start);
  return all;
}
int weap_row_click_mask(int rowY, int screenW, int screenH, const WeapState& start) {
  const UiRect p = menu_panel_rect(screenW, screenH);
  int mask = 0;
  for (int x = p.x; x < p.x + p.w; x++) {
    weap_restore(start);
    menu_set_tab(TAB_WEAPONS);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = false; g_ui.pressed = true;
    menu_draw(screenW, screenH);
    mask |= weap_changed(start);

    weap_restore(start);
    menu_set_tab(TAB_WEAPONS);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
    menu_draw(screenW, screenH);
    mask |= weap_changed(start);
  }
  g_ui.down = false; g_ui.pressed = false;
  weap_restore(start);
  return mask;
}
bool tab_text_fits(int tab, int screenW, int screenH, int inset, int* worstOverflowOut) {
  const UiRect p = menu_panel_rect(screenW, screenH);
  menu_set_tab(tab);
  canvas_record_begin();
  menu_draw(screenW, screenH);
  canvas_record_end();
  bool ok = true;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (!o || o->kind != CANVAS_OP_TEXT) continue;
    const int right = o->x + o->w;
    const int bound = p.x + p.w - inset;
    if (right > bound) {
      ok = false;
      if (worstOverflowOut && right - bound > *worstOverflowOut) *worstOverflowOut = right - bound;
    }
  }
  return ok;
}
bool tab_bottom_row_fits(int tab, const char* bottomLabel, int screenW, int screenH) {
  menu_set_tab(tab);
  canvas_record_begin();
  menu_draw(screenW, screenH);
  canvas_record_end();
  return !canvas_record_overflowed() && canvas_record_find_text(bottomLabel) != nullptr;
}
const CanvasOp* find_readout_at_y(int rowY) {
  const int TOLERANCE = 4;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (o && o->kind == CANVAS_OP_TEXT && o->font == FONT_MONO
        && o->y >= rowY - TOLERANCE && o->y <= rowY + TOLERANCE) return o;
  }
  return nullptr;
}
const CanvasOp* find_track_at_label(const char* label) {
  const CanvasOp* l = canvas_record_find_text(label);
  if (!l) return nullptr;
  for (int i = 0; i < canvas_record_count(); i++) {
    const CanvasOp* o = canvas_record_op(i);
    if (o && o->kind == CANVAS_OP_BEVEL && o->colour == C_WELL
        && o->y >= l->y && o->y <= l->y + 20) return o;
  }
  return nullptr;
}
bool slider_row_reaches_ends(int tab, const char* label, int screenW, int screenH,
                                    int lo, int hi, int* fieldPtr, int startValue) {
  const UiRect p = menu_panel_rect(screenW, screenH);
  menu_set_tab(tab);
  canvas_record_begin();
  menu_draw(screenW, screenH);
  canvas_record_end();
  const CanvasOp* labelOp = canvas_record_find_text(label);
  if (!labelOp) return false;
  const int rowY = labelOp->y;
  bool sawMin = false, sawMax = false, allLegal = true;
  for (int x = p.x; x < p.x + p.w; x++) {
    *fieldPtr = startValue;
    menu_set_tab(tab);
    g_ui.mx = x; g_ui.my = rowY; g_ui.down = true; g_ui.pressed = false;
    menu_draw(screenW, screenH);
    const int v = *fieldPtr;
    if (v < lo || v > hi) allLegal = false;
    if (v == lo) sawMin = true;
    if (v == hi) sawMax = true;
  }
  g_ui.down = false; g_ui.pressed = false;
  *fieldPtr = startValue;
  return sawMin && sawMax && allLegal;
}
const char* tooltip_for_row(const char* label, int screenW, int screenH) {
  const int  savedAnim = g_cfg.menuanim;
  const int  savedScale = g_cfg.uiscale;
  const int  savedMx = g_ui.mx, savedMy = g_ui.my;
  const bool savedDown = g_ui.down, savedPressed = g_ui.pressed;
  g_cfg.menuanim = ANIM_OFF;
  // Forced, not assumed: feeds a recorded x (device space) back into
  // g_ui.mx (logical space), only sound while the applied scale is 100.
  g_cfg.uiscale = UI_SCALE_DEFAULT;
  // No button held: a press puts the tooltip away (tooltip.h), so a held one would give a null indistinguishable from a
  // row with no hint.
  g_ui.down = false; g_ui.pressed = false;
  g_ui.mx = -1; g_ui.my = -1;
  const unsigned t0 = 100000u;
  menu_set_now_ms(t0);
  canvas_record_begin();
  menu_draw(screenW, screenH);
  canvas_record_end();
  const CanvasOp* row = canvas_record_find_text(label);
  const char* shown = nullptr;
  if (row) {
    // Label op plus a small offset is a point inside the row whatever its indent, derived from the recording, not
    // menu.cpp's row pitch.
    const int rx = row->x, ry = row->y;
    g_ui.mx = rx + 4; g_ui.my = ry + 4;
    menu_set_now_ms(t0 + 16);                          // the arrival frame
    menu_draw(screenW, screenH);
    menu_set_now_ms(t0 + 16 + TOOLTIP_DELAY_MS + 1);   // past the rest delay
    menu_draw(screenW, screenH);
    shown = menu_tooltip_text();
  }
  g_ui.mx = savedMx; g_ui.my = savedMy;
  g_ui.down = savedDown; g_ui.pressed = savedPressed;
  menu_set_now_ms(0);
  g_cfg.menuanim = savedAnim;
  g_cfg.uiscale = savedScale;
  return shown;
}
GProj gcam(const CamState& c, const short* st) {
  return proj_gproj_recomputed(c, st);
}
PolyPt polymost_point(const CamState& c, const GProj& g,
                             double wx, double wy, double wz) {
  PolyPt p = { 0.0, 0.0, false };
  const double dx = wx - c.posx, dy = wy - c.posy, dz = wz - c.posz;
  const double glat   = dy * g.cosang  - dx * g.sinang;      // 0x00482d02
  const double gdepth = dx * g.cosang2 + dy * g.sinang2;     // 0x00482d1c
  if (!(gdepth >= 4.0)) return p;                            // NEAR_CLIP
  const double ox  = glat * g.halfx / gdepth;                // 0x00475471
  const double oy  = dz * g.yxscale / gdepth;                // 0x00475483
  const double oy2 = oy * g.chang - g.halfx * g.shang;       // 0x004754a7
  const double oz2 = oy * g.shang + g.halfx * g.chang;       // 0x004754c4
  if (!(oz2 >= 1.0)) return p;                               // 0x00475527
  const double f = g.halfx / oz2;                            // 0x00475544
  p.px = (double)c.windowx1 + g.halfx + ox * f;
  p.py = (double)c.windowy1 + g.horiz + oy2 * f;
  p.ok = true;
  return p;
}
bool esp_name_is(int picnum, const char* expect) {
  const char* n = esp_enemy_name(picnum);
  return n != nullptr && strcmp(n, expect) == 0;
}
bool esp_item_name_is(int picnum, const char* expect) {
  const char* n = esp_item_name(picnum);
  return n != nullptr && strcmp(n, expect) == 0;
}
bool streq(const char* a, const char* b) {
  return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}
bool esp_names_match(int a, int b) {
  const char* x = esp_enemy_name(a);
  const char* y = esp_enemy_name(b);
  return x != nullptr && y != nullptr && strcmp(x, y) == 0;
}
const unsigned ACC_TEST_RW_PROT   = 0x40;

const int ACC_TEST_MAX_PAGES = 64;

static size_t   s_accPageKey[ACC_TEST_MAX_PAGES];

static unsigned s_accPageProt[ACC_TEST_MAX_PAGES];

static int      s_accPageCount    = 0;

static int      s_accPageOverflow = 0;

const int ACC_TEST_MAX_EV = 512;

static int      s_accEvKind[ACC_TEST_MAX_EV];

static unsigned s_accEvProt[ACC_TEST_MAX_EV];

static int      s_accEvCount    = 0;

static int      s_accEvOverflow = 0;


static int      s_accProtCalls       = 0;

static unsigned s_accProtLastSize    = 0;

static unsigned s_accProtLastProt    = 0;

static int      s_accWriteCalls      = 0;

static int      s_accWriteViolations = 0;

// Never cleared by acc_test_protect_reset: one check at the end of the block
// then covers every write the bracket made anywhere in it.
static int      s_accWriteViolTotal  = 0;
void acc_test_protect_reset() {
  s_accProtCalls = 0; s_accProtLastSize = 0; s_accProtLastProt = 0;
  s_accWriteCalls = 0; s_accWriteViolations = 0;
  s_accPageCount = 0; s_accPageOverflow = 0;
  s_accEvCount = 0; s_accEvOverflow = 0;
}
int      acc_test_protect_calls()     { return s_accProtCalls; }
unsigned acc_test_protect_last_size() { return s_accProtLastSize; }
unsigned acc_test_protect_last_prot() { return s_accProtLastProt; }
int      acc_test_write_calls()       { return s_accWriteCalls; }
int      acc_test_write_violations()  { return s_accWriteViolations; }
int      acc_test_write_viol_total()  { return s_accWriteViolTotal; }
int      acc_test_page_overflow()     { return s_accPageOverflow; }
int      acc_test_ev_count()          { return s_accEvCount; }
int      acc_test_ev_overflow()       { return s_accEvOverflow; }
int      acc_test_ev_kind(int i) { return (i >= 0 && i < s_accEvCount) ? s_accEvKind[i] : 0; }
unsigned acc_test_ev_prot(int i) { return (i >= 0 && i < s_accEvCount) ? s_accEvProt[i] : 0u; }
void acc_test_log(int kind, unsigned prot) {
  if (s_accEvCount >= ACC_TEST_MAX_EV) { s_accEvOverflow = 1; return; }
  s_accEvKind[s_accEvCount] = kind;
  s_accEvProt[s_accEvCount] = prot;
  s_accEvCount++;
}
size_t acc_test_page_key(const void* p) {
  return (size_t)p / (size_t)ACC_TEST_PAGE_SIZE;
}
int acc_test_page_slot(size_t key) {
  for (int i = 0; i < s_accPageCount; i++) if (s_accPageKey[i] == key) return i;
  return -1;
}
unsigned acc_test_page_prot(const void* p) {
  const int slot = acc_test_page_slot(acc_test_page_key(p));
  return slot < 0 ? ACC_TEST_OLD_PROT : s_accPageProt[slot];
}
void acc_test_page_set(size_t key, unsigned prot) {
  int slot = acc_test_page_slot(key);
  if (slot < 0) {
    if (s_accPageCount >= ACC_TEST_MAX_PAGES) { s_accPageOverflow = 1; return; }
    slot = s_accPageCount++;
    s_accPageKey[slot] = key;
  }
  s_accPageProt[slot] = prot;
}
int acc_test_pages_writable() {
  int n = 0;
  for (int i = 0; i < s_accPageCount; i++) if (s_accPageProt[i] == ACC_TEST_RW_PROT) n++;
  return n;
}
void acc_test_protect_apply(void* addr, unsigned size, unsigned newProt,
                                   unsigned* oldProtOut) {
  s_accProtLastSize = size;
  s_accProtLastProt = newProt;
  if (oldProtOut) *oldProtOut = acc_test_page_prot(addr);   // first page's, as VirtualProtect reports
  const size_t first = acc_test_page_key(addr);
  const size_t last  = acc_test_page_key((const unsigned char*)addr + (size ? size - 1u : 0u));
  for (size_t k = first; k <= last; k++) acc_test_page_set(k, newProt);
  acc_test_log(ACC_EV_PROTECT, newProt);
}
int acc_test_protect_ok(void* addr, unsigned size, unsigned newProt, unsigned* oldProtOut) {
  s_accProtCalls++;
  acc_test_protect_apply(addr, size, newProt, oldProtOut);
  return 1;
}
int acc_test_protect_fail_second(void* addr, unsigned size, unsigned newProt,
                                        unsigned* oldProtOut) {
  s_accProtCalls++;
  if (s_accProtCalls == 2) { (void)addr; (void)size; (void)newProt; (void)oldProtOut; return 0; }
  acc_test_protect_apply(addr, size, newProt, oldProtOut);
  return 1;
}
const void* q_stub_last_addr = 0;
unsigned q_stub_last_bytes = 0;
int q_stub_refuse(const void* addr, unsigned bytes) {
  q_stub_last_addr = addr; q_stub_last_bytes = bytes; return 0;
}
int q_stub_accept(const void* addr, unsigned bytes) {
  q_stub_last_addr = addr; q_stub_last_bytes = bytes; return 1;
}
void acc_test_write_watch(void* addr, unsigned size) {
  s_accWriteCalls++;
  const unsigned char* q = (const unsigned char*)addr;
  for (unsigned i = 0; i < size; i++) {
    if (acc_test_page_prot(q + i) != ACC_TEST_RW_PROT) {
      s_accWriteViolations++; s_accWriteViolTotal++; break;
    }
  }
  acc_test_log(ACC_EV_WRITE, 0);
}
long __cdecl fake_hitscan_writes_nothing(long, long, long, short,
                                                long, long, long,
                                                short*, short*, short*,
                                                long*, long*, long*,
                                                unsigned long) {
  return 0;
}


// Stand-in for polymost_scansector's body (nocull.cpp cannot link into test_host): it marks its own bit like the real
// seed marking and records call order, so the suite can check "each once, in index order" over a synthetic gotsector
// bitset.
static unsigned char s_nocullTestGot[512];

static const int NOCULL_TEST_MAX_CALLS = 4200;

static int        s_nocullTestOrder[NOCULL_TEST_MAX_CALLS];

static int         s_nocullTestCalls = 0;
void nocull_test_reset() {
  memset(s_nocullTestGot, 0, sizeof(s_nocullTestGot));
  s_nocullTestCalls = 0;
}
void nocull_test_original(int s) {
  if (s >= 0 && s < 4096) s_nocullTestGot[s >> 3] |= (unsigned char)(1 << (s & 7));
  if (s_nocullTestCalls < NOCULL_TEST_MAX_CALLS) s_nocullTestOrder[s_nocullTestCalls] = s;
  s_nocullTestCalls++;
}
unsigned char* nocull_test_got()      { return s_nocullTestGot; }
int             nocull_test_call_count() { return s_nocullTestCalls; }
int             nocull_test_call_at(int i) {
  return (i >= 0 && i < s_nocullTestCalls) ? s_nocullTestOrder[i] : -1;
}
const char* tmp;
FILE* f;
UiRect r;
UiRect cur;
UiRect a;
UiRect b;
UiRect p;
UiRect q;
UiRect s;

const char* tb_path(const char* s) {
  static char bufs[4][260];
  static int next = 0;
  char* r = bufs[next++ & 3];
  size_t i = 0;
  for (; s[i] && i < 259; i++) r[i] = s[i] == '|' ? '\\' : s[i] == '~' ? '/' : s[i];
  r[i] = 0;
  return r;
}
