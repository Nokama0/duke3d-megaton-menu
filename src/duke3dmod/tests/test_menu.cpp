#include "test_common.h"
#include "../light.h"   // light_row_unavailable, light_mark_unavailable, light_reset_available
#include "../firerate.h" // FIRERATE_MIN/MAX: the switch-speed row's declared ends
#include "../tbot.h"    // TBOT_WHEN_CROSSHAIR/AIMBOT, TBOT_DELAY_MS_MIN/MAX
#include "../anim.h"    // anim_switch_apply, ANIM_OFF/SIMPLE/GLASS
#include "../entry.h"   // EK_INT: the one pin below needing an ARMED capture,
                        // which only a click on a binding row can produce

#include "test_blocks.h"

// To add a check, append a tb_ function to the area file it belongs in, declare it in test_blocks.h and call it from
// main() in test_host.cpp after whatever it depends on.





void tb_0003_defaults_when_file_absent_menu() {
config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.menukey == 0x2D);
  CHECK(g_cfg.fov == 90);
  CHECK(!g_cfg.fovon);
  CHECK(!g_cfg.debuglog);

  tmp = "test_duke3dmod.ini";
  f = nullptr;
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nfovon=1\nfov=118\ndebuglog=1\nsomethingfuture=42\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.fovon && g_cfg.fov == 118 && g_cfg.debuglog);

  // Out-of-range values clamp rather than corrupt.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nfov=999\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.fov == 140);

  // Unknown keys survive a save so a newer config is not destroyed by an older DLL.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nfov=100\nsomethingfuture=42\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(config_save(tmp));
}



// vk_to_sdl_scancode: the default menu key (VK_INSERT) and a sample of other groups map to the SDL scancode input.cpp
// compares; a left-out key reports failure so the caller can fall back (tb_0119 pins the full table).
void tb_0017_vk_to_sdl_scancode_default_menu_key_vk_insert() {
    int sc = -1;
    CHECK(vk_to_sdl_scancode(0x2D, &sc) && sc == 73);   // VK_INSERT -> SDL_SCANCODE_INSERT
    CHECK(vk_to_sdl_scancode(0x2E, &sc) && sc == 76);   // VK_DELETE -> SDL_SCANCODE_DELETE
    CHECK(vk_to_sdl_scancode(0x24, &sc) && sc == 74);   // VK_HOME   -> SDL_SCANCODE_HOME
    CHECK(vk_to_sdl_scancode(0x23, &sc) && sc == 77);   // VK_END    -> SDL_SCANCODE_END
    CHECK(vk_to_sdl_scancode(0x21, &sc) && sc == 75);   // VK_PRIOR  -> SDL_SCANCODE_PAGEUP
    CHECK(vk_to_sdl_scancode(0x22, &sc) && sc == 78);   // VK_NEXT   -> SDL_SCANCODE_PAGEDOWN
    CHECK(vk_to_sdl_scancode(0x09, &sc) && sc == 43);   // VK_TAB    -> SDL_SCANCODE_TAB
    CHECK(vk_to_sdl_scancode(0xC0, &sc) && sc == 53);   // VK_OEM_3  -> SDL_SCANCODE_GRAVE
    CHECK(vk_to_sdl_scancode(0x70, &sc) && sc == 58);   // VK_F1     -> SDL_SCANCODE_F1
    CHECK(vk_to_sdl_scancode(0x7B, &sc) && sc == 69);   // VK_F12    -> SDL_SCANCODE_F12
    CHECK(vk_to_sdl_scancode(0x41, &sc) && sc == 4);    // VK_A -> SDL_SCANCODE_A
    // Escape is left out (the capture's own cancel key); modifiers and lock keys are the others.
    CHECK(!vk_to_sdl_scancode(0x1B, &sc));              // Escape stays unmapped
    CHECK(!vk_to_sdl_scancode(0x10, &sc));              // VK_SHIFT, a modifier
  }



void tb_0032_panel_left_anchored_fully_screen() {
p = menu_panel_rect(3440, 1440);
  CHECK(p.x > 0 && p.y > 0);
  CHECK(p.x + p.w <= 3440 && p.y + p.h <= 1440);
  CHECK(p.x < 3440 / 4);                    // left-anchored, not centred
  q = menu_panel_rect(1920, 1080);
  CHECK(q.x + q.w <= 1920 && q.y + q.h <= 1080);
  // The panel has fixed logical dimensions, so a small resolution must still
  // clamp it fully on screen rather than letting it run off the bottom/right.
  s = menu_panel_rect(1024, 768);
  CHECK(s.x > 0 && s.y > 0);
  CHECK(s.x + s.w <= 1024 && s.y + s.h <= 768);
  menu_set_tab(TAB_WEAPONS);
  CHECK(menu_active_tab() == TAB_WEAPONS);
  menu_set_tab(-1);
  CHECK(menu_active_tab() == TAB_WEAPONS);
  menu_set_tab(TAB_COUNT);
  CHECK(menu_active_tab() == TAB_WEAPONS);

  // Engine RVAs pinned against the production constants, not retyped: a
  // typo without a CHECK silently corrupts game memory.
  CHECK(rva::SPRITE       == 0x019D4B60u);
  CHECK(rva::SECTOR       == 0x01893F80u);
  CHECK(rva::HEADSPRITESTAT == 0x018907C0u);
  CHECK(rva::NEXTSPRITESTAT == 0x01A91760u);
  CHECK(rva::GLOBALPOSX   == 0x01ADC9A4u);
  CHECK(rva::GLOBALPOSY   == 0x01967DCCu);
  CHECK(rva::GLOBALPOSZ   == 0x01893F20u);
  CHECK(rva::GLOBALANG    == 0x019D4B5Eu);
  CHECK(rva::GLOBALHORIZ  == 0x018D18BCu);
  CHECK(rva::HALFXDIMEN   == 0x01AD6440u);
  CHECK(rva::XDIMENSCALE  == 0x01893F30u);
  // YDIMENSCALE is deliberately absent: polymost_drawsprite never reads it, only dorotatesprite's 2D HUD path does.
  CHECK(rva::VIEWINGRANGE == 0x01AF7094u);  // load-bearing for project_world, not just FOV diagnostics

  // Polymost's nine per-frame projection constants (common.h) are read as raw doubles by engine_gproj(): a wrong
  // address silently feeds garbage into the projection.
  CHECK(rva::GCOSANG   == 0x01774350u);   // VA 0x01b74350
  CHECK(rva::GSINANG   == 0x017732D0u);   // VA 0x01b732d0
  CHECK(rva::GCOSANG2  == 0x01773280u);   // VA 0x01b73280
  CHECK(rva::GSINANG2  == 0x01774340u);   // VA 0x01b74340
  CHECK(rva::GHALFX    == 0x00A14C58u);   // VA 0x00e14c58
  CHECK(rva::GHORIZ    == 0x00A14C60u);   // VA 0x00e14c60
  CHECK(rva::GSHANG    == 0x01773290u);   // VA 0x01b73290
  CHECK(rva::GCHANG    == 0x01773288u);   // VA 0x01b73288
  CHECK(rva::GYXSCALE  == 0x009D0C38u);   // VA 0x00dd0c38
  // Four angle constants are distinct addresses, not aliases: gcosang and
  // gcosang2 differ only by the viewingrange factor, invisible at neutral FOV.
  CHECK(rva::GCOSANG != rva::GCOSANG2 && rva::GSINANG != rva::GSINANG2);
  CHECK(rva::GSHANG != rva::GCHANG);
  CHECK(rva::GHORIZ != rva::GHALFX && rva::GHORIZ != rva::YDIMEN);
  // ydimen (3D viewport height) differs from YDIM (full screen height, 0x01967DC4); swapping them moves project_world's
  // vertical origin by half the status bar.
  CHECK(rva::YDIMEN == 0x018CFD5Cu);
  CHECK(rva::YDIM   == 0x01967DC4u);
  CHECK(rva::YDIMEN != rva::YDIM);
  CHECK(rva::WINDOWX1     == 0x019D2624u);
  CHECK(rva::WINDOWY1     == 0x01893F2Cu);
  CHECK(rva::NUMSECTORS   == 0x018907A0u);
  CHECK(rva::SINTABLE     == 0x01A8B020u);
  CHECK(rva::THUNK_BADGUY_RVA == 0x00003BCFu);
  CHECK(rva::BADGUY       == 0x000B9C50u);
  CHECK(rva::SPRITE_STRIDE == 44);
  CHECK(rva::SECTOR_STRIDE == 40);
  CHECK(rva::MAX_SPRITES   == 16384);
  CHECK(rva::MAX_SECTORS   == 4096);
  CHECK(rva::MAX_STATNUM   == 1024);
  // Field offsets must match the PDB layout exactly; a wrong one reads the
  // neighbouring field and silently mislabels every sprite.
  CHECK(SPR_X == 0x00);  CHECK(SPR_Y == 0x04);  CHECK(SPR_Z == 0x08);
  CHECK(SPR_CSTAT == 0x0C); CHECK(SPR_PICNUM == 0x0E);
  CHECK(SPR_XREPEAT == 0x14); CHECK(SPR_YREPEAT == 0x15);
  CHECK(SPR_SECTNUM == 0x18); CHECK(SPR_STATNUM == 0x1A);
  CHECK(SPR_EXTRA == 0x2A);

  // sprite_field_read_ok: reading `width` bytes at `offset` must not run past the 44-byte sprite struct into the next
  // sprite; checked at the end of the read, not just the start.
  CHECK(sprite_field_read_ok(42, 2) == true);   // offset 42, width 2: reads bytes 42-43 (valid)
  CHECK(sprite_field_read_ok(42, 4) == false);  // offset 42, width 4: reads bytes 42-45 (past end)
  CHECK(sprite_field_read_ok(43, 2) == false);  // offset 43, width 2: reads bytes 43-44 (past end)
  CHECK(sprite_field_read_ok(0, 4) == true);
}



// overlay_should_draw (overlay.h) is the pure extraction of overlay.cpp's GL-bracket-opening decision (overlay.cpp
// cannot link into test_host): all sixteen (menuOpen, anyEspOn, dmgNumbersOn, secretTriggersOn) states, then an
// exhaustive sweep so a dropped term is visible.
void tb_0046_overlay_should_draw_overlay_h_task_3() {
CHECK(!overlay_should_draw(false, false, false, false));  // nothing on: bracket must stay shut
    // The world overlays also need a level being played: the level-complete tally and the game's own menus draw over a
    // stale world, and the markers went on drawing across both.
    CHECK(overlay_world_should_draw(true));
    CHECK(!overlay_world_should_draw(false));
  CHECK( overlay_should_draw(true,  false, false, false)); // panel open: opens for the panel
  CHECK( overlay_should_draw(false, true,  false, false)); // ESP on: opens for markers alone
  CHECK( overlay_should_draw(false, false, true,  false)); // damage numbers alone open it too
  CHECK( overlay_should_draw(false, false, false, true));  // and so do the secret triggers
  // Every one of the sixteen: the bracket opens for ANY term and for nothing
  // else, so a term silently dropped from the disjunction fails here.
  {
    bool agree = true;
    for (int m = 0; m <= 1; m++)
      for (int e = 0; e <= 1; e++)
        for (int d = 0; d <= 1; d++)
          for (int t = 0; t <= 1; t++)
            if (overlay_should_draw(m != 0, e != 0, d != 0, t != 0) != (m || e || d || t))
              agree = false;
    CHECK(agree);
  }
}




