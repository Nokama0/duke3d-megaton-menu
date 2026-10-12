#include "esp.h"
#include "esp_names.h"
#include "esp_layout.h"
#include "esp_world.h"
#include "trigger.h"   // trigger_map_signature, TRIG_SCAN_PERIOD_MS
#include "secretpath.h"  // secretpath_draw: the secret-path polyline, drawn in this file's GL bracket
#include "wallclock.h"
#include "esp_switch.h"
#include "engine.h"
#include "target.h"    // target_hp_for_display: clamps the enemy health number shown
#include "aim.h"
#include "actorstate.h"  // actor_state_cached/shown/label: the one alive/dying rule
#include "freecam.h"   // camera_active/camera_snapshot, freecam_marker_on's gate
#include "dollhouse.h" // dollhouse_frame_stats, the debug readout
#include "project.h"
#include "canvas.h"
#include "common.h"
#include "hooks.h"
#include "config.h"
#include "font.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

// STAT_ACTOR (1) and STAT_ZOMBIEACTOR (2) both hold pickups (spawn(), VA 0x004c1e00, puts a map-placed one on 2;
// movefta(), VA 0x00499530, promotes 2->1 like an enemy), so both walks classify inline rather than walking a list
// twice (the cycle counter is single-slot).


bool esp_any_on() {
  // Only the parent switches: espboxes/esphealth/espnames/espdistance default on and draw nothing alone, so ORing them
  // in would open the GL bracket every frame. The freecam "You" marker gates on the live camera; camera_notice() is
  // non-null only for the two seconds a teleport reports.
  return esp_any_category_on(g_cfg.espenemies, g_cfg.espitems, g_cfg.espsecrets,
                             aim_marker_on(g_cfg.aimon, g_cfg.silenton, g_cfg.aimdebug),
                             freecam_marker_on(camera_active(), g_cfg.freecamshowplayer),
                             camera_notice() != 0,
                             g_cfg.espexplodables, g_cfg.espswitches, g_cfg.espbreakables);
}

// The one draw-distance cull, on p.dist2d, never p.depth: depth is FOV-scaled and under-reads off-axis, so it would
// cull at a quarter range.
static bool esp_dist_ok(const ScreenPt& p) {
  return esp_marker_in_range(p, g_cfg.espdrawdist, g_cfg.espnolimit);
}

static bool diag_throttle_due() {
  static DWORD s_lastMs = 0;
  static bool  s_first = true;
  const DWORD now = GetTickCount();
  if (!s_first && (now - s_lastMs) < 3000) return false;
  s_first = false;
  s_lastMs = now;
  return true;
}

struct SpriteDiagLine {
  int index, wx, wy, wz;
  int dx, dy, sinang, cosang;
  long long fwd, lat;
  double glat, gdepth;
  int dh;
  double oz2;
  int depth, x, y;
  bool visible;
  const char* reason;
  int pic, pal, hp, maxhp;       // what the label is built from
  int base, ruled;               // actorscrptr strength, and the (picnum,pal) rule on it
  int tsx, tsy, xrep, yrep;      // what the box is sized from
  int bx, by, bw, bh, corners;   // and the box itself; all zero when not drawn
};

// The eight rectangles of one box's corner brackets. Brackets, not a closed rectangle, so outlines don't merge into a
// grid in a crowd.
static void esp_draw_brackets(const ScreenBox& box, unsigned colour) {
  const int cx = esp_bracket_arm(box.w);
  const int cy = esp_bracket_arm(box.h);
  const int t  = esp_bracket_thickness(cx, cy);
  if (t <= 0) return;
  const int x0 = box.x, y0 = box.y;
  const int x1 = box.x + box.w, y1 = box.y + box.h;
  canvas_rect(x0,      y0,      cx, t,  colour);
  canvas_rect(x0,      y0,      t,  cy, colour);
  canvas_rect(x1 - cx, y0,      cx, t,  colour);
  canvas_rect(x1 - t,  y0,      t,  cy, colour);
  canvas_rect(x0,      y1 - t,  cx, t,  colour);
  canvas_rect(x0,      y1 - cy, t,  cy, colour);
  canvas_rect(x1 - cx, y1 - t,  cx, t,  colour);
  canvas_rect(x1 - t,  y1 - cy, t,  cy, colour);
}

static void esp_collect(EspMarker* markers, int* markerCount, int* markerSeq,
                        int* markerOverflow, int kind, int index,
                        const ScreenPt& p, const ScreenBox& box) {
  if (*markerCount >= ESP_MARKER_MAX) { (*markerOverflow)++; return; }
  EspMarker& m = markers[(*markerCount)++];
  m.kind = kind; m.index = index; m.dist2d = p.dist2d; m.seq = (*markerSeq)++;
  m.p = p; m.box = box;
}

// One non-enemy sprite from the walk, collected if a pickup. Same box path as the enemy marker, because the engine
// draws both alike (polymost_drawsprite's face branch never checks picnum, VA 0x00482d70-0x00482dd8), so the box is the
// real footprint.
static void esp_collect_item(int i, const CamState& cam, const GProj& g,
                          const short* sintable, int screenW, int screenH,
                          int* classified, int* drawn, int* refused, int* farculled,
                          EspMarker* markers, int* markerCount, int* markerSeq,
                          int* markerOverflow) {
  const int pic = engine_sprite_field_s(i, SPR_PICNUM);
  // Name is a function of (picnum, PAL): an ACCESSCARD's pal says which lock it opens and survives spawn()
  // (esp_names.h).
  const char* nm = esp_item_name_pal(pic, engine_sprite_field_u8(i, SPR_PAL));
  if (!nm) return;   // not a pickup: scenery, a projectile, a corpse, an effect

  const int cstat = engine_sprite_field_s(i, SPR_CSTAT);
  const int xrep  = engine_sprite_field_u8(i, SPR_XREPEAT);
  if (!esp_item_present(cstat, xrep)) return;
  (*classified)++;

  const int wx = engine_sprite_field_l(i, SPR_X);
  const int wy = engine_sprite_field_l(i, SPR_Y);
  const int wz = engine_sprite_field_l(i, SPR_Z);
  const ScreenPt p = project_world(cam, g, sintable, wx, wy, wz);
  if (!p.visible) return;
  if (!esp_dist_ok(p)) { (*farculled)++; return; }
  if (!proj_on_screen(p.x, p.y, screenW, screenH)) return;
  (*drawn)++;

  // cstat read live, not assumed 0: ATOMICHEALTH is the one pickup spawn() centres (0x80); a floor anchor would
  // misplace its box.
  const SpriteExtents ext = proj_sprite_extents(wz, cstat,
                                                engine_tilesizx(pic), engine_tilesizy(pic),
                                                xrep, engine_sprite_field_u8(i, SPR_YREPEAT));
  const ScreenBox projected = project_sprite_box(cam, g, sintable, wx, wy, ext);
  if (!proj_box_corners_ok(projected.corners)) (*refused)++;
  const ScreenBox box = esp_box_or_anchor(projected, p.x, p.y);

  esp_collect(markers, markerCount, markerSeq, markerOverflow,
             ESP_KIND_ITEM, i, p, box);
}