// Pins the draw-distance cull: bounds, the shared clamp, and the predicate
// (which field it reads, the boundary, monotonicity).
void tb_0048_visuals_tab_draw_distance_cull() {
    // Bounds as literals, not `== ESP_DRAWDIST_MIN`, so a moved constant
    // takes its own check with it.
    CHECK(ESP_DRAWDIST_MIN     == 512);     // one metre
    CHECK(ESP_DRAWDIST_DEFAULT == 131072);  // 256 metres: the MAXIMUM
    CHECK(ESP_DRAWDIST_MAX     == 131072);  // 256 metres
    CHECK(ESP_DRAWDIST_MIN > 0);
    CHECK(ESP_DRAWDIST_MIN < ESP_DRAWDIST_DEFAULT);
    // Default is the top of the range: a cull defaulting to less would hide
    // markers on a fresh install, reading like a broken projection.
    CHECK(ESP_DRAWDIST_DEFAULT == ESP_DRAWDIST_MAX);
    CHECK(ESP_DRAWDIST_MAX / ESP_DRAWDIST_MIN >= 10);
    CHECK(ESP_DRAWDIST_MIN / BUILD_UNITS_PER_METRE >= 1);
    CHECK(ESP_DRAWDIST_MAX / BUILD_UNITS_PER_METRE <= 512);

    CHECK(esp_draw_distance_clamped(999999)  == 131072);
    CHECK(esp_draw_distance_clamped(131073)  == 131072);
    CHECK(esp_draw_distance_clamped(32769)   == 32769);   // in range now
    CHECK(esp_draw_distance_clamped(0)      == 512);
    CHECK(esp_draw_distance_clamped(-5)     == 512);
    CHECK(esp_draw_distance_clamped(511)    == 512);
    CHECK(esp_draw_distance_clamped(512)    == 512);
    CHECK(esp_draw_distance_clamped(513)    == 513);
    CHECK(esp_draw_distance_clamped(1000)   == 1000);
    CHECK(esp_draw_distance_clamped(6144)   == 6144);
    CHECK(esp_draw_distance_clamped(32767)  == 32767);
    CHECK(esp_draw_distance_clamped(32768)  == 32768);
    CHECK(esp_draw_distance_clamped(131072) == 131072);
    // Int extremes: a hand-edited ini reaches atoi, atoi reaches this.
    CHECK(esp_draw_distance_clamped(2147483647)      == 131072);
    CHECK(esp_draw_distance_clamped(-2147483647 - 1) == 512);
    CHECK(clamp_int(999999, 512, 131072) == 131072);
    // The switch is its own term: on, every distance draws; off, the slider
    // decides, and a garbage distance is still refused either way.
    CHECK(esp_range_unlimited(1) && !esp_range_unlimited(0));
    CHECK(esp_within_draw_distance_or_unlimited(999999, 512, 1));
    CHECK(!esp_within_draw_distance_or_unlimited(999999, 512, 0));
    CHECK(esp_within_draw_distance_or_unlimited(400, 512, 0));
    CHECK(!esp_within_draw_distance_or_unlimited(-1, 512, 1));   // garbage read, both ways
    CHECK(!esp_within_draw_distance_or_unlimited(-1, 512, 0));

    CHECK( esp_within_draw_distance(0,     6144));   // right on top of you
    CHECK( esp_within_draw_distance(3000,  6144));   // comfortably inside
    CHECK( esp_within_draw_distance(6143,  6144));   // one unit inside
    CHECK( esp_within_draw_distance(6144,  6144));   // EXACTLY at: still drawn
    CHECK(!esp_within_draw_distance(6145,  6144));   // one unit past: culled
    CHECK(!esp_within_draw_distance(20000, 6144));   // far past
    CHECK( esp_within_draw_distance(20000, 32768));
    CHECK( esp_within_draw_distance(32768, 32768));
    CHECK(!esp_within_draw_distance(32769, 32768));
    CHECK( esp_within_draw_distance(1000,  6144));
    CHECK(!esp_within_draw_distance(1000,  512));
    CHECK( esp_within_draw_distance(512,   512));
    CHECK( esp_within_draw_distance(511,   512));
    CHECK( esp_within_draw_distance(0,     512));
    CHECK(!esp_within_draw_distance(40000, 32768));

    // project_world saturates dist2d to the full int range, so INT_MAX must
    // be culled without the comparison overflowing.
    CHECK(!esp_within_draw_distance(2147483647, 32768));
    // Negative distance is impossible from project_world (dist2d is a
    // sqrt): refuse as garbage rather than draw it.
    CHECK(!esp_within_draw_distance(-1, 6144));
    CHECK(!esp_within_draw_distance(-2147483647 - 1, 32768));

    // An out-of-range setting still produces a usable range: esp.cpp reads
    // g_cfg directly (config_load's clamp does not cover every route), so
    // a bad field must not silently switch the feature off. Zero behaves as the minimum.
    CHECK( esp_within_draw_distance(400,   0));
    CHECK( esp_within_draw_distance(400,   512));
    CHECK(!esp_within_draw_distance(600,   0));
    CHECK(!esp_within_draw_distance(600,   512));
    CHECK( esp_within_draw_distance(20000, 999999));
    CHECK( esp_within_draw_distance(40000, 999999));   // past the old 64 m top, inside 256 m
    CHECK(!esp_within_draw_distance(140000, 999999));  // past the new one
    CHECK(!esp_within_draw_distance(600,   -2147483647 - 1));

    // Sweep asserts it saw both a yes and a no (a constant predicate would
    // otherwise pass trivially) and that the cull is monotonic in distance.
    {
      bool everOk = false, everRejected = false, monotonic = true, wasOk = true;
      for (int d = 0; d <= 40000; d++) {
        const bool ok = esp_within_draw_distance(d, 6144);
        if (ok) everOk = true; else everRejected = true;
        if (ok && !wasOk) monotonic = false;
        wasOk = ok;
      }
      CHECK(everOk);
      CHECK(everRejected);
      CHECK(monotonic);
      bool flipAtSetting = true;
      for (int d = 0; d <= 40000; d++)
        if (esp_within_draw_distance(d, 6144) != (d <= 6144)) flipAtSetting = false;
      CHECK(flipAtSetting);
    }
    {
      bool flipAtSetting = true;
      for (int d = 0; d <= 40000; d++)
        if (esp_within_draw_distance(d, 12000) != (d <= 12000)) flipAtSetting = false;
      CHECK(flipAtSetting);
    }
    {
      bool ok = true;
      for (int m = ESP_DRAWDIST_MIN; m <= ESP_DRAWDIST_MAX; m++)
        if (esp_within_draw_distance(6000, m) != (m >= 6000)) ok = false;
      CHECK(ok);
    }


    // Which field the cull reads: p.depth (the engine's forward term, scaled by viewingrange) instead of p.dist2d would
    // look like a working feature, so every fixture sets the two to different values.
    {
      ScreenPt p;
      p.x = 100; p.y = 100; p.visible = true;

      p.dist2d = 3000; p.depth = 3000 * 16;
      CHECK(esp_marker_in_range(p, 6144));

      p.dist2d = 20000; p.depth = 100;
      CHECK(!esp_marker_in_range(p, 6144));

      p.dist2d = 6144; p.depth = 999999;
      CHECK(esp_marker_in_range(p, 6144));
      p.dist2d = 6145; p.depth = 0;
      CHECK(!esp_marker_in_range(p, 6144));

      {
        bool ignoresDepth = true;
        const int depths[7] = { -2147483647 - 1, -1, 0, 1, 6144, 100000, 2147483647 };
        for (int k = 0; k < 7; k++) {
          p.dist2d = 3000; p.depth = depths[k];
          if (!esp_marker_in_range(p, 6144)) ignoresDepth = false;
          p.dist2d = 30000;
          if (esp_marker_in_range(p, 6144)) ignoresDepth = false;
        }
        CHECK(ignoresDepth);
      }

      // And it agrees with the raw predicate for every distance: one is the field selector, the other the comparison.
      {
        bool agrees = true, sawIn = false, sawOut = false;
        for (int d = 0; d <= 40000; d += 7) {
          p.dist2d = d; p.depth = 40000 - d;   // deliberately anti-correlated
          const bool in = esp_marker_in_range(p, 6144);
          if (in != esp_within_draw_distance(d, 6144)) agrees = false;
          if (in) sawIn = true; else sawOut = true;
        }
        CHECK(agrees);
        CHECK(sawIn);     // both answers reached, so "agrees" is not
        CHECK(sawOut);    // trivially true of a sweep that never varies
      }

      // The setting is read at more than one value (not just the default 6144, which a hard-coded-default entry point
      // would also pass).
      p.dist2d = 20000; p.depth = 1;
      CHECK( esp_marker_in_range(p, 32768));
      CHECK(!esp_marker_in_range(p, 6144));
      p.dist2d = 1000; p.depth = 99999;
      CHECK( esp_marker_in_range(p, 6144));
      CHECK(!esp_marker_in_range(p, 512));
      p.dist2d = 3000; p.depth = 0;
      CHECK( esp_marker_in_range(p, 3000));
      CHECK(!esp_marker_in_range(p, 2999));
      {
        bool tracks = true, sawIn = false, sawOut = false;
        p.dist2d = 6000; p.depth = 123;
        for (int mx = ESP_DRAWDIST_MIN; mx <= ESP_DRAWDIST_MAX; mx++) {
          const bool in = esp_marker_in_range(p, mx);
          if (in != (mx >= 6000)) tracks = false;
          if (in) sawIn = true; else sawOut = true;
        }
        CHECK(tracks);
        CHECK(sawIn);
        CHECK(sawOut);
      }

      p.dist2d = 2147483647; p.depth = 1;
      CHECK(!esp_marker_in_range(p, 32768));
      p.dist2d = -1; p.depth = 1;
      CHECK(!esp_marker_in_range(p, 6144));
    }

    {
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(!g_cfg.espenemies);
      CHECK(!g_cfg.espitems);
      CHECK(!g_cfg.espsecrets);
      CHECK(g_cfg.espdrawdist == 131072);
      {
        const char* t = "test_esp.ini";
        FILE* f = nullptr; fopen_s(&f, t, "w");
        fputs("[nukepanel]\nespenemies=1\nespitems=1\nespsecrets=1\nespdrawdist=8192\n", f);
        fclose(f);
        config_load(t);
        CHECK(g_cfg.espenemies && g_cfg.espitems && g_cfg.espsecrets);
        CHECK(g_cfg.espdrawdist == 8192);
        CHECK(config_save(t));
        config_load(t);
        CHECK(g_cfg.espdrawdist == 8192);   // survives a round trip
        remove(t);
      }

      const char* t = "test_esp_task7.ini";
      FILE* f = nullptr;

      // A second round-trip value, distinct from every bound and the default, so a constant-writing save cannot pass by
      // luck.
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespdrawdist=1234\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espdrawdist == 1234);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espdrawdist == 1234);

      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=999999\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 131072);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=131073\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 131072);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=32767\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 32767);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=32768\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 32768);   // in range now
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=0\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 512);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=-7\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 512);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=511\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 512);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=512\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 512);
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=513\n", f); fclose(f);
      config_load(t); CHECK(g_cfg.espdrawdist == 513);

      // A clamped value survives the save: a bad ini is repaired, not
      // written straight back out.
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=999999\n", f); fclose(f);
      config_load(t);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espdrawdist == 131072);

      // Absent file -> the default, not the last value loaded; set something else first so a load that left the field
      // alone fails.
      fopen_s(&f, t, "w"); fputs("[nukepanel]\nespdrawdist=2048\n", f); fclose(f);
      config_load(t);
      CHECK(g_cfg.espdrawdist == 2048);   // the field really is something else now
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(g_cfg.espdrawdist == 131072);

      // The default survives a save and load as an absolute (not ESP_DRAWDIST_DEFAULT), and the saved text is checked:
      // a save that dropped the key would still read back as 32768, since config_load sets defaults before parsing.
      config_load(tb_path("Z:|does|not|exist.ini"));
      CHECK(config_save(t));
      {
        char saved[4096] = { 0 };
        // Text mode, matching how config_save writes it: read "rb" and the
        // line ends CRLF, which the newline below would not match.
        FILE* rf = nullptr; fopen_s(&rf, t, "r");
        CHECK(rf != nullptr);
        if (rf) { fread(saved, 1, sizeof(saved) - 1, rf); fclose(rf); }
        CHECK(strstr(saved, "espdrawdist=131072\n") != nullptr);
      }
      config_load(t);
      CHECK(g_cfg.espdrawdist == 131072);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespsecrets=1\nespitems=0\nespdrawdist=2048\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espsecrets == 1);
      CHECK(g_cfg.espitems == 0);
      CHECK(g_cfg.espdrawdist == 2048);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espsecrets == 1);
      CHECK(g_cfg.espitems == 0);
      CHECK(g_cfg.espdrawdist == 2048);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespsecrets=0\nespitems=1\nespdrawdist=16000\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.espsecrets == 0);
      CHECK(g_cfg.espitems == 1);
      CHECK(g_cfg.espdrawdist == 16000);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espsecrets == 0);
      CHECK(g_cfg.espitems == 1);
      CHECK(g_cfg.espdrawdist == 16000);

      // ...and against the FOV slider, the other int field, so a save that swapped the two numeric lines is caught: 100
      // is a legal fov and an ILLEGAL draw distance.
      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nfov=100\nespdrawdist=4096\n", f);
      fclose(f);
      config_load(t);
      CHECK(g_cfg.fov == 100);
      CHECK(g_cfg.espdrawdist == 4096);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.fov == 100);
      CHECK(g_cfg.espdrawdist == 4096);

      fopen_s(&f, t, "w");
      fputs("[nukepanel]\nespdrawdist=3000\nfuturefield=42\n", f);
      fclose(f);
      config_load(t);
      CHECK(config_save(t));
      config_load(t);
      CHECK(g_cfg.espdrawdist == 3000);

      remove(t);
      config_load(tb_path("Z:|does|not|exist.ini"));
    }

    // Inert row is inert, not merely dim: all eight states, exhaustively.
    // The second matters most -- a real click that must do nothing when disabled.
    CHECK( ui_row_accepts_click(true,  true,  true));
    CHECK(!ui_row_accepts_click(false, true,  true));   // INERT, not just dim
    CHECK(!ui_row_accepts_click(true,  false, true));
    CHECK(!ui_row_accepts_click(true,  true,  false));
    CHECK(!ui_row_accepts_click(false, false, true));
    CHECK(!ui_row_accepts_click(false, true,  false));
    CHECK(!ui_row_accepts_click(true,  false, false));
    CHECK(!ui_row_accepts_click(false, false, false));

    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    CHECK(menu_live_count(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // fov
    CHECK(menu_live_count(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // enemies
    CHECK(menu_live_count(0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // items
    CHECK(menu_live_count(0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // secrets
    CHECK(menu_live_count(0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    CHECK(menu_live_count(0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 3);
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 4);
    CHECK(menu_live_count(1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    CHECK(menu_live_count(1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    // A truthy value that is not 1 still counts once (a hand-edited ini can
    // put a 7 in the struct before the clamp sees it).
    CHECK(menu_live_count(0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);
    CHECK(menu_live_count(0, -3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);
    CHECK(menu_live_count(9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 4);
    // Cross-checked against esp_any_category_on. The aim-calibration marker is held at 0 (it opens the bracket without
    // counting here), as are aimon/silenton.
    {
      bool agree = true, sawLive = false, sawIdle = false;
      for (int fv = 0; fv < 2; fv++)
        for (int en = 0; en < 2; en++)
          for (int it = 0; it < 2; it++)
            for (int se = 0; se < 2; se++) {
              const int n = menu_live_count(fv, en, it, se, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
              if ((n > 0) != (fv != 0 || esp_any_category_on(en, it, se, 0, 0, 0, 0, 0))) agree = false;
              if (n < 0 || n > 4) agree = false;
              if (n > 0) sawLive = true; else sawIdle = true;
            }
      CHECK(agree);
      CHECK(sawLive);    // sweep reaches both answers
      CHECK(sawIdle);
    }
    CHECK(menu_live_count(0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 2);

    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // aimbot alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0) == 1);   // silent aim alone
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    CHECK(menu_live_count(1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    CHECK(menu_live_count(0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0) == 2);
    CHECK(menu_live_count(1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0) == 6);
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 1);
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, -3, 0, 0, 0, 0, 0, 0, 0, 0) == 1);
    CHECK(menu_live_count(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) == 1);   // triggerbot alone
    // Whole six-field space, asserting the additive property (count == how many of the six are truthy), so a term
    // dropped from the sum cannot pass.
    {
      bool agree = true, sawSix = false;
      for (int fv = 0; fv < 2; fv++)
      for (int en = 0; en < 2; en++)
      for (int it = 0; it < 2; it++)
      for (int se = 0; se < 2; se++)
      for (int ao = 0; ao < 2; ao++)
      for (int so = 0; so < 2; so++) {
        const int n = menu_live_count(fv, en, it, se, 0, 0, ao, so, 0, 0, 0, 0, 0, 0, 0, 0);
        const int want = fv + en + it + se + ao + so;
        if (n != want) agree = false;
        if (n == 6) sawSix = true;
      }
      CHECK(agree);
      CHECK(sawSix);   // the sweep reached the all-on corner, so "agree" is
    }

    // combat_sub_enabled, the row-enable rule both Combat sub-groups use: off is off, any nonzero value (even a
    // hand-edited 7) is on, like menu_live_count.
    CHECK(!combat_sub_enabled(0));
    CHECK( combat_sub_enabled(1));
    CHECK( combat_sub_enabled(7));
    CHECK( combat_sub_enabled(-3));
    {
      bool agree = true;
      for (int v = -5; v <= 5; v++)
        if (combat_sub_enabled(v) != (v != 0)) agree = false;
      CHECK(agree);
    }

    // combat_shared_enabled, TARGETING's either/or rule: all four aimon/silenton combinations, chiefly (0, 1), silent
    // aim alone, which an aimon-only rule gets wrong.
    CHECK(!combat_shared_enabled(0, 0));   // neither parent: off
    CHECK( combat_shared_enabled(1, 0));   // aimbot alone
    CHECK( combat_shared_enabled(0, 1));   // silent aim alone -- the whole point
    CHECK( combat_shared_enabled(1, 1));   // both
    CHECK( combat_shared_enabled(7, 0));
    CHECK( combat_shared_enabled(0, -3));
    {
      bool agree = true;
      int seen = 0;
      for (int a = 0; a < 2; a++)
        for (int s = 0; s < 2; s++) {
          const bool got = combat_shared_enabled(a, s);
          const bool want = (a != 0) || (s != 0);
          if (got != want) agree = false;
          seen++;
        }
      CHECK(agree);
      CHECK(seen == 4);   // all four combinations were actually exercised
    }

    // aim_target_label / aim_point_label: the Target and Aim point rows' named readouts (a bare switch cannot say
    // crosshair from closest).
    CHECK(!strcmp(aim_target_label(0), "Crosshair"));
    CHECK(!strcmp(aim_target_label(1), "Closest"));
    CHECK(!strcmp(aim_point_label(0),  "Centre"));
    CHECK(!strcmp(aim_point_label(1),  "Upper"));
    CHECK(!strcmp(aim_target_label(7),  "Closest"));
    CHECK(!strcmp(aim_target_label(-3), "Closest"));
    CHECK(!strcmp(aim_point_label(7),   "Upper"));
    CHECK(!strcmp(aim_point_label(-3),  "Upper"));
    {
      bool agree = true;
      for (int v = -5; v <= 5; v++) {
        const bool wantClosest = v != 0, wantUpper = v != 0;
        if ((!strcmp(aim_target_label(v), "Closest")) != wantClosest) agree = false;
        if ((!strcmp(aim_point_label(v),  "Upper"))   != wantUpper)   agree = false;
      }
      CHECK(agree);
    }
    CHECK(strcmp(aim_target_label(0), aim_point_label(0)) != 0);
    CHECK(strcmp(aim_target_label(1), aim_point_label(1)) != 0);

    // Rows driven through the real panel: no coordinate appears below,
    // since vis_sweep clicks every pixel of the panel and reports what changed.
    {
      const int savedTab = menu_active_tab();
      const VisState saved = vis_snapshot();
      const int savedFovOn = g_cfg.fovon, savedFov = g_cfg.fov;

      // 1024x768 is the smallest resolution checked, most likely to push a
      // row off the bottom; every row is exercised at both sizes.
      const int sizes[2][2] = { { 1024, 768 }, { 3440, 1440 } };
      const int subs    = VF_BOXES | VF_HEALTH | VF_NAMES | VF_DISTANCE;
      // Explodables, Breakables, Switches and locks, Fullbright, Damage numbers and Secret triggers are each a
      // top-level parent (own switch, live the moment it is on), so all join `parents`.
      const int parents = VF_ENEMIES | VF_ITEMS | VF_SECRETS | VF_EXPLODABLES | VF_BREAKABLES |
                          VF_SWITCHES | VF_FULLBRIGHT | VF_DMGNUMBERS | VF_SECRETTRIGGERS;
      // A press also reaches a slider through the reset glyph beside its track (a press-edge button), so a press-edge
      // sweep reaches "Draw distance" too, only where the fixture value differs from the default (fixtures start at
      // 6144, the default is ESP_DRAWDIST_MAX).
      const int resets = VF_DRAWDIST;

      // Every clickable control is at least this tall/wide at the
      // smallest draw scale (UI_SCALE_MIN), so the sweep lattice below
      // can never step clean over one.
      CHECK(SWEEP_STRIDE <= ui_scale_px(8, UI_SCALE_MIN));
      CHECK(SWEEP_STRIDE <= ui_scale_px(9, UI_SCALE_MIN));

      for (int z = 0; z < 2; z++) {
        const int sw = sizes[z][0], sh = sizes[z][1];
        bool multi = false;

        // Parent off: the four sub-rows must not move from any pixel, in
        // either starting state; the four parent rows still must.
        const VisState off1 = { 0, 1, 1, 1, 1, 0, 0, 6144, 0 };
        CHECK(vis_sweep(sw, sh, off1, false, &multi) == (parents | resets));
        CHECK(!multi);
        const VisState off0 = { 0, 0, 0, 0, 0, 0, 0, 6144, 0 };
        CHECK(vis_sweep(sw, sh, off0, false, &multi) == (parents | resets));
        CHECK(!multi);

        // Parent on: every row on the tab is reachable in both directions, asserted as equality with the full set so a
        // row that flipped something extra is caught. on1 has `secrets` on, so its sub-row "Secret volume" belongs in
        // the set; on0 has it off.
        const VisState on1 = { 1, 1, 1, 1, 1, 1, 1, 6144, 1 };
        CHECK(vis_sweep(sw, sh, on1, false, &multi) == (parents | subs | VF_SECRETVOLUME | resets));
        CHECK(!multi);
        const VisState on0 = { 1, 0, 0, 0, 0, 0, 0, 6144, 0 };
        CHECK(vis_sweep(sw, sh, on0, false, &multi) == (parents | subs | resets));
        CHECK(!multi);

        // The slider reads the held button, not the press edge: a sweep with `pressed` false isolates it and proves a
        // switch reads the edge.
        const VisState sl = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        CHECK(vis_sweep(sw, sh, sl, true, &multi) == VF_DRAWDIST);
        CHECK(!multi);

        // The other direction for "Secret volume": starting on, so a row that could only be switched on fails here (the
        // off cases above already assert it is inert while its parent is off).
        const VisState vol = { 1, 1, 1, 1, 1, 1, 1, 6144, 1, 1, 1, 1, 1, 1 };
        CHECK(vis_sweep(sw, sh, vol, false, &multi) == (parents | subs | VF_SECRETVOLUME | resets));
        CHECK(!multi);
      }

      {
        bool sawMin = false, sawMax = false, sawMiddle = false, allLegal = true;
        const UiRect p = menu_panel_rect(3440, 1440);
        const VisState st = { 1, 1, 1, 1, 1, 1, 1, 6144 };
        for (int y = p.y; y < p.y + p.h; y++)
          for (int x = p.x; x < p.x + p.w; x++) {
            vis_restore(st);
            menu_set_tab(TAB_VISUALS);
            g_ui.mx = x; g_ui.my = y; g_ui.down = true; g_ui.pressed = false;
            menu_draw(3440, 1440);
            const int v = g_cfg.espdrawdist;
            if (v != esp_draw_distance_clamped(v)) allLegal = false;
            if (v == ESP_DRAWDIST_MIN) sawMin = true;
            else if (v == ESP_DRAWDIST_MAX) sawMax = true;
            else if (v != 6144) sawMiddle = true;
          }
        g_ui.down = false; g_ui.pressed = false;
        CHECK(sawMin);
        CHECK(sawMax);
        CHECK(sawMiddle);
        CHECK(allLegal);
      }

      // A control on another tab must not be driven by a click on this
      // one: the FOV switch/slider share the panel and nothing here may touch either.
      {
        bool fovMoved = false;
        const UiRect p = menu_panel_rect(3440, 1440);
        for (int y = p.y; y < p.y + p.h; y++)
          for (int x = p.x; x < p.x + p.w; x++) {
            g_cfg.fovon = 0; g_cfg.fov = 90;
            menu_set_tab(TAB_VISUALS);
            g_ui.mx = x; g_ui.my = y; g_ui.down = true; g_ui.pressed = true;
            menu_draw(3440, 1440);
            if (g_cfg.fovon != 0 || g_cfg.fov != 90) fovMoved = true;
            // THE PANEL IS PUT DOWN BETWEEN PIXELS: this sweep holds the button DOWN and raises a fresh press edge at
            // every pixel, which a mouse cannot do. Two panel-wide latches span a hold (a reset button's spent press,
            // the move a press no control took arms) and menu_keycap_close() puts both down.
            menu_keycap_close();
          }
        g_ui.down = false; g_ui.pressed = false;
        CHECK(!fovMoved);
      }

      // Toggling a row marks the config dirty, or nothing would be written back or persist across a restart.
      {
        const VisState st = { 0, 1, 1, 1, 1, 0, 0, 6144 };
        bool sawDirty = false;
        const UiRect p = menu_panel_rect(3440, 1440);
        for (int y = p.y; y < p.y + p.h && !sawDirty; y++)
          for (int x = p.x; x < p.x + p.w && !sawDirty; x++) {
            vis_restore(st);
            g_cfgDirty = false;
            menu_set_tab(TAB_VISUALS);
            g_ui.mx = x; g_ui.my = y; g_ui.down = false; g_ui.pressed = true;
            menu_draw(3440, 1440);
            if (g_cfg.espenemies != st.enemies && g_cfgDirty) sawDirty = true;
          }
        g_ui.pressed = false;
        CHECK(sawDirty);
      }

      g_ui.mx = 0; g_ui.my = 0; g_ui.down = false; g_ui.pressed = false;
      g_cfgDirty = false;
      g_cfg.fovon = savedFovOn; g_cfg.fov = savedFov;
      vis_restore(saved);
      menu_set_tab(savedTab);
    }
  }




// Pins the per-screen scale ceiling (must not clip its own control) and
// that the applied scale freezes while the button is held.
void tb_0075_phase_5b_task_5_fix() {
    // Per-screen ceiling: the panel is PANEL_H (900) logical pixels tall inside a PANEL_VINSET (30) margin, so 960
    // logical pixels must fit the device screen; a higher scale shrinks the logical screen. The raw cap snaps down to a
    // multiple of five.
    CHECK(ui_scale_max_for(1440, 900, 30) == 150);   // (1440 * 100 + 49) / 960 = 150
    CHECK(ui_scale_max_for(2160, 900, 30) == UI_SCALE_MAX);
    CHECK(ui_scale_max_for(1080, 900, 30) == 110);   // raw 112, snapped down
    CHECK(ui_scale_max_for(768,  900, 30) == 80);    // raw 80, already round
    // Snap is downward, never nearest: 1470 raws to 153, snaps to 150
    // (would round to 155, which clips).
    CHECK(ui_scale_max_for(1000, 900, 30) == 100);   // raw 104
    CHECK(ui_scale_max_for(1447, 900, 30) == 150);   // raw 150, just under the next step
    CHECK(ui_scale_max_for(1470, 900, 30) == 150);   // raw 153; nearest would give 155
    CHECK(ui_scale_px(960, 155) > 1470);             // and 155 really does not fit
    // Never outside the legal range: the screen size comes from the engine
    // every frame and nothing here may trust it.
    CHECK(ui_scale_max_for(0, 900, 30) == UI_SCALE_MIN);
    CHECK(ui_scale_max_for(-1000, 900, 30) == UI_SCALE_MIN);
    CHECK(ui_scale_max_for(2147483647, 900, 30) == UI_SCALE_MAX);
    // Closed form pinned against the predicate it stands for ("largest
    // multiple of five in 75..200 whose scaled panel still fits"), swept
    // at every screen height, not just the sizes above.
    for (int h = 200; h <= 4400; h += 7) {
      const int m = ui_scale_max_for(h, 900, 30);
      CHECK(m >= UI_SCALE_MIN && m <= UI_SCALE_MAX);
      CHECK(m % 5 == 0);                                            // always a round number
      if (m > UI_SCALE_MIN) CHECK(ui_scale_px(960, m) <= h);        // it fits
      if (m < UI_SCALE_MAX) CHECK(ui_scale_px(960, m + 5) > h);     // the next step does not
    }
    CHECK(ui_scale_max_for(1440, 0, 0) == UI_SCALE_MAX);
    CHECK(ui_scale_max_for(1440, -100, 0) == UI_SCALE_MAX);

    // Applied scale frozen while the button is held: row_slider_at has no drag capture (it re-tests its grab box every
    // frame), so the layout's value is held for the press and the new one applies on release.
    CHECK(ui_scale_effective(150, 100, false) == 150);   // button up: the settled value
    CHECK(ui_scale_effective(150, 100, true)  == 100);   // held: whatever was already applied
    CHECK(ui_scale_effective(75, 200, true)   == 200);
    CHECK(ui_scale_effective(75, 200, false)  == 75);
    CHECK(ui_scale_effective(100, 100, true)  == 100);
    CHECK(ui_scale_effective(100, 100, false) == 100);
  }

// Every CanvasOp::TEXT op in the record is under the recorder's 64-byte cap. A guard rail, not the guard: the recorder
// truncates (strncpy_s _TRUNCATE, canvas.cpp), so this cannot fail; canvas_record_find_text's full-string match catches
// an overgrown hint.
static void check_recorded_text_lengths() {
  const int n = canvas_record_count();
  for (int i = 0; i < n; i++) {
    const CanvasOp* op = canvas_record_op(i);
    if (op && op->kind == CANVAS_OP_TEXT) {
      CHECK(strlen(op->text) < 64);
    }
  }
}

// Three View-tab camera sub-rows and Visuals' "Fullbright" row: each moves exactly its own key, is inert while its
// parent is off, and stays under the 64-byte hint cap.
void tb_0081_phase_6_rows() {
  const int savedTab = menu_active_tab();
  const int savedOn = g_cfg.freecamon;
  const int savedFog = g_cfg.freecamfog, savedFb = g_cfg.freecamfullbright,
            savedShot = g_cfg.freecamshot, savedFull = g_cfg.fullbright,
            savedScale = g_cfg.uiscale;
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

  menu_set_tab(TAB_VIEW);
  g_cfg.freecamon = 1;
  g_cfg.freecamfog = 0; g_cfg.freecamfullbright = 0; g_cfg.freecamshot = 0;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  const CanvasOp* rFog  = canvas_record_find_text("Remove fog");
  const CanvasOp* rFb   = canvas_record_find_text("Fullbright");
  const CanvasOp* rShot = canvas_record_find_text("Screenshot mode");
  CHECK(rFog && rFb && rShot);

  const UiRect panel = menu_panel_rect(3440, 1440);

  if (rFog) {
    g_cfg.freecamfog = 0; g_cfg.freecamfullbright = 0; g_cfg.freecamshot = 0;
    bool flipped = false;
    for (int x = panel.x; x < panel.x + panel.w && !flipped; x++) {
      g_ui.mx = x; g_ui.my = rFog->y; g_ui.down = false; g_ui.pressed = true;
      menu_draw(3440, 1440);
      if (g_cfg.freecamfog != 0) flipped = true;
    }
    CHECK(flipped);
    CHECK(g_cfg.freecamfullbright == 0 && g_cfg.freecamshot == 0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.freecamfog = 0;
  }
  if (rFb) {
    g_cfg.freecamfog = 0; g_cfg.freecamfullbright = 0; g_cfg.freecamshot = 0;
    bool flipped = false;
    for (int x = panel.x; x < panel.x + panel.w && !flipped; x++) {
      g_ui.mx = x; g_ui.my = rFb->y; g_ui.down = false; g_ui.pressed = true;
      menu_draw(3440, 1440);
      if (g_cfg.freecamfullbright != 0) flipped = true;
    }
    CHECK(flipped);
    CHECK(g_cfg.freecamfog == 0 && g_cfg.freecamshot == 0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.freecamfullbright = 0;
  }
  if (rShot) {
    g_cfg.freecamfog = 0; g_cfg.freecamfullbright = 0; g_cfg.freecamshot = 0;
    bool flipped = false;
    for (int x = panel.x; x < panel.x + panel.w && !flipped; x++) {
      g_ui.mx = x; g_ui.my = rShot->y; g_ui.down = false; g_ui.pressed = true;
      menu_draw(3440, 1440);
      if (g_cfg.freecamshot != 0) flipped = true;
    }
    CHECK(flipped);
    CHECK(g_cfg.freecamfog == 0 && g_cfg.freecamfullbright == 0);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
    g_cfg.freecamshot = 0;
  }

  g_cfg.freecamon = 0;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  const CanvasOp* rFog2  = canvas_record_find_text("Remove fog");
  const CanvasOp* rFb2   = canvas_record_find_text("Fullbright");
  const CanvasOp* rShot2 = canvas_record_find_text("Screenshot mode");
  CHECK(rFog2 && rFb2 && rShot2);
  if (rFog2 && rFb2 && rShot2) {
    g_cfg.freecamfog = 0; g_cfg.freecamfullbright = 0; g_cfg.freecamshot = 0;
    for (int x = panel.x; x < panel.x + panel.w; x++) {
      g_ui.mx = x; g_ui.my = rFog2->y;  g_ui.down = false; g_ui.pressed = true; menu_draw(3440, 1440);
      g_ui.mx = x; g_ui.my = rFb2->y;   g_ui.down = false; g_ui.pressed = true; menu_draw(3440, 1440);
      g_ui.mx = x; g_ui.my = rShot2->y; g_ui.down = false; g_ui.pressed = true; menu_draw(3440, 1440);
    }
    CHECK(g_cfg.freecamfog == 0 && g_cfg.freecamfullbright == 0 && g_cfg.freecamshot == 0);
  }
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

  g_cfg.freecamon = 1;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  // A hint is a tooltip now, so it is read off the row that owns it; after check_recorded_text_lengths() since
  // tooltip_for_row takes the recording over.
  check_recorded_text_lengths();
  CHECK(streq(tooltip_for_row("Remove fog", 3440, 1440),
              "Distance fog off while flying."));

  menu_set_tab(TAB_VISUALS);
  VisState vstart = vis_snapshot();
  vstart.fullbright = 0;
  vis_restore(vstart);
  {
    bool multi = false;
    const int mask = vis_sweep(3440, 1440, vstart, false, &multi);
    CHECK((mask & VF_FULLBRIGHT) != 0);
    CHECK(!multi);
  }
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  const CanvasOp* rFull = canvas_record_find_text("Fullbright");
  CHECK(rFull);
  if (rFull) {
    const int m = vis_row_click_mask(rFull->y, 3440, 1440, vstart);
    CHECK(m == VF_FULLBRIGHT);
  }
  check_recorded_text_lengths();
  CHECK(streq(tooltip_for_row("Fullbright", 3440, 1440),
              "No shading or fog. Textures reload once."));

  // The Fullbright rows' "(Unavailable)" suffix off the light session latch, as "Disable culling" does for nocull's
  // (test_freecam.cpp): label and layout only, on the View sub-row and the Visuals row.
  {
    light_reset_available();

    menu_set_tab(TAB_VIEW);
    g_cfg.freecamon = 1;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright") != nullptr);
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") == nullptr);

    light_mark_unavailable();
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") != nullptr);
    {
      int worst = 0;
      CHECK(tab_text_fits(TAB_VIEW, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_VIEW, 1024, 768, 18, &worst));
    }

    light_reset_available();
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright") != nullptr);
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") == nullptr);

    menu_set_tab(TAB_VISUALS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright") != nullptr);
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") == nullptr);

    light_mark_unavailable();
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") != nullptr);
    {
      int worst = 0;
      CHECK(tab_text_fits(TAB_VISUALS, 3440, 1440, 18, &worst));
      CHECK(tab_text_fits(TAB_VISUALS, 1024, 768, 18, &worst));
    }

    // Reset restores the plain label (the suite's only way back, light.h).
    light_reset_available();
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Fullbright") != nullptr);
    CHECK(canvas_record_find_text("Fullbright (Unavailable)") == nullptr);
  }

  // Both tabs' bottom rows still fit at the ceiling: forcing the stored scale above UI_SCALE_MAX makes
  // menu_applied_scale fall back to the screen's own cap (150 at 3440x1440, 80 at 1024x768). Hints are tooltips, so the
  // View tab's last row is the "Screenshot mode" switch.
  g_cfg.uiscale = UI_SCALE_MAX;
  CHECK(tab_bottom_row_fits(TAB_VIEW, "Screenshot mode", 3440, 1440));
  CHECK(tab_bottom_row_fits(TAB_VIEW, "Screenshot mode", 1024, 768));
  CHECK(tab_bottom_row_fits(TAB_VISUALS, "Fullbright", 3440, 1440));
  CHECK(tab_bottom_row_fits(TAB_VISUALS, "Fullbright", 1024, 768));

  g_cfg.freecamon = savedOn;
  g_cfg.freecamfog = savedFog; g_cfg.freecamfullbright = savedFb;
  g_cfg.freecamshot = savedShot; g_cfg.fullbright = savedFull;
  g_cfg.uiscale = savedScale;
  menu_set_tab(savedTab);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
}

// Weapons tab's MOUSE AIM group: the switch moves only its own field, both sliders reach both ends, sensitivity reads
// three decimals with no unit, no click moves two fields, the bottom row fits at the ceiling scale.
void tb_0086_phase_7_rows() {
  const int savedTab = menu_active_tab();
  const WeapState savedWeap = weap_snapshot();
  const int savedScale = g_cfg.uiscale;
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

  menu_set_tab(TAB_WEAPONS);
  g_cfg.mouseaimon = 0;
  g_cfg.mouseaimsens = MOUSEAIM_SENS_DEFAULT;
  g_cfg.mouseaimvscale = MOUSEAIM_VSCALE_DEFAULT;

  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  const CanvasOp* rSwitch = canvas_record_find_text("Modern mouse aim");
  const CanvasOp* rSens   = canvas_record_find_text("Sensitivity");
  const CanvasOp* rVscale = canvas_record_find_text("Vertical scale");
  CHECK(rSwitch && rSens && rVscale);

  if (rSwitch) {
    const WeapState start = weap_snapshot();
    const int mask = weap_row_click_mask(rSwitch->y, 3440, 1440, start);
    CHECK(mask == WF_MOUSEAIMON);
  }

  g_cfg.mouseaimon = 1;
  CHECK(slider_row_reaches_ends(TAB_WEAPONS, "Sensitivity", 3440, 1440,
                                MOUSEAIM_SENS_MIN, MOUSEAIM_SENS_MAX,
                                &g_cfg.mouseaimsens, MOUSEAIM_SENS_DEFAULT));
  CHECK(slider_row_reaches_ends(TAB_WEAPONS, "Vertical scale", 3440, 1440,
                                MOUSEAIM_VSCALE_MIN, MOUSEAIM_VSCALE_MAX,
                                &g_cfg.mouseaimvscale, MOUSEAIM_VSCALE_DEFAULT));

  // Sensitivity readout: three decimal places, no unit suffix.
  g_cfg.mouseaimsens = MOUSEAIM_SENS_DEFAULT;   // 40 -> "0.040"
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  {
    const CanvasOp* lbl = canvas_record_find_text("Sensitivity");
    CHECK(lbl);
    if (lbl) {
      const CanvasOp* readout = find_readout_at_y(lbl->y);
      CHECK(readout && streq(readout->text, "0.040"));
    }
  }
  g_cfg.mouseaimsens = MOUSEAIM_SENS_MAX;   // 200 -> "0.200"
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  {
    const CanvasOp* lbl = canvas_record_find_text("Sensitivity");
    CHECK(lbl);
    if (lbl) {
      const CanvasOp* readout = find_readout_at_y(lbl->y);
      CHECK(readout && streq(readout->text, "0.200"));
    }
  }
  g_cfg.mouseaimsens = MOUSEAIM_SENS_DEFAULT;

  // No click on the tab moves two fields, switches and sliders alike.
  {
    WeapState start = weap_snapshot();
    start.pistol = start.chaingun = start.shotgun = start.expander = 0;
    start.mouseaimon = 1;   // so the sliders are live for the held sweep
    start.mouseaimsens = MOUSEAIM_SENS_DEFAULT;
    start.mouseaimvscale = MOUSEAIM_VSCALE_DEFAULT;
    bool multiPress = false, multiHeld = false;
    weap_sweep(3440, 1440, start, false, &multiPress);
    weap_sweep(3440, 1440, start, true, &multiHeld);
    CHECK(!multiPress);
    CHECK(!multiHeld);
  }

  // Hint is short and belongs to the parent switch: a tooltip on "Modern
  // mouse aim", not a row under "Vertical scale".
  CHECK(strlen("Raw input, equal axes, no dead zone.") < 63);
  CHECK(streq(tooltip_for_row("Modern mouse aim", 3440, 1440),
              "Raw input, equal axes, no dead zone."));

  // MOUSE AIM group's own last row ("Vertical scale") still fits at the
  // ceiling; the tab's true bottom row is pinned separately below.
  g_cfg.uiscale = UI_SCALE_MAX;
  CHECK(tab_bottom_row_fits(TAB_WEAPONS, "Vertical scale", 3440, 1440));
  CHECK(tab_bottom_row_fits(TAB_WEAPONS, "Vertical scale", 1024, 768));

  // The group is WEAPON SPEED, with a second slider beside Fire rate; "FIRE RATE" appears nowhere. uiscale goes back to
  // the saved value first: the block above left it at UI_SCALE_MAX, where a row's y differs from menu_panel_rect's.
  g_cfg.uiscale = savedScale;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  CHECK(canvas_record_find_text("WEAPON SPEED") != nullptr);
  CHECK(canvas_record_find_text("FIRE RATE") == nullptr);
  CHECK(canvas_record_find_text("Weapon switch speed") != nullptr);
  {
    const int savedSwitch = g_cfg.switchspeed;
    CHECK(slider_row_reaches_ends(TAB_WEAPONS, "Weapon switch speed", 3440, 1440,
                                  FIRERATE_MIN, FIRERATE_MAX,
                                  &g_cfg.switchspeed, FIRERATE_MIN));
    g_cfg.switchspeed = savedSwitch;
  }
  CHECK(strlen("Faster lowering and raising when you switch") < 63);
  // Copy is read off the slider it belongs to (a tooltip), and it is also
  // the tab's bottom row now.
  CHECK(streq(tooltip_for_row("Weapon switch speed", 3440, 1440),
              "Faster lowering and raising when you switch"));
  g_cfg.uiscale = UI_SCALE_MAX;
  CHECK(tab_bottom_row_fits(TAB_WEAPONS, "Weapon switch speed", 3440, 1440));
  CHECK(tab_bottom_row_fits(TAB_WEAPONS, "Weapon switch speed", 1024, 768));

  weap_restore(savedWeap);
  g_cfg.uiscale = savedScale;
  menu_set_tab(savedTab);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
}

// TRIGGERBOT group on the Combat tab: its own switch, "Fire when" named
// readout, "Delay" slider, gated like the AIMBOT/SILENT AIM sub-rows
// (combat_sub_enabled, menu.h).
void tb_0109_triggerbot_rows() {
  CHECK(streq(tbot_when_label(TBOT_WHEN_CROSSHAIR), "Crosshair on enemy"));
  CHECK(streq(tbot_when_label(TBOT_WHEN_AIMBOT), "Aimbot locked"));
  CHECK(streq(tbot_when_label(9), "Crosshair on enemy"));      // out of range reads as the default

  const int savedScale = g_cfg.uiscale;
  const int savedOn = g_cfg.tboton, savedWhen = g_cfg.tbotwhen, savedDelay = g_cfg.tbotdelay;
  const int savedTab = menu_active_tab();

  menu_set_tab(TAB_COMBAT);
  g_cfg.uiscale = UI_SCALE_MAX;
  g_cfg.tboton = 0;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  CHECK(canvas_record_find_text("TRIGGERBOT") != nullptr);
  CHECK(canvas_record_find_text("Triggerbot") != nullptr);
  CHECK(canvas_record_find_text("Fire when") != nullptr);
  CHECK(canvas_record_find_text("Delay") != nullptr);
  {
    const CanvasOp* w = canvas_record_find_text("Fire when");
    const CanvasOp* d = canvas_record_find_text("Delay");
    CHECK(w && d);
    if (w && d) {
      CHECK(w->colour == C_BONEFNT);
      CHECK(d->colour == C_BONEFNT);
    }
  }

  g_cfg.tboton = 1;
  // slider_row_reaches_ends measures via menu_panel_rect at the screen's own dimensions, valid only at applied scale
  // 100 (logicalW/logicalH collapse to screenW/screenH), so scale drops to default around this call.
  g_cfg.uiscale = UI_SCALE_DEFAULT;
  CHECK(slider_row_reaches_ends(TAB_COMBAT, "Delay", 3440, 1440,
                                TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, &g_cfg.tbotdelay, 0));
  g_cfg.uiscale = UI_SCALE_MAX;

  CHECK(strlen("Fires by itself while the condition below holds") < 63);
  // Hint hangs off the Triggerbot switch (one hint for the group); the
  // Combat tab's bottom row is "Max redirect", SILENT AIM's last row.
  CHECK(streq(tooltip_for_row("Triggerbot", 3440, 1440),
              "Fires by itself while the condition below holds"));
  CHECK(tab_bottom_row_fits(TAB_COMBAT, "Max redirect", 3440, 1440));

  g_cfg.tboton = savedOn; g_cfg.tbotwhen = savedWhen; g_cfg.tbotdelay = savedDelay;
  g_cfg.uiscale = savedScale;
  menu_set_tab(savedTab);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
}

void tb_0116_ui_polish_rank9() {
  // anim_switch_apply (anim.h): off writes ANIM_OFF; on keeps a stored non-off style (a stored Glass survives a click
  // meaning "still on"), else writes ANIM_SIMPLE. All six (stored, on) pairs, plus an odd `on` counting as on.
  CHECK(anim_switch_apply(ANIM_OFF,    1) == ANIM_SIMPLE);
  CHECK(anim_switch_apply(ANIM_SIMPLE, 1) == ANIM_SIMPLE);
  CHECK(anim_switch_apply(ANIM_GLASS,  1) == ANIM_GLASS);
  CHECK(anim_switch_apply(ANIM_OFF,    0) == ANIM_OFF);
  CHECK(anim_switch_apply(ANIM_SIMPLE, 0) == ANIM_OFF);
  CHECK(anim_switch_apply(ANIM_GLASS,  0) == ANIM_OFF);
  CHECK(anim_switch_apply(ANIM_GLASS,  7) == ANIM_GLASS);   // an odd `on` still counts as on

  // "Draw distance" reads in metres, same division as "Aim range"
  // (BUILD_UNITS_PER_METRE): ESP_DRAWDIST_MIN/MAX divide down to 1 and 256.
  CHECK(ESP_DRAWDIST_MIN / BUILD_UNITS_PER_METRE == 1);
  CHECK(ESP_DRAWDIST_MAX / BUILD_UNITS_PER_METRE == 256);

  // Three suffix drops: "Field of view", "Cone" and "Max redirect" all
  // draw a bare number, no trailing "d".
  {
    const int savedTab = menu_active_tab();
    const int savedFovOn = g_cfg.fovon, savedFov = g_cfg.fov;
    const int savedAimOn = g_cfg.aimon, savedSilentOn = g_cfg.silenton;
    const int savedCone = g_cfg.aimcone, savedMaxRedirect = g_cfg.silentmaxangle;
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

    menu_set_tab(TAB_VIEW);
    g_cfg.fovon = 1;
    g_cfg.fov = 90;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* lbl = canvas_record_find_text("Field of view");
      CHECK(lbl);
      if (lbl) {
        const CanvasOp* readout = find_readout_at_y(lbl->y);
        CHECK(readout && streq(readout->text, "90"));
      }
    }

    menu_set_tab(TAB_COMBAT);
    g_cfg.aimon = 1;
    g_cfg.silenton = 0;
    g_cfg.aimcone = 45;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* lbl = canvas_record_find_text("Cone");
      CHECK(lbl);
      if (lbl) {
        const CanvasOp* readout = find_readout_at_y(lbl->y);
        CHECK(readout && streq(readout->text, "45"));
      }
    }

    g_cfg.silenton = 1;
    g_cfg.silentmaxangle = 90;
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* lbl = canvas_record_find_text("Max redirect");
      CHECK(lbl);
      if (lbl) {
        const CanvasOp* readout = find_readout_at_y(lbl->y);
        CHECK(readout && streq(readout->text, "90"));
      }
    }

    g_cfg.fovon = savedFovOn; g_cfg.fov = savedFov;
    g_cfg.aimon = savedAimOn; g_cfg.silenton = savedSilentOn;
    g_cfg.aimcone = savedCone; g_cfg.silentmaxangle = savedMaxRedirect;
    menu_set_tab(savedTab);
    g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  }
}


// HOTKEYS group: the four bindings on the Settings tab, each a well with a rim reading "Click to set" on hover. Pins
// layout, field and text rule; the capture state machine (keycap_step, bind.h) is swept by tb_0064.
void tb_0117_hotkeys_group() {
  const int savedTab = menu_active_tab();
  const int savedMenu = g_cfg.menukey, savedAim = g_cfg.aimbind;
  const int savedCam = g_cfg.freecamkey, savedTp = g_cfg.freecamteleportkey;
  const int savedOn = g_cfg.freecamon, savedAimOn = g_cfg.aimon;
  const int savedAnim = g_cfg.menuanim;
  const int savedScale = g_cfg.uiscale;
  // Blocks below feed a recorder x (device space) straight into g_ui.mx,
  // which ui_hit reads in logical space -- sound only at applied scale 100.
  g_cfg.uiscale = UI_SCALE_DEFAULT;
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_keycap_cancel();
  menu_gamecfg_load(0);
  // Off, stated rather than inherited: colour checks below read exact
  // colour words, and the other two styles scale them by open alpha.
  g_cfg.menuanim = ANIM_OFF;

  // BIND_LABEL_WIDEST really is the widest name bind_label has (the field is sized from it once per draw): swept over
  // every byte plus the five mouse codes.
  {
    const size_t widest = strlen(BIND_LABEL_WIDEST);
    CHECK(streq(BIND_LABEL_WIDEST, "Page down"));
    CHECK(streq(bind_label(0x22), BIND_LABEL_WIDEST));   // a name it really returns
    bool allFit = true;
    for (int vk = 0; vk <= 255; vk++)
      if (strlen(bind_label(vk)) > widest) allFit = false;
    const int mouse[5] = { VK_BIND_LBUTTON, VK_BIND_RBUTTON, VK_BIND_MBUTTON,
                           VK_BIND_XBUTTON1, VK_BIND_XBUTTON2 };
    for (int i = 0; i < 5; i++)
      if (strlen(bind_label(mouse[i])) > widest) allFit = false;
    CHECK(allFit);
  }

  // bind_field_text: armed beats hovered, hovered beats the name. All
  // four (armed, hovered) combinations, for a keyboard key, mouse button, unbound 0.
  {
    CHECK(streq(MENU_KEYCAP_HOVER, "Click to set"));
    const int codes[3] = { 0x2D, VK_BIND_XBUTTON2, 0 };
    const char* names[3] = { "Insert", "Mouse 5", "Unbound" };
    for (int i = 0; i < 3; i++) {
      CHECK(streq(bind_field_text(true, true, codes[i], MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER),
                  MENU_KEYCAP_PROMPT));
      CHECK(streq(bind_field_text(true, false, codes[i], MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER),
                  MENU_KEYCAP_PROMPT));
      CHECK(streq(bind_field_text(false, true, codes[i], MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER),
                  MENU_KEYCAP_HOVER));
      CHECK(streq(bind_field_text(false, false, codes[i], MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER),
                  names[i]));
    }
  }

  // EVERY FEATURE OFF, a fresh install's state: all four rows still draw at full strength and take a click; a hotkey
  // can be set whether or not the thing it drives is on.
  g_cfg.menukey = 0x2D;                  // Insert
  g_cfg.freecamkey = 0x56;               // V
  g_cfg.freecamteleportkey = 0x54;       // T
  g_cfg.aimbind = VK_BIND_XBUTTON2;      // Mouse 5
  g_cfg.freecamon = 0; g_cfg.aimon = 0;
  menu_set_tab(TAB_SETTINGS);
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  {
    const CanvasOp* gHot   = canvas_record_find_text("HOTKEYS");
    const CanvasOp* rPanel = canvas_record_find_text("Panel key");
    const CanvasOp* rCam   = canvas_record_find_text("Free camera key");
    const CanvasOp* rTele  = canvas_record_find_text("Teleport key");
    const CanvasOp* rAim   = canvas_record_find_text("Aimbot button");
    const CanvasOp* rScale = canvas_record_find_text("UI scale");
    CHECK(gHot && rPanel && rCam && rTele && rAim && rScale);
    if (gHot && rPanel && rCam && rTele && rAim && rScale) {
      CHECK(gHot->colour == C_BONEFNT);
      CHECK(gHot->font == FONT_SMALL);
      CHECK(gHot->y < rPanel->y);
      CHECK(rPanel->y < rCam->y);
      CHECK(rCam->y   < rTele->y);
      CHECK(rTele->y  < rAim->y);
      CHECK(rAim->y   < rScale->y);
      // Top-level rows: no sub-row indent, full-strength bone even with
      // every feature off.
      CHECK(rPanel->x == rScale->x);
      CHECK(rCam->x   == rScale->x);
      CHECK(rTele->x  == rScale->x);
      CHECK(rAim->x   == rScale->x);
      CHECK(rPanel->colour == C_BONE);
      CHECK(rCam->colour   == C_BONE);
      CHECK(rTele->colour  == C_BONE);
      CHECK(rAim->colour   == C_BONE);
    }
  }
  // With the cursor off the panel every field shows its own key's name, and
  // neither the prompt nor the invitation is anywhere on screen.
  CHECK(canvas_record_find_text("Insert") != nullptr);
  CHECK(canvas_record_find_text("V") != nullptr);
  CHECK(canvas_record_find_text("T") != nullptr);
  CHECK(canvas_record_find_text("Mouse 5") != nullptr);
  CHECK(canvas_record_find_text(MENU_KEYCAP_HOVER) == nullptr);
  CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) == nullptr);
  {
    int wells = 0, rims = 0;
    for (int i = 0; i < canvas_record_count(); i++) {
      const CanvasOp* o = canvas_record_op(i);
      if (!o) continue;
      if (o->kind == CANVAS_OP_ROUNDRECT    && o->colour == C_WELL)   wells++;
      if (o->kind == CANVAS_OP_ROUNDOUTLINE && o->colour == C_RAISED) rims++;
    }
    CHECK(wells == BIND_ROW_COUNT);
    CHECK(rims  == BIND_ROW_COUNT);
  }

  // Rows are on Settings and nowhere else, swept over every tab so a row
  // put back on a third tab is caught too.
  {
    for (int t = 0; t < TAB_COUNT; t++) {
      menu_set_tab(t);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      CHECK(canvas_record_find_text("Toggle key") == nullptr);
      CHECK(canvas_record_find_text("Button") == nullptr);
      const bool onSettings = (t == TAB_SETTINGS);
      CHECK((canvas_record_find_text("Panel key")       != nullptr) == onSettings);
      CHECK((canvas_record_find_text("Free camera key") != nullptr) == onSettings);
      CHECK((canvas_record_find_text("Teleport key")    != nullptr) == onSettings);
      CHECK((canvas_record_find_text("Aimbot button")   != nullptr) == onSettings);
    }
    // The tabs they left still draw other rows, so "not found" above
    // cannot pass because the tab drew nothing at all.
    menu_set_tab(TAB_VIEW);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Free camera") != nullptr);
    menu_set_tab(TAB_COMBAT);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text("Hold key") != nullptr);
  }

  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  {
    // Cursor placed on the panel key's field, found through the recorder: X from the readout, Y from the row's own
    // label (the readout's raw Y may fall outside the hit rect once its ink band is centred).
    const CanvasOp* ins = canvas_record_find_text("Insert");
    const CanvasOp* lbl = canvas_record_find_text("Panel key");
    CHECK(ins && lbl);
    if (ins && lbl) {
      g_ui.mx = ins->x + ins->w / 2; g_ui.my = lbl->y;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      int hovers = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_KEYCAP_HOVER)) hovers++;
      }
      CHECK(hovers == 1);                                    // one field, not all four
      CHECK(canvas_record_find_text("Insert") == nullptr);    // its name gave way to it
      CHECK(canvas_record_find_text("V") != nullptr);         // the other three untouched
      CHECK(canvas_record_find_text("T") != nullptr);
      CHECK(canvas_record_find_text("Mouse 5") != nullptr);
      CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) == nullptr);   // hovered is not armed
      // Nothing runs off the panel's right edge with the invitation
      // showing (the widest string a field can draw); tab_text_fits parks
      // the cursor off the panel and cannot see it otherwise.
      {
        const UiRect p = menu_panel_rect(3440, 1440);
        bool fits = true;
        for (int i = 0; i < canvas_record_count(); i++) {
          const CanvasOp* o = canvas_record_op(i);
          if (!o || o->kind != CANVAS_OP_TEXT) continue;
          if (o->x + o->w > p.x + p.w - 18) fits = false;
        }
        CHECK(fits);
      }
      // A click on the row arms it; armed beats hovered even with the
      // cursor unmoved.
      g_ui.pressed = true;
      menu_draw(3440, 1440);
      g_ui.pressed = false;
      CHECK(menu_keycap_armed());
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(canvas_record_find_text(MENU_KEYCAP_PROMPT) != nullptr);
      CHECK(canvas_record_find_text(MENU_KEYCAP_HOVER) == nullptr);
      CHECK(canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD) != nullptr);
      // Opening a numeric field takes the armed capture down: input.cpp's routing asks the field first, so a capture
      // left armed behind an open field could never bind a digit/sign/Backspace/Enter/Escape.
      menu_entry_begin_edit(0, EK_INT, 0, 10, "5");
      CHECK(!menu_keycap_armed());
      CHECK(menu_entry_open());
      CHECK(g_cfg.menukey == 0x2D);         // and the row's binding is untouched
      menu_entry_close();
      CHECK(!menu_entry_open());
      menu_keycap_cancel();
      CHECK(g_cfg.menukey == 0x2D);                          // cancelling kept the binding
    }
  }
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;

  {
    g_cfg.freecamon = 0; g_cfg.aimon = 0;
    const char* labels[BIND_ROW_COUNT] = { "Panel key", "Free camera key",
                                           "Teleport key", "Aimbot button" };
    const UiRect p = menu_panel_rect(3440, 1440);
    const int contentX = p.x + 1 + RAIL_W;   // past the rail, which would switch tabs
    for (int i = 0; i < BIND_ROW_COUNT; i++) {
      menu_keycap_cancel();
      menu_set_tab(TAB_SETTINGS);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      const CanvasOp* row = canvas_record_find_text(labels[i]);
      CHECK(row);
      if (!row) continue;
      bool armedSomewhere = false;
      for (int x = contentX; x < p.x + p.w && !armedSomewhere; x++) {
        menu_keycap_cancel();
        menu_set_tab(TAB_SETTINGS);
        g_ui.mx = x; g_ui.my = row->y; g_ui.down = false; g_ui.pressed = true;
        menu_draw(3440, 1440);
        if (menu_keycap_armed()) armedSomewhere = true;
      }
      CHECK(armedSomewhere);
      g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
      menu_keycap_cancel();
    }
  }
  g_cfgDirty = false;

  {
    const char* t = "test_gamecfg_rank10.cfg";
    FILE* fc = nullptr;
    CHECK(fopen_s(&fc, t, "w") == 0 && fc);
    if (fc) {
      fputs("[KeyDefinitions]\nSave_Menu = \"F2\" \"None\"\n", fc);
      fclose(fc);
    }
    menu_gamecfg_load(t);
    g_cfg.freecamkey = 0x71;                 // F2, which that cfg binds
    g_cfg.freecamteleportkey = 0x54;         // T, which it does not
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      const CanvasOp* note = canvas_record_find_text(MENU_GAMECFG_NOTE);
      const CanvasOp* cam  = canvas_record_find_text("Free camera key");
      const CanvasOp* tele = canvas_record_find_text("Teleport key");
      CHECK(note && cam && tele);
      if (note && cam && tele) CHECK(cam->y < note->y && note->y < tele->y);
      int notes = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_GAMECFG_NOTE)) notes++;
      }
      CHECK(notes == 1);                     // one note, under one row
    }
    g_cfg.freecamkey = 0x56;                 // V
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    CHECK(canvas_record_find_text(MENU_GAMECFG_NOTE) == nullptr);

    // Worst case: all four rows bound to keys the cfg also binds, costing
    // four extra rows; the tab's last line must survive at smallest and largest panel.
    fc = nullptr;
    CHECK(fopen_s(&fc, t, "w") == 0 && fc);
    if (fc) {
      fputs("[KeyDefinitions]\n", fc);
      fputs("Map = \"Insert\" \"None\"\n", fc);
      fputs("Save_Menu = \"F2\" \"None\"\n", fc);
      fputs("Jump = \"T\" \"None\"\n", fc);
      fputs("Fire = \"MOUSE 5\" \"None\"\n", fc);
      fclose(fc);
    }
    menu_gamecfg_load(t);
    g_cfg.menukey = 0x2D; g_cfg.freecamkey = 0x71;
    g_cfg.freecamteleportkey = 0x54; g_cfg.aimbind = VK_BIND_XBUTTON2;
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(3440, 1440);
    canvas_record_end();
    {
      int notes = 0;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (o && o->kind == CANVAS_OP_TEXT && streq(o->text, MENU_GAMECFG_NOTE)) notes++;
      }
      CHECK(notes == BIND_ROW_COUNT);        // all four rows really are bound
    }
    // Debug log's hint is a tooltip, so the tab's last row is the switch itself.
    static const char* SETTINGS_BOTTOM = "Debug log";
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM, 3440, 1440));
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM, 1920, 1080));
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM, 1024, 768));
    CHECK(tab_bottom_row_fits(TAB_SETTINGS, SETTINGS_BOTTOM, 1280, 720));
    // Every row of the group survives it too: a row out of cursor height
    // draws nothing rather than overflowing, so a dropped row would still leave a bottom line.
    menu_set_tab(TAB_SETTINGS);
    canvas_record_begin();
    menu_draw(1280, 720);
    canvas_record_end();
    CHECK(canvas_record_find_text("HOTKEYS") != nullptr);
    CHECK(canvas_record_find_text("Panel key") != nullptr);
    CHECK(canvas_record_find_text("Free camera key") != nullptr);
    CHECK(canvas_record_find_text("Teleport key") != nullptr);
    CHECK(canvas_record_find_text("Aimbot button") != nullptr);
    CHECK(canvas_record_find_text("UI scale") != nullptr);
    CHECK(canvas_record_find_text("Animations") != nullptr);
    CHECK(canvas_record_find_text("Debug log") != nullptr);
    menu_gamecfg_load(0);
    remove(t);
  }

  // Nothing on the tab runs off the panel's right edge with the WIDEST key
  // name a field can show bound in every one of them.
  {
    int worst = 0;
    g_cfg.menukey = 0x22; g_cfg.freecamkey = 0x22;
    g_cfg.freecamteleportkey = 0x22; g_cfg.aimbind = 0x22;   // "Page down"
    CHECK(tab_text_fits(TAB_SETTINGS, 3440, 1440, 18, &worst));
    CHECK(tab_text_fits(TAB_SETTINGS, 1024, 768, 18, &worst));
  }

  g_cfg.menukey = savedMenu; g_cfg.aimbind = savedAim;
  g_cfg.freecamkey = savedCam; g_cfg.freecamteleportkey = savedTp;
  g_cfg.freecamon = savedOn; g_cfg.aimon = savedAimOn;
  g_cfg.menuanim = savedAnim;
  g_cfg.uiscale = savedScale;
  menu_keycap_cancel();
  menu_gamecfg_load(0);
  menu_set_tab(savedTab);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  g_cfgDirty = false;
}