// Secret-area markers: one EspMarker per undiscovered secret sector's centre. A sector pass, so the sprite cycle
// counter is untouched. Found secrets vanish on their own (checksectors zeroes the lotag, VA 0x005141e4), so keep no
// found-set (see SECRET_SECTOR_LOTAG, engine.h).
static void esp_collect_secrets(const CamState& cam, const GProj& g,
                             const short* sintable, int screenW, int screenH,
                             int* scanned, int* secret, int* nocentre,
                             int* projected, int* drawn, int* farculled,
                             EspMarker* markers, int* markerCount, int* markerSeq,
                             int* markerOverflow) {
  const int numsectors = engine_numsectors();
  for (int s = 0; s < numsectors; s++) {
    (*scanned)++;
    if (!engine_sector_is_secret(s)) continue;
    (*secret)++;

    int wx = 0, wy = 0, wz = 0;
    if (!engine_sector_centre(s, &wx, &wy, &wz)) { (*nocentre)++; continue; }

    const ScreenPt p = project_world(cam, g, sintable, wx, wy, wz);
    if (!p.visible) continue;
    (*projected)++;
    if (!esp_dist_ok(p)) { (*farculled)++; continue; }
    if (!proj_on_screen(p.x, p.y, screenW, screenH)) continue;
    (*drawn)++;

    esp_collect(markers, markerCount, markerSeq, markerOverflow,
               ESP_KIND_SECRET, s, p, proj_anchor_box(p.x, p.y));
  }
}

// esp_world_tic (tic-clock producer, render-clock consumer over a seqlock) reads map memory only: each sector's wall
// range and each wall's picnum against esp_breakable_name. The marker sits at the wall's 2D midpoint at the z halfway
// between floor and ceiling AT it.
static EspWallSnapshot s_wallPub = { 0u, 0, 0, {} };

static unsigned wall_seq_read() { return *(volatile unsigned*)&s_wallPub.seq; }
static void wall_seq_write(unsigned v) { *(volatile unsigned*)&s_wallPub.seq = v; }

void esp_world_tic() {
  if (!esp_world_should_scan(g_cfg.espbreakables, g_cfg.espswitches)) {
    unsigned seq = wall_seq_read();
    snap_begin(&seq);
    wall_seq_write(seq);
    s_wallPub.count = 0;
    s_wallPub.overflow = 0;
    snap_end(&seq);
    wall_seq_write(seq);
    return;
  }

  EspWallSnapshot local;
  local.seq = 0;
  local.count = 0;
  local.overflow = 0;

  const int numsectors = engine_numsectors();
  for (int s = 0; s < numsectors; s++) {
    int wallptr = 0, wallnum = 0;
    if (!engine_sector_wall_range(s, &wallptr, &wallnum)) continue;

    for (int w = wallptr; w < wallptr + wallnum; w++) {
      const int picnum = engine_wall_field_s(w, rva::WAL_PICNUM);
      const bool breakable = esp_breakable_name(picnum) != 0;
      // A second picnum test (the sets are disjoint: checkhitwall vs checkhitswitch). esp_wall_switch_capturable also
      // needs lotag != 0: otherwise every ordinary door fills ESP_WALL_MAX with marks the render clock drops, starving
      // real switches.
      int lotag = 0;
      bool isSwitch = false;
      if (!breakable) {
        lotag = engine_wall_field_s(w, rva::WAL_LOTAG);
        isSwitch = esp_wall_switch_capturable(picnum, lotag);
      }
      if (!breakable && !isSwitch) continue;

      const int point2 = engine_wall_field_s(w, rva::WAL_POINT2);
      if (point2 < 0 || point2 >= engine_numwalls()) continue;

      const int x0 = engine_wall_field_l(w, WAL_X);
      const int y0 = engine_wall_field_l(w, WAL_Y);
      const int x1 = engine_wall_field_l(point2, WAL_X);
      const int y1 = engine_wall_field_l(point2, WAL_Y);
      const int mx = esp_wall_mid(x0, x1);
      const int my = esp_wall_mid(y0, y1);

      if (local.count >= ESP_WALL_MAX) { local.overflow++; continue; }

      int florz = 0, ceilz = 0;
      if (!engine_florz(s, mx, my, &florz)) continue;
      if (!engine_ceilz(s, mx, my, &ceilz)) continue;

      EspWallMark& mk = local.m[local.count++];
      mk.kind = breakable ? ESP_WKIND_BREAKABLE : ESP_WKIND_SWITCH;
      mk.wallnum = w;
      mk.sectnum = s;
      mk.x = mx;
      mk.y = my;
      mk.z = esp_wall_mid_z(florz, ceilz);
      if (isSwitch) {
        mk.picnum = picnum;
        mk.pal    = engine_wall_field_b(w, rva::WAL_PAL);
        mk.hitag  = engine_wall_field_s(w, rva::WAL_HITAG);
        mk.lotag  = lotag;   // already read above; not re-read here
      } else {
        mk.picnum = 0; mk.pal = 0; mk.hitag = 0; mk.lotag = 0;
      }
    }
  }

  unsigned seq = wall_seq_read();
  snap_begin(&seq);
  wall_seq_write(seq);          // odd: a reader starting now will refuse its copy

  s_wallPub.count = local.count;
  s_wallPub.overflow = local.overflow;
  for (int i = 0; i < local.count; i++) s_wallPub.m[i] = local.m[i];

  snap_end(&seq);
  wall_seq_write(seq);          // even: stable again
}

bool esp_wall_snapshot(EspWallSnapshot* out) {
  if (!out) return false;
  const unsigned before = wall_seq_read();
  EspWallSnapshot tmp = s_wallPub;
  const unsigned after = wall_seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

// Secret-exit scan: tic clock, once per level. Sets whether the level has a secret exit and, for a sector with no
// NUKEBUTTON (a "touch" exit), the world point its marker sits at. No seqlock: tic and render share one thread.
static bool s_levelHasSecretExit = false;
static unsigned s_secretExitSig = 0;        // trigger_map_signature of the scanned map
static unsigned s_secretExitScanMs = 0;     // wallclock of the last scan
static bool     s_secretExitScanned = false;
static unsigned s_exitLogSig = 0;   // the map signature the exit: lines were last written for

void esp_secret_exit_tic() {
  if (!g_cfg.espsecrets) {
    s_levelHasSecretExit = false;
    s_secretExitScanned = false;   // rescan the moment it's re-enabled
    return;
  }
  const unsigned sig = trigger_map_signature(engine_numsectors(), engine_numwalls(), 0);
  const unsigned now = wallclock_ms();
  if (s_secretExitScanned && sig == s_secretExitSig &&
      (now - s_secretExitScanMs) < TRIG_SCAN_PERIOD_MS) return;
  s_secretExitScanned = true;
  s_secretExitSig = sig;
  s_secretExitScanMs = now;

  // One drain of statnum 0: a secret exit is a NUKEBUTTON with non-zero pal (exit_kind_for). The whole sprite array,
  // not a status list: which list holds the button isn't settled (E1L4 showed neither 0 nor 6) and 16k reads a second
  // are free. A free slot has statnum MAXSTATUS (1024).
  s_levelHasSecretExit = false;
  int logged = 0;
  for (int i = 0; i < (int)rva::MAX_SPRITES; i++) {
    const int stat = engine_sprite_field_s(i, SPR_STATNUM);
    if (stat < 0 || stat >= 1024) continue;
    if (engine_sprite_field_s(i, SPR_PICNUM) != ESP_PIC_NUKEBUTTON) continue;
    const int pal = engine_sprite_field_u8(i, SPR_PAL);
    const int lotag = engine_sprite_field_s(i, SPR_LOTAG) & 0xFFFF;
    const int kind = exit_kind_for(ESP_PIC_NUKEBUTTON, pal, lotag);
    if (kind == EXIT_SECRET) s_levelHasSecretExit = true;
    if (g_cfg.debuglog != 0 && sig != s_exitLogSig && logged < 8) {
      logged++;
      LOGI("exit: i=%d stat=%d pal=%d lotag=%d xrep=%d kind=%d", i, stat, pal, lotag,
           engine_sprite_field_u8(i, SPR_XREPEAT), kind);
    }
  }
  if (logged) s_exitLogSig = sig;
}

bool esp_level_has_secret_exit() { return s_levelHasSecretExit; }

static char s_readout[1200];
static void readout_keep(const char* line) {
  strcat_s(s_readout, line);
  strcat_s(s_readout, "\r\n");
}
const char* esp_debug_readout_text() { return s_readout; }


// One draw helper per EspMarker::kind, called far-first from the sorted loop in esp_draw. Each re-derives display data
// from EspMarker::index (validated by the collecting pass); re-reading engine fields is safe, nothing touches sprite
// memory in between.

static void esp_draw_item_marker(const EspMarker& m) {
  const int pic = engine_sprite_field_s(m.index, SPR_PICNUM);
  const int pal = engine_sprite_field_u8(m.index, SPR_PAL);
  const char* nm = esp_item_name_pal(pic, pal);
  if (!nm) return;   // cannot happen: only a named pickup is ever collected

  if (esp_is_keycard(pic)) {
    esp_draw_card_mark(m.p, keycard_colour(pal), nm, nullptr);
    return;
  }

  esp_draw_brackets(m.box, canvas_esp_colour(false));

  canvas_text(esp_label_x(m.box.x, m.box.w, canvas_text_width(nm, FONT_SMALL)),
              esp_name_label_y(m.box.y), nm, C_BONE, FONT_SMALL);
}

static void esp_draw_secret_marker(const EspMarker& m) {
  const unsigned colour = canvas_esp_secret_colour();
  canvas_rect(esp_secret_hbar_x(m.p.x), esp_secret_hbar_y(m.p.y),
              esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(m.p.x), esp_secret_vbar_y(m.p.y),
              ESP_SECRET_T, esp_secret_bar_span(), colour);

  canvas_text(esp_label_x(m.p.x, 0, canvas_text_width(ESP_SECRET_LABEL, FONT_SMALL)),
              esp_secret_name_label_y(m.p.y), ESP_SECRET_LABEL, colour, FONT_SMALL);

  char dist[16];
  _snprintf_s(dist, sizeof(dist), _TRUNCATE, "%d m", m.p.dist2d / BUILD_UNITS_PER_METRE);
  canvas_text(esp_label_x(m.p.x, 0, canvas_text_width(dist, FONT_MONO)),
              esp_secret_dist_label_y(m.p.y), dist, C_BONEDIM, FONT_MONO);
}


// Explodables: shared point marker, name above. One marker, two sources: a crack sprite's index is < rva::MAX_SPRITES,
// a wall mark's is MAX_SPRITES + wallnum. A crack's name comes from its live picnum; a wall's is one fixed string
// ("Breakable").
static void esp_draw_explodable_marker(const EspMarker& m) {
  const unsigned colour = canvas_esp_explodable_colour();
  const char* nm;
  if (m.index >= rva::MAX_SPRITES) {
    nm = "Breakable";
  } else {
    const int pic = engine_sprite_field_s(m.index, SPR_PICNUM);
    nm = esp_explodable_name(pic);
    if (!nm) return;   // cannot happen: only a live crack sprite is ever collected
  }

  esp_draw_cross_mark(m.p, colour, nm, nullptr);
}

// Seen-set keyed by the index space esp_draw_switch_marker decodes, not the 3s throttle (which would drop most switches
// before the log named them): one line per switch per level, cleared when engine_numwalls() changes.
const int ESP_SWITCH_LOG_SEEN_MAX = 512;
static int s_switchLogSeen[ESP_SWITCH_LOG_SEEN_MAX];
static int s_switchLogSeenCount = 0;
static int s_switchLogLastNumwalls = -1;

static void esp_switch_log_once(int idx, int wallnum, int picnum, int pal,
                                int lotag, int hitag, int access,
                                SwitchFamily fam, const char* line) {
  const int nw = engine_numwalls();
  if (nw != s_switchLogLastNumwalls) {
    s_switchLogLastNumwalls = nw;
    s_switchLogSeenCount = 0;
  }
  for (int i = 0; i < s_switchLogSeenCount; i++)
    if (s_switchLogSeen[i] == idx) return;   // already logged this level
  if (s_switchLogSeenCount < ESP_SWITCH_LOG_SEEN_MAX)
    s_switchLogSeen[s_switchLogSeenCount++] = idx;
  LOGI("switch: idx=%d wall=%d pic=%d pal=%d lotag=%d hitag=%d access=%d fam=%d line=%s",
       idx, wallnum, picnum, pal, lotag, hitag, access, (int)fam, line ? line : "(none)");
}

// Switches and locks: shared point marker in canvas_esp_switch_colour(), family label above, and when switch_classify
// says so a second line below (keycard colour or combination hint). Two sources like the explodable marker, re-read
// live; `access` is sampled once per frame.
static void esp_draw_switch_marker(const EspMarker& m, int access) {
  int picnum, pal, hitag, lotag;
  int wallnum = -1;   // -1 for a sprite switch; the wall index for a wall one
  if (m.index >= rva::MAX_SPRITES) {
    wallnum = m.index - rva::MAX_SPRITES;
    picnum = engine_wall_field_s(wallnum, rva::WAL_PICNUM);
    pal    = engine_wall_field_b(wallnum, rva::WAL_PAL);
    hitag  = engine_wall_field_s(wallnum, rva::WAL_HITAG);
    lotag  = engine_wall_field_s(wallnum, rva::WAL_LOTAG);
  } else {
    picnum = engine_sprite_field_s(m.index, SPR_PICNUM);
    pal    = engine_sprite_field_u8(m.index, SPR_PAL);
    hitag  = engine_sprite_field_s(m.index, SPR_HITAG);
    lotag  = engine_sprite_field_s(m.index, SPR_LOTAG);
  }
  const SwitchInfo si = switch_classify(picnum, pal, hitag, lotag, access);
  if (si.family == SW_NONE) return;   // cannot happen: only a live switch is ever collected

  if (g_cfg.debuglog != 0)
    esp_switch_log_once(m.index, wallnum, picnum, pal, lotag, hitag, access,
                        si.family, si.line);

  // SW_ACCESS: a card marker in the lock's colour, as the keycard pickup that opens it. Held: the line is dropped
  // (switch_classify's rule).
  if (si.family == SW_ACCESS) {
    esp_draw_card_mark(m.p, keycard_colour(pal), switch_label(si.family), si.line);
    return;
  }

  // SW_EXIT's secret half takes the secrets colour so it joins the "Secret" cross; its label is si.line.
  const unsigned colour = (si.family == SW_EXIT && si.exitKind == EXIT_SECRET)
                          ? canvas_esp_secret_colour() : canvas_esp_switch_colour();
  const char* label = (si.family == SW_EXIT) ? si.line : switch_label(si.family);
  const char* line2 = (si.family == SW_EXIT) ? nullptr : si.line;
  esp_draw_cross_mark(m.p, colour, label, line2);
}

// Enemies (g_cfg.espenemies): corner brackets, health bar, name with current/max above, distance below. The name stays
// FONT_SMALL, cur/max moves to FONT_MONO so digits align down a column; esp_label_runs centres the pair as one run.
static void esp_draw_enemy_marker(const EspMarker& m, int troopBase) {
  const int i = m.index;
  const ScreenBox& box = m.box;

  if (g_cfg.espboxes) esp_draw_brackets(box, canvas_esp_colour(true));

  const int pic   = engine_sprite_field_s(i, SPR_PICNUM);
  const int pal   = engine_sprite_field_u8(i, SPR_PAL);
  const int extra = engine_sprite_field_s(i, SPR_EXTRA);
  const int base  = engine_actor_strength(pic);
  int t5 = 0;
  const bool hasRun = engine_actor_temp_data(i, 5, &t5) && t5 != 0;
  const int hp    = esp_display_hp(target_hp_for_display(extra),
                                   esp_rule_max_health(pic, pal, base, troopBase), hasRun);
  const int maxhp = esp_max_health(pic, pal, hp, base, troopBase);

  // State label: null for AS_ALIVE (draw number and bar), else "Choking", "Playing dead" or "Cloaked". Computed once
  // per sprite per tic and shared with the aim walk via the per-tic cache (actorstate.h), since this draw runs several
  // times per tic.
  const char* stateLabel = actor_state_label(actor_state_cached(i));

  // Bar sits left of the box. maxhp == 0 means unknown, so no bar (a bar against a guess is worse than none); a
  // labelled state gets none.
  if (g_cfg.esphealth && maxhp > 0 && stateLabel == nullptr) {
    const int barX = esp_bar_x(box.x);
    const int fill = canvas_bar_fill(box.h, hp, maxhp);
    canvas_rect(barX, box.y, ESP_BAR_W, box.h, C_WELL);
    if (fill > 0)
      canvas_rect(barX, esp_bar_fill_y(box.y, box.h, fill), ESP_BAR_W, fill,
                  canvas_health_colour(hp, maxhp));
  }

  if (g_cfg.espnames) {
    // Unknown picnum shows no name run; numbers alone are centred. A wrong name is worse than none.
    const char* nm = esp_enemy_name_for(pic, engine_addon_index());
    char nameBuf[64];
    char numBuf[32];
    int nameW = 0;
    if (nm) {
      _snprintf_s(nameBuf, sizeof(nameBuf), _TRUNCATE, "%s", nm);
      nameW = canvas_text_width(nameBuf, FONT_SMALL);
    }
    // No maximum means no fraction: "640" reads as a fact, "640/4500" would lie about how much is left.
    if (stateLabel)
      _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%s", stateLabel);
    else if (maxhp > 0)
      _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%d/%d", hp, maxhp);
    else
      _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%d", hp);
    const int numW = canvas_text_width(numBuf, FONT_MONO);

    const EspLabelRuns runs = esp_label_runs(box.x, box.w, nameW, numW);
    const int nameY = esp_name_label_y(box.y);
    if (nm) canvas_text(runs.nameX, nameY, nameBuf, C_BONE, FONT_SMALL);
    canvas_text(runs.numX,
               esp_label_mono_y(nameY, font_height[FONT_SMALL], font_height[FONT_MONO]),
               numBuf, C_BONE, FONT_MONO);
  }

  if (g_cfg.espdistance) {
    char dist[16];
    _snprintf_s(dist, sizeof(dist), _TRUNCATE, "%d m", m.p.dist2d / BUILD_UNITS_PER_METRE);
    canvas_text(esp_label_x(box.x, box.w, canvas_text_width(dist, FONT_MONO)),
                esp_dist_label_y(box.y, box.h), dist, C_BONEDIM, FONT_MONO);
  }
}

// Draws both ESP categories in one walk of the two status lists; enemies and items share box and brackets, as
// polymost_drawsprite does. Health maximum is read from the compiled CON by (picnum, pal) (esp_names.h), else a learned
// ceiling, else a bare number.

// Shared switch-sprite walk (esp.h). picnum test first (switch_base_for is the wall scan's gate too). The invisible bit
// (cstat 0x8000) is refused for both callers; engine_sprite_field_s sign-extends it to -32768, hence the mask.
void esp_switch_sprite_walk(int gotAccess, EspSwitchSpriteFn fn, void* ctx) {
  if (!fn) return;
  // Two sequential drains, never nested (single-slot walk counter): statnum 0 for every switch, then statnum 5 (Duke's
  // MISC list) for the NUKEBUTTON.
  for (int pass = 0; pass < 2; pass++) {
  const int stat = (pass == 0) ? ESP_STAT_SWITCH : ESP_STAT_NUKEBUTTON;
  for (int i = engine_first_sprite_of_stat(stat); i >= 0;
       i = engine_next_sprite(i)) {
    const int pic = engine_sprite_field_s(i, SPR_PICNUM);
    const bool isExit = (pic == ESP_PIC_NUKEBUTTON);
    if (pass == 1 && !isExit) continue;
    if (switch_base_for(pic) < 0 && !isExit) continue;
    const int cstat = engine_sprite_field_s(i, SPR_CSTAT);
    if ((cstat & SPR_CSTAT_INVISIBLE) != 0) continue;
    const int pal   = engine_sprite_field_u8(i, SPR_PAL);
    const int hitag = engine_sprite_field_s(i, SPR_HITAG);
    const int lotag = engine_sprite_field_s(i, SPR_LOTAG);
    EspSwitchSprite s;
    s.i = i; s.pic = pic; s.cstat = cstat;
    s.si = switch_classify(pic, pal, hitag, lotag, gotAccess);
    if (s.si.family == SW_NONE) continue;
    fn(s, ctx);
  }
  }
}

struct EspSwitchMarkCtx {
  const CamState* cam; const GProj* g; const short* sintable;
  int screenW, screenH;
  EspMarker* markers; int* markerCount; int* markerSeq; int* markerOverflow;
};
static void esp_switch_mark(const EspSwitchSprite& s, void* vctx) {
  // SW_EXIT answers to its OWN category: a secret exit button appears with only "Secret areas" on, the normal exit
  // under espswitches.
  if (s.si.family == SW_EXIT) {
    const bool on = (s.si.exitKind == EXIT_SECRET) ? (g_cfg.espsecrets != 0)
                                                    : (g_cfg.espswitches != 0);
    if (!on) return;
  }
  EspSwitchMarkCtx& c = *(EspSwitchMarkCtx*)vctx;
  const int wx = engine_sprite_field_l(s.i, SPR_X);
  const int wy = engine_sprite_field_l(s.i, SPR_Y);
  const int wz = engine_sprite_field_l(s.i, SPR_Z);
  const ScreenPt p = project_world(*c.cam, *c.g, c.sintable, wx, wy, wz);
  if (!p.visible) return;
  if (!esp_dist_ok(p)) return;
  if (!proj_on_screen(p.x, p.y, c.screenW, c.screenH)) return;
  esp_collect(c.markers, c.markerCount, c.markerSeq, c.markerOverflow,
              ESP_KIND_SWITCH, s.i, p, proj_anchor_box(p.x, p.y));
}

void esp_draw(int screenW, int screenH) {
  // Teleport's one line, first and outside every other gate: it must appear whether or not any category is on (the
  // normal case while flying). camera_notice() is non-null only for its two-second window and is what opened the GL
  // bracket (esp_any_on).
  const char* notice = camera_notice();
  if (notice) {
    const int tw = canvas_text_width(notice, FONT_UI);
    canvas_text(esp_notice_x(screenW, tw), esp_notice_y(screenH), notice,
                canvas_notice_colour(), FONT_UI);
  }

  // The secret-exit level note: top-left while espsecrets is on and the level has one. Independent of the category gate
  // below (espsecrets on already implies esp_any_on()); the draw is esp_layout.h's esp_draw_secret_exit_note, the
  // suite's hook.
  esp_draw_secret_exit_note(g_cfg.espsecrets != 0 && esp_level_has_secret_exit());

  // Debug readout (debuglog on, camera flying): where the camera is and how far it may pitch, so a report can quote the
  // numbers.
  if (g_cfg.freecamdebug != 0) {
    FreecamState fc;
    if (camera_snapshot(&fc) && fc.active) {
      char line[256];
      s_readout[0] = 0;
      sprintf_s(line, "cam x=%d y=%d z=%d ang=%d pitch=%d cap=%d cutaway=%d",
                fc.x, fc.y, fc.z, fc.ang, fc.pitch, camera_pitch_cap_last(), g_cfg.freecamcutaway);
      canvas_text(esp_secret_exit_note_x(), esp_secret_exit_note_y() + 2 * font_height[FONT_SMALL],
                  line, C_BONE, FONT_SMALL);
      readout_keep(line);
      const DollhouseFrameStats st = dollhouse_frame_stats();
      sprintf_s(line, "domost calls=%d drawn=%d sky=%d solid=%d top=%d bottom=%d recs=%d",
                st.calls, st.drawn, st.sky, st.solid, st.top, st.bottom, st.recs);
      canvas_text(esp_secret_exit_note_x(), esp_secret_exit_note_y() + 3 * font_height[FONT_SMALL],
                  line, C_BONE, FONT_SMALL);
      readout_keep(line);
    }
  }

  // Self-guarded: independent of esp_any_on(), which only decides whether the GL bracket opens. The notice term is 0
  // here: a notice alone isn't a reason to walk two sprite lists.
  if (!esp_any_category_on(g_cfg.espenemies, g_cfg.espitems, g_cfg.espsecrets,
                           aim_marker_on(g_cfg.aimon, g_cfg.silenton, g_cfg.aimdebug),
                           freecam_marker_on(camera_active(), g_cfg.freecamshowplayer),
                           0,
                           g_cfg.espexplodables, g_cfg.espswitches, g_cfg.espbreakables))
    return;
  const CamState cam = engine_camera();

  const bool doLog = g_cfg.debuglog != 0 && diag_throttle_due();

  int walked = 0, badguys = 0, visible = 0;
  int farCulled = 0;
  // Enemies whose box lacked a corner and fell back to a position-only marker: an aggregate beside the per-sprite dump
  // (first three of a window).
  int boxRefused = 0;
  // Item half: walked, named (table recognised), drawn, refused (partial-corner box): four numbers for four causes of
  // "no markers".
  int itemsWalked = 0, itemsNamed = 0, itemsDrawn = 0, itemsRefused = 0, itemsFar = 0;
  int secScanned = 0, secSecret = 0, secNoCentre = 0, secProjected = 0, secDrawn = 0;
  int secFar = 0;
  SpriteDiagLine diagLines[3];
  int diagCount = 0;

  EspMarker markers[ESP_MARKER_MAX];
  int markerCount = 0, markerSeq = 0, markerOverflow = 0;
  int wallOverflow = 0;

  if (cam.valid) {
    const short* sintable = (const short*)(g_base + rva::SINTABLE);

    const GProj gEngine = engine_gproj();
    const GProj gOurs   = proj_gproj_recomputed(cam, sintable);

    // CON's TROOPSTRENGTH, read once per frame from LIZTROOP's actor strength (what `addstrength` adds to a pal-21
    // trooper, GAME.CON:4709).
    const int troopBase = engine_actor_strength(ESP_PIC_LIZTROOP);

    const int gotAccess = engine_player_got_access();

    if (g_cfg.espsecrets)
      esp_collect_secrets(cam, gEngine, sintable, screenW, screenH,
                       &secScanned, &secSecret, &secNoCentre,
                       &secProjected, &secDrawn, &secFar,
                       markers, &markerCount, &markerSeq, &markerOverflow);

    const int stats[2] = { STAT_ACTOR, STAT_ZOMBIEACTOR };
    for (int s = 0; s < 2; s++) {
      for (int i = engine_first_sprite_of_stat(stats[s]); i >= 0; i = engine_next_sprite(i)) {
        walked++;
        const bool isEnemy = engine_is_badguy(i);
        if (!isEnemy) {
          itemsWalked++;
          if (g_cfg.espitems)
            esp_collect_item(i, cam, gEngine, sintable, screenW, screenH,
                          &itemsNamed, &itemsDrawn, &itemsRefused, &itemsFar,
                          markers, &markerCount, &markerSeq, &markerOverflow);
          continue;
        }
        if (!g_cfg.espenemies) continue;
        // Liveness is the state filter (actorstate.h): shown for AS_ALIVE, CHOKING, PLAYDEAD, CLOAKED; skipped for
        // DYING, DEAD, NONE. The engine's aim() test alone kept a box through a death animation.
        const ActorState state = actor_state_cached(i);
        if (!actor_state_shown(state)) continue;
        badguys++;

        const int wx = engine_sprite_field_l(i, SPR_X);
        const int wy = engine_sprite_field_l(i, SPR_Y);
        const int wz = engine_sprite_field_l(i, SPR_Z);

        // Max health depends on (picnum, PAL): a pal-21 Assault Trooper gets `addstrength TROOPSTRENGTH` (60 vs 30),
        // both picnum 1680 via `cactor LIZTROOP` (esp_names.h). Resolved before the off-screen cull so the
        // learned-ceiling fallback sees every live enemy.
        const int pic   = engine_sprite_field_s(i, SPR_PICNUM);
        const int pal   = engine_sprite_field_u8(i, SPR_PAL);
        const int base  = engine_actor_strength(pic);
        const int ruled = esp_rule_max_health(pic, pal, base, troopBase);
        int t5 = 0;   // the ai, set by the CON's first-tic `ifai 0` block (see esp_display_hp)
        const bool hasRun = engine_actor_temp_data(i, 5, &t5) && t5 != 0;
        const int hp = esp_display_hp(target_hp_for_display(engine_sprite_field_s(i, SPR_EXTRA)), ruled, hasRun);
        const int maxhp = esp_max_health(pic, pal, hp, base, troopBase);

        const bool wantDbg = doLog && diagCount < 3;
        ProjectDebug dbg;
        const ScreenPt p = project_world(cam, gEngine, sintable, wx, wy, wz,
                                         wantDbg ? &dbg : nullptr);

        // Single classification, priority order: near clip, range, the shared off-screen cull, else drawn; names the
        // rejection reason when logging. Invisible is split by ProjectDebug::reject (near clip vs pitch clip: different
        // causes). dbg is filled only when wantDbg.
        const char* reason;
        bool drawn;
        if      (!p.visible)             { reason = !wantDbg          ? "clipped"
                                                  : dbg.reject == PROJ_REJECT_PITCH   ? "pitchclip"
                                                  : dbg.reject == PROJ_REJECT_INVALID ? "camera"
                                                                                      : "nearclip";
                                            drawn = false; }
        else if (!esp_dist_ok(p))                  { reason = "cull-dist";  drawn = false;
                                                     farCulled++; }
        else if (!proj_on_screen(p.x, p.y, screenW, screenH)) {
          reason = p.x < -PROJ_CULL_MARGIN          ? "cull-x-low"
                 : p.x > screenW + PROJ_CULL_MARGIN ? "cull-x-high"
                 : p.y < -PROJ_CULL_MARGIN          ? "cull-y-low"
                                                    : "cull-y-high";
          drawn = false;
        }
        else                                       { reason = "drawn";      drawn = true;  }

        const int cstat = engine_sprite_field_s(i, SPR_CSTAT);
        const int tsx   = engine_tilesizx(pic);
        const int tsy   = engine_tilesizy(pic);
        const int xrep  = engine_sprite_field_u8(i, SPR_XREPEAT);
        const int yrep  = engine_sprite_field_u8(i, SPR_YREPEAT);

        int diagSlot = -1;
        if (wantDbg) {
          diagSlot = diagCount++;
          SpriteDiagLine& d = diagLines[diagSlot];
          d.index = i; d.wx = wx; d.wy = wy; d.wz = wz;
          d.dx = dbg.dx; d.dy = dbg.dy;
          d.sinang = dbg.sinang; d.cosang = dbg.cosang;
          d.fwd = dbg.fwd; d.lat = dbg.lat;
          d.dh = dbg.dh; d.oz2 = dbg.oz2;
          d.glat = dbg.glat; d.gdepth = dbg.gdepth;
          d.depth = p.depth; d.x = p.x; d.y = p.y; d.visible = p.visible;
          d.reason = reason;
          d.pic = pic; d.pal = pal; d.hp = hp; d.maxhp = maxhp;
          d.base = base; d.ruled = ruled;
          d.tsx = tsx; d.tsy = tsy; d.xrep = xrep; d.yrep = yrep;
          d.bx = 0; d.by = 0; d.bw = 0; d.bh = 0; d.corners = 0;   // iff drawn
        }

        if (!drawn) continue;
        visible++;

        // Box: the sprite's world extents projected as four points. project_sprite_box refuses unless all four project
        // (the top pair's own dz can fail the pitched-plane clip on a tall close enemy; unioning survivors made a
        // screen-wide red streak); esp_box_or_anchor claims position only.
        const SpriteExtents ext = proj_sprite_extents(wz, cstat, tsx, tsy, xrep, yrep);
        const ScreenBox projected = project_sprite_box(cam, gEngine, sintable, wx, wy, ext);
        if (!proj_box_corners_ok(projected.corners)) boxRefused++;
        const ScreenBox box = esp_box_or_anchor(projected, p.x, p.y);
        if (diagSlot >= 0) {
          SpriteDiagLine& d = diagLines[diagSlot];
          d.bx = box.x; d.by = box.y; d.bw = box.w; d.bh = box.h;
          d.corners = projected.corners;
        }

        esp_collect(markers, &markerCount, &markerSeq, &markerOverflow,
                   ESP_KIND_ENEMY, i, p, box);
      }
    }

    if (g_cfg.espexplodables) {
      for (int i = engine_first_sprite_of_stat(ESP_STAT_STANDABLE); i >= 0;
           i = engine_next_sprite(i)) {
        const int pic = engine_sprite_field_s(i, SPR_PICNUM);
        const char* nm = esp_explodable_name(pic);
        if (!nm) continue;   // not one of the four crack tiles

        // Shown only while hitag > 0: a hitag-0 crack is inert decoration the game never detonates. No liveness test:
        // detonation deletes the sprite.
        const int hitag = engine_sprite_field_s(i, SPR_HITAG);
        if (!esp_crack_visible(hitag)) continue;

        const int wx = engine_sprite_field_l(i, SPR_X);
        const int wy = engine_sprite_field_l(i, SPR_Y);
        const int wz = engine_sprite_field_l(i, SPR_Z);
        const ScreenPt p = project_world(cam, gEngine, sintable, wx, wy, wz);
        if (!p.visible) continue;
        if (!esp_dist_ok(p)) continue;
        if (!proj_on_screen(p.x, p.y, screenW, screenH)) continue;

        esp_collect(markers, &markerCount, &markerSeq, &markerOverflow,
                   ESP_KIND_EXPLODABLE, i, p, proj_anchor_box(p.x, p.y));
      }
    }

    // Switches and locks: own gate, OR'd with espsecrets so a secret exit button still gets walked with only "Secret
    // areas" on.
    if (g_cfg.espswitches || g_cfg.espsecrets) {
      EspSwitchMarkCtx sctx;
      sctx.cam = &cam; sctx.g = &gEngine; sctx.sintable = sintable;
      sctx.screenW = screenW; sctx.screenH = screenH;
      sctx.markers = markers; sctx.markerCount = &markerCount;
      sctx.markerSeq = &markerSeq; sctx.markerOverflow = &markerOverflow;
      esp_switch_sprite_walk(gotAccess, esp_switch_mark, &sctx);
    }

    // Wall snapshot: the tic-clock scan's publish, read through the seqlock; a false return draws none rather than a
    // mix of two tics. One read serves both categories (esp_world_tic is the shared producer), each mark collected
    // under its own switch.
    if (g_cfg.espbreakables || g_cfg.espswitches) {
      EspWallSnapshot ws;
      if (esp_wall_snapshot(&ws)) {
        wallOverflow = ws.overflow;
        const int wsCount = esp_wall_count_clamped(ws.count);
        for (int k = 0; k < wsCount; k++) {
          const EspWallMark& wm = ws.m[k];

          if (!esp_wall_mark_wanted(wm.kind, g_cfg.espbreakables, g_cfg.espswitches)) continue;
          if (wm.kind == ESP_WKIND_SWITCH) {
            if (switch_classify(wm.picnum, wm.pal, wm.hitag, wm.lotag,
                                gotAccess).family == SW_NONE)
              continue;
          }

          const ScreenPt p = project_world(cam, gEngine, sintable, wm.x, wm.y, wm.z);
          if (!p.visible) continue;
          if (!esp_dist_ok(p)) continue;
          if (!proj_on_screen(p.x, p.y, screenW, screenH)) continue;

          const int kind = (wm.kind == ESP_WKIND_SWITCH) ? ESP_KIND_SWITCH : ESP_KIND_EXPLODABLE;
          esp_collect(markers, &markerCount, &markerSeq, &markerOverflow,
                     kind, rva::MAX_SPRITES + wm.wallnum, p,
                     proj_anchor_box(p.x, p.y));
        }
      }
    }

    // Path to the nearest secret: drawn inside this bracket BEFORE the marker pass, so every marker sits on top of the
    // line. It gates itself on espsecrets and espsecretpath, so the call is unconditional.
    secretpath_draw(screenW, screenH);

    esp_sort_far_to_near(markers, markerCount);
    for (int k = 0; k < markerCount; k++) {
      const EspMarker& mk = markers[k];
      switch (mk.kind) {
        case ESP_KIND_SECRET:     esp_draw_secret_marker(mk);          break;
        case ESP_KIND_ITEM:       esp_draw_item_marker(mk);            break;
        case ESP_KIND_ENEMY:      esp_draw_enemy_marker(mk, troopBase); break;
        case ESP_KIND_EXPLODABLE: esp_draw_explodable_marker(mk);      break;
        case ESP_KIND_SWITCH:     esp_draw_switch_marker(mk, gotAccess); break;
      }
    }

    // Aim calibration marker: drawn last so it sits on top of any ESP box around the same enemy. A false snapshot read
    // draws nothing rather than a marker from two tics.
    if (aim_marker_on(g_cfg.aimon, g_cfg.silenton, g_cfg.aimdebug)) {
      AimSnapshot as;
      if (aim_snapshot(&as) && as.targetIndex >= 0) {
        const ScreenPt rawPt = project_world(cam, gEngine, sintable, as.rawx, as.rawy, as.rawz);
        const ScreenPt aimPt = project_world(cam, gEngine, sintable, as.aimx, as.aimy, as.aimz);
        const unsigned colour = canvas_aim_colour();

        if (rawPt.visible && proj_on_screen(rawPt.x, rawPt.y, screenW, screenH)) {
          const int bx = aim_marker_x(rawPt.x), by = aim_marker_y(rawPt.y);
          const int span = aim_marker_span();
          canvas_rect(bx, by, span, AIM_MARKER_T, colour);                      // top
          canvas_rect(bx, aim_marker_edge_far(rawPt.y), span, AIM_MARKER_T, colour);  // bottom
          canvas_rect(bx, by, AIM_MARKER_T, span, colour);                      // left
          canvas_rect(aim_marker_edge_far(rawPt.x), by, AIM_MARKER_T, span, colour);  // right
        }

        if (aimPt.visible && proj_on_screen(aimPt.x, aimPt.y, screenW, screenH)) {
          canvas_rect(aim_marker_x(aimPt.x), aim_marker_y(aimPt.y),
                      aim_marker_span(), aim_marker_span(), colour);
          char lab[32];
          _snprintf_s(lab, sizeof(lab), _TRUNCATE, "%s %d", AIM_MARKER_LABEL, as.targetIndex);
          canvas_text(esp_label_x(aimPt.x, 0, canvas_text_width(lab, FONT_SMALL)),
                      aim_marker_label_y(aimPt.y), lab, colour, FONT_SMALL);
        }
      }
    }

    // Free camera's player marker, drawn last of all. Projects the player's own sprite, not ps[].posx/posy/posz (which
    // the camera bracket substitutes during displayrooms). The gate reads the render-clock snapshot, not
    // camera_active(); esp_any_on() uses the tic-clock flag.
    FreecamState fc;
    const bool flying = camera_snapshot(&fc) && fc.active;
    if (freecam_marker_on(flying, g_cfg.freecamshowplayer)) {
      const PlayerAim pa = engine_player_aim();
      if (pa.valid && pa.spriteIndex >= 0) {
        const int wx = engine_sprite_field_l(pa.spriteIndex, SPR_X);
        const int wy = engine_sprite_field_l(pa.spriteIndex, SPR_Y);
        const int wz = engine_sprite_field_l(pa.spriteIndex, SPR_Z);
        const ScreenPt p = project_world(cam, gEngine, sintable, wx, wy, wz);
        if (p.visible && proj_on_screen(p.x, p.y, screenW, screenH)) {
          const unsigned colour = canvas_player_colour();
          for (int r = 0; r < player_marker_rows(); r++)
            canvas_rect(player_marker_row_x(p.x, r), player_marker_row_y(p.y, r),
                        player_marker_row_w(r), 1, colour);
          canvas_text(esp_label_x(p.x, 0, canvas_text_width(PLAYER_MARKER_LABEL, FONT_SMALL)),
                      player_marker_label_y(p.y), PLAYER_MARKER_LABEL, colour, FONT_SMALL);
        }
      }
    }

    // Camera state, a sintable sanity check (entries with known values) and up to 3 sprites' rejection detail; needs
    // cam.valid.
    if (doLog) {
      LOGI("esp diag cam: posx=%d posy=%d posz=%d ang=%d horiz=%d ydimen=%d xdimenscale=%d "
           "viewingrange=%d halfxdimen=%d windowx1=%d windowy1=%d screenw=%d screenh=%d",
           cam.posx, cam.posy, cam.posz, cam.ang, cam.horiz, cam.ydimen, cam.xdimenscale,
           cam.viewingrange, cam.halfxdimen, cam.windowx1, cam.windowy1, screenW, screenH);

      LOGI("esp diag sintable: idx0=%d(exp 0) idx256=%d(exp 11585) idx512=%d(exp 16384) idx1536=%d(exp -16384)",
           sintable[0], sintable[256], sintable[512], sintable[1536]);

      LOGI("esp diag gproj eng: valid=%d cosang=%.9f sinang=%.9f cosang2=%.9f sinang2=%.9f "
           "halfx=%.3f horiz=%.3f shang=%.9f chang=%.9f yxscale=%.6f",
           gEngine.valid ? 1 : 0, gEngine.cosang, gEngine.sinang, gEngine.cosang2,
           gEngine.sinang2, gEngine.halfx, gEngine.horiz, gEngine.shang, gEngine.chang,
           gEngine.yxscale);
      LOGI("esp diag gproj our: valid=%d cosang=%.9f sinang=%.9f cosang2=%.9f sinang2=%.9f "
           "halfx=%.3f horiz=%.3f shang=%.9f chang=%.9f yxscale=%.6f",
           gOurs.valid ? 1 : 0, gOurs.cosang, gOurs.sinang, gOurs.cosang2,
           gOurs.sinang2, gOurs.halfx, gOurs.horiz, gOurs.shang, gOurs.chang,
           gOurs.yxscale);

      for (int k = 0; k < diagCount; k++) {
        const SpriteDiagLine& d = diagLines[k];
        LOGI("esp diag sprite: index=%d wx=%d wy=%d wz=%d dx=%d dy=%d sinang=%d cosang=%d "
             "fwd=%lld lat=%lld glat=%.3f gdepth=%.3f dh=%d oz2=%.2f depth=%d x=%d y=%d "
             "visible=%d reason=%s",
             d.index, d.wx, d.wy, d.wz, d.dx, d.dy, d.sinang, d.cosang,
             d.fwd, d.lat, d.glat, d.gdepth, d.dh, d.oz2, d.depth, d.x, d.y,
             d.visible ? 1 : 0, d.reason);
        // Box diagnosis: tsx/tsy/xrep/yrep say what it was sized from, bw/bh what came out. pal/base/rule confirm the
        // (picnum, pal) model: two Assault Troopers must log pal=0 base=30 rule=30 and pal=21 base=30 rule=60. base=0
        // means actorscrptr couldn't be read.
        const char* nm = esp_enemy_name_for(d.pic, engine_addon_index());
        LOGI("esp diag box: index=%d pic=%d pal=%d hp=%d maxhp=%d base=%d rule=%d "
             "tsx=%d tsy=%d xrep=%d yrep=%d bx=%d by=%d bw=%d bh=%d corners=%d name=%s",
             d.index, d.pic, d.pal, d.hp, d.maxhp, d.base, d.ruled,
             d.tsx, d.tsy, d.xrep, d.yrep,
             d.bx, d.by, d.bw, d.bh, d.corners, nm ? nm : "(unnamed)");
      }
    }
  }

  // Read top to bottom: verified=0 fingerprint failed; cam.valid=0 camera globals not ready; walked=0 both lists empty;
  // badguys=0 nothing passed engine_is_badguy; visible=0 (badguys>0) every candidate culled (reason= in the sprite
  // lines says which).
  if (doLog) {
    // farculled: the slider's number; healthy but high means the range is too low. on=0 tells the category off from
    // badguy() rejecting everything. markeroverflow: ESP_MARKER_MAX too small; wallmarkoverflow: the tic scan couldn't
    // fit every wall in ESP_WALL_MAX.
    LOGI("esp diag: verified=%d cam.valid=%d on=%d walked=%d badguys=%d visible=%d "
         "boxrefused=%d farculled=%d drawdist=%d maxhpfull=%d markeroverflow=%d "
         "wallmarkoverflow=%d",
         engine_verified() ? 1 : 0, cam.valid ? 1 : 0, g_cfg.espenemies ? 1 : 0,
         walked, badguys, visible, boxRefused, farCulled,
         g_cfg.espdrawdist, esp_max_health_overflow(), markerOverflow, wallOverflow);
    LOGI("esp diag items: on=%d walked=%d classified=%d drawn=%d boxrefused=%d farculled=%d",
         g_cfg.espitems ? 1 : 0, itemsWalked, itemsNamed, itemsDrawn, itemsRefused, itemsFar);
    LOGI("esp diag secrets: on=%d scanned=%d secret=%d nocentre=%d projected=%d drawn=%d "
         "farculled=%d",
         g_cfg.espsecrets ? 1 : 0, secScanned, secSecret, secNoCentre,
         secProjected, secDrawn, secFar);
  }
}