// Hovered field's colours: fill moves toward C_RAISED, rim toward C_BONEDIM, text not at all (all three once lightened
// toward C_BONE, erasing the name at full hover). The blend rule (anim.h) is pinned pure first, then read off a real
// Simple frame.
void tb_0118_bind_field_hover_colours() {
  const int savedTab   = menu_active_tab();
  const int savedAnim  = g_cfg.menuanim;
  const int savedScale = g_cfg.uiscale;
  const int savedMenu  = g_cfg.menukey, savedAim = g_cfg.aimbind;
  const int savedCam   = g_cfg.freecamkey, savedTp = g_cfg.freecamteleportkey;

  // anim_blend_colour, the pure rule: C_WELL toward C_RAISED at both ends and the middle, the midpoint written as the
  // hex it computes to, not an expression of the ends.
  {
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 0.0) == C_WELL);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 0.0) == 0xFF0E1114u);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 0.5) == 0xFF20262Du);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 1.0) == C_RAISED);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 1.0) == 0xFF333C46u);
    // Source's alpha survives, target's is ignored -- the panel's own
    // open/tab fades own alpha.
    CHECK(anim_blend_colour(0x800E1114u, C_RAISED, 1.0) == 0x80333C46u);
    CHECK(anim_blend_colour(0x000E1114u, 0xFF333C46u, 0.5) == 0x0020262Du);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, -1.0) == C_WELL);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, -0.0001) == C_WELL);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 2.5) == C_RAISED);
    CHECK(anim_blend_colour(C_WELL, C_RAISED, 1.0001) == C_RAISED);
    CHECK(anim_blend_colour(C_RAISED, C_BONEDIM, 1.0) == C_BONEDIM);
    CHECK(anim_blend_colour(C_RAISED, C_BONEDIM, 0.0) == C_RAISED);
    CHECK(anim_blend_colour(C_WELL, C_BONE, 1.0) == C_BONE);
    CHECK(anim_blend_colour(C_BONE, C_WELL, 1.0) == C_WELL);
  }

  // What row_bind actually draws with it: ANIM_SIMPLE (ANIM_OFF is the
  // identity and would show nothing). Clock driven by hand
  // (menu_set_now_ms) so the hover fraction really reaches 1.0.
  g_cfg.uiscale = UI_SCALE_DEFAULT;   // logical and device agree, as tb_0117 keeps them
  g_cfg.menuanim = ANIM_SIMPLE;
  g_cfg.menukey = 0x2D;               // Insert
  g_cfg.freecamkey = 0x56;            // V
  g_cfg.freecamteleportkey = 0x54;    // T
  g_cfg.aimbind = VK_BIND_XBUTTON2;   // Mouse 5
  menu_keycap_cancel();
  menu_gamecfg_load(0);
  menu_stub_set_open(true);
  menu_set_tab(TAB_SETTINGS);
  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  unsigned t = 900000u;
  // Settled: open spring on 1, tab crossfade finished, every row's hover
  // decayed to 0 with the cursor off the panel (ANIM_HOVER_OUT_MS is 160).
  for (int i = 0; i < 80; i++) { t += 16; menu_set_now_ms(t); menu_draw(3440, 1440); }
  t += 16; menu_set_now_ms(t);
  canvas_record_begin();
  menu_draw(3440, 1440);
  canvas_record_end();
  CHECK(!canvas_record_overflowed());
  {
    // A settled Simple frame is at full alpha, so the colour words below
    // are exact rather than "C_RAISED at whatever alpha the fade was at".
    CHECK(canvas_record_find_kind(CANVAS_OP_ROUNDRECT, C_WELL) != nullptr);
    const CanvasOp* ins = canvas_record_find_text("Insert");
    const CanvasOp* lbl = canvas_record_find_text("Panel key");
    CHECK(ins && lbl);
    if (ins && lbl) {
      g_ui.mx = ins->x + ins->w / 2; g_ui.my = lbl->y;
      for (int i = 0; i < 10; i++) { t += 16; menu_set_now_ms(t); menu_draw(3440, 1440); }
      t += 16; menu_set_now_ms(t);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      CHECK(!canvas_record_overflowed());
      int wells = 0, raisedFills = 0, raisedRims = 0, litRims = 0;
      const CanvasOp* fill = nullptr;
      for (int i = 0; i < canvas_record_count(); i++) {
        const CanvasOp* o = canvas_record_op(i);
        if (!o) continue;
        if (o->kind == CANVAS_OP_ROUNDRECT && o->colour == C_WELL) wells++;
        if (o->kind == CANVAS_OP_ROUNDRECT && o->colour == C_RAISED) { raisedFills++; fill = o; }
        if (o->kind == CANVAS_OP_ROUNDOUTLINE && o->colour == C_RAISED) raisedRims++;
        if (o->kind == CANVAS_OP_ROUNDOUTLINE && o->colour == C_BONEDIM) litRims++;
      }
      CHECK(wells == BIND_ROW_COUNT - 1);   // the three the cursor is not on
      CHECK(raisedFills == 1);              // the hovered well, RAISED and not bone
      CHECK(raisedRims == BIND_ROW_COUNT - 1);
      CHECK(litRims == 1);
      const CanvasOp* text = canvas_record_find_text(MENU_KEYCAP_HOVER);
      CHECK(text != nullptr);
      CHECK(fill != nullptr);
      if (fill && text) {
        CHECK(fill->colour == C_RAISED);
        CHECK(text->colour == C_BONE);
        CHECK(fill->colour != text->colour);
        CHECK(text->x >= fill->x && text->x + text->w <= fill->x + fill->w);
        CHECK(fill->colour != C_BONE);
      }
      g_ui.pressed = true;
      t += 16; menu_set_now_ms(t); menu_draw(3440, 1440);
      g_ui.pressed = false;
      CHECK(menu_keycap_armed());
      t += 16; menu_set_now_ms(t);
      canvas_record_begin();
      menu_draw(3440, 1440);
      canvas_record_end();
      {
        const CanvasOp* prompt = canvas_record_find_text(MENU_KEYCAP_PROMPT);
        CHECK(canvas_record_find_kind(CANVAS_OP_ROUNDOUTLINE, C_HAZARD) != nullptr);
        CHECK(prompt != nullptr);
        if (prompt) CHECK(prompt->colour == C_HAZARD);
        CHECK(canvas_record_find_kind(CANVAS_OP_ROUNDRECT, C_RAISED) != nullptr);
      }
      menu_keycap_cancel();
      CHECK(g_cfg.menukey == 0x2D);       // cancelling kept the binding
    }
  }

  g_ui.mx = -1; g_ui.my = -1; g_ui.down = false; g_ui.pressed = false;
  menu_set_now_ms(0);                   // release the clock override
  menu_stub_set_open(true);
  g_cfg.menuanim = savedAnim;
  g_cfg.uiscale = savedScale;
  g_cfg.menukey = savedMenu; g_cfg.aimbind = savedAim;
  g_cfg.freecamkey = savedCam; g_cfg.freecamteleportkey = savedTp;
  menu_keycap_cancel();
  menu_set_tab(savedTab);
  g_cfgDirty = false;
}


// The key table: vk_to_sdl_scancode maps every standard key; modifiers, lock/system keys and Escape (the capture's own
// cancel) are the only deliberate omissions. Existing sweeps carry the inverse and named-iff-mapped invariants; this
// pins the table's contents, omissions and size.
void tb_0119_widened_key_table() {
  // One code from every group the table gained, mapped and named, with the SDL2 scancode written out rather than
  // derived.
  struct KeyPin { int vk; int sc; const char* name; };
  static const KeyPin PINS[] = {
    { 0x41, 4,  "A" },        // letters, A first
    { 0x5A, 29, "Z" },        // ... and Z last
    { 0x31, 30, "1" },        // digits: '1' is the FIRST scancode, not '0'
    { 0x39, 38, "9" },
    { 0x30, 39, "0" },        // '0' sits after '9', which is the easy one to get wrong
    { 0x0D, 40, "Enter" },
    { 0x08, 42, "Backspace" },
    { 0x20, 44, "Space" },
    { 0xBD, 45, "-" },
    { 0xBF, 56, "/" },
    { 0x13, 72, "Pause" },
    { 0x25, 80, "Left" },     // the arrows are NOT in reading order: right is 79
    { 0x27, 79, "Right" },
    { 0x26, 82, "Up" },
    { 0x28, 81, "Down" },
    { 0x60, 98, "Num 0" },    // and the pad's 0 sits after its 9, like the row above
    { 0x69, 97, "Num 9" },
    { 0x6E, 99, "Num ." },
    { 0x6F, 84, "Num /" },
  };
  const int pinCount = (int)(sizeof(PINS) / sizeof(PINS[0]));
  for (int i = 0; i < pinCount; i++) {
    int sc = -1;
    CHECK(vk_to_sdl_scancode(PINS[i].vk, &sc));
    CHECK(sc == PINS[i].sc);
    CHECK(bind_from_sdl_scancode(PINS[i].sc) == PINS[i].vk);   // and back again
    CHECK(streq(bind_label(PINS[i].vk), PINS[i].name));
    CHECK(bind_matches_key(PINS[i].vk, PINS[i].sc));
  }
  // The two pre-existing codes are untouched, and the letter run lands on
  // them exactly right (T is the 20th letter: 4+19=23; V the 22nd: 4+21=25).
  {
    int sc = -1;
    CHECK(vk_to_sdl_scancode(0x54, &sc) && sc == 23);
    CHECK(vk_to_sdl_scancode(0x56, &sc) && sc == 25);
    CHECK(streq(bind_label(0x54), "T"));
    CHECK(streq(bind_label(0x56), "V"));
  }
  {
    bool allLetters = true;
    for (int i = 0; i < 26; i++) {
      int sc = -1;
      const char want[2] = { (char)('A' + i), 0 };
      if (!vk_to_sdl_scancode(0x41 + i, &sc) || sc != 4 + i) allLetters = false;
      if (!streq(bind_label(0x41 + i), want)) allLetters = false;
    }
    CHECK(allLetters);
    bool allDigits = true;
    for (int i = 1; i <= 9; i++) {
      int sc = -1;
      const char want[2] = { (char)('0' + i), 0 };
      if (!vk_to_sdl_scancode(0x30 + i, &sc) || sc != 29 + i) allDigits = false;
      if (!streq(bind_label(0x30 + i), want)) allDigits = false;
    }
    CHECK(allDigits);
    bool allPad = true;
    for (int i = 1; i <= 9; i++) {
      int sc = -1;
      char want[8];
      sprintf_s(want, "Num %d", i);
      if (!vk_to_sdl_scancode(0x60 + i, &sc) || sc != 88 + i) allPad = false;
      if (!streq(bind_label(0x60 + i), want)) allPad = false;
    }
    CHECK(allPad);
  }
  // Deliberate omissions, by code and scancode: Escape is the capture's cancel; modifiers, Windows keys and lock/system
  // keys are not hotkey material; numeric pad Enter has no virtual key of its own.
  {
    static const int NOT_MAPPED[] = {
      0x1B,                                 // Escape
      0x10, 0x11, 0x12,                     // Shift, Ctrl, Alt (either side)
      0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,   // and their left/right halves
      0x5B, 0x5C, 0x5D,                     // the two Windows keys and the menu key
      0x14, 0x90, 0x91,                     // Caps lock, Num lock, Scroll lock
      0x2C,                                 // Print screen
    };
    bool noneMapped = true;
    for (int i = 0; i < (int)(sizeof(NOT_MAPPED) / sizeof(NOT_MAPPED[0])); i++) {
      int sc = -1;
      if (vk_to_sdl_scancode(NOT_MAPPED[i], &sc)) noneMapped = false;
      if (!streq(bind_label(NOT_MAPPED[i]), "Unbound")) noneMapped = false;
      if (bind_matches_key(NOT_MAPPED[i], 4)) noneMapped = false;
    }
    CHECK(noneMapped);
    CHECK(bind_from_sdl_scancode(41) == 0);    // SDL_SCANCODE_ESCAPE
    CHECK(bind_from_sdl_scancode(225) == 0);   // SDL_SCANCODE_LSHIFT
    CHECK(bind_from_sdl_scancode(57) == 0);    // SDL_SCANCODE_CAPSLOCK
    CHECK(bind_from_sdl_scancode(88) == 0);    // SDL_SCANCODE_KP_ENTER
    // A code with no key behind it still falls back (the one fallback
    // this DLL has, which a widened table must not swallow).
    CHECK(bind_menu_key_effective(0x10) == 0x2D);
  }
  // Table size stated as a number so a group dropped wholesale fails
  // rather than a sweep quietly covering less.
  {
    int mapped = 0;
    for (int vk = 0; vk <= 255; vk++) {
      int sc = -1;
      if (vk_to_sdl_scancode(vk, &sc)) mapped++;
    }
    CHECK(mapped == 89);
    CHECK(mapped == 26 + 10 + 4 + 11 + 12 + 1 + 6 + 4 + 15);
  }
  // No name outgrows the field: BIND_LABEL_WIDEST is still nine
  // characters ("Backspace" ties "Page down"), so tb_0117's sweep still holds.
  {
    CHECK(strlen(BIND_LABEL_WIDEST) == 9);
    CHECK(strlen(bind_label(0x08)) == strlen(BIND_LABEL_WIDEST));   // "Backspace"
    bool allFit = true;
    for (int vk = 0; vk <= 255; vk++)
      if (strlen(bind_label(vk)) > strlen(BIND_LABEL_WIDEST)) allFit = false;
    CHECK(allFit);
  }
}
