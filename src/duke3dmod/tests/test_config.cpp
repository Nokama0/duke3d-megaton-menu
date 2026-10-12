#include "test_common.h"
#include "../anim.h"

#include "test_blocks.h"



// To ADD a check, append a tb_ function to the area file it belongs in, declare it in test_blocks.h and call it from
// main() in test_host.cpp after whatever it depends on.





void tb_0007_overlong_line_followed_normal_unknown() {
fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\n", f);
  fputs("verylong=", f);
  for (int i = 0; i < 505; i++) fputc('y', f);
  fputs("\n", f);
  fputs("andnormalkey=99\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(config_save(tmp));
}



// Final line with no trailing newline (511 characters, the boundary case) should still parse.
void tb_0010_final_line_trailing_newline_should() {
    const char* setting_prefix = "debuglog=1";
    int padding = CONFIG_LINE_BUFSIZE - 1 - (int)strlen(setting_prefix);

    fopen_s(&f, tmp, "w");
    fputs("[nukepanel]\n", f);
    fputs(setting_prefix, f);
    for (int i = 0; i < padding; i++) fputc('x', f);
    fclose(f);
    config_load(tmp);
    CHECK(g_cfg.debuglog == 1);
  }


// The four new config keys: defaults, ini round trip, and the state logger seeing exactly the one that changed.
void tb_0080_phase_6_config_keys() {
  config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.freecamfog == 0 && g_cfg.freecamfullbright == 0 &&
        g_cfg.freecamshot == 0 && g_cfg.fullbright == 0);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nfreecamfog=1\nfreecamfullbright=7\nfreecamshot=1\nfullbright=1\n", f);
  fclose(f);
  config_load(tb_path("Z:|does|not|exist.ini"));
  config_load(tmp);
  CHECK(g_cfg.freecamfog == 1 && g_cfg.freecamfullbright == 1 &&
        g_cfg.freecamshot == 1 && g_cfg.fullbright == 1);
  CHECK(config_save(tmp));
  config_load(tb_path("Z:|does|not|exist.ini"));
  config_load(tmp);
  CHECK(g_cfg.freecamfog == 1 && g_cfg.fullbright == 1);
  remove(tmp);
  Config before = g_cfg;
  g_cfg.freecamshot = 0;
  log_record_begin();
  config_log_state_changes(before, g_cfg);
  log_record_end();
  CHECK(log_record_count() == 1);
  CHECK(log_record_find("state: freecamshot 1 -> 0") != nullptr);
}

// The three mouse-aim keys: defaults, ini round trip with both ranges clamped at both ends, and the state logger.
void tb_0085_phase_7_config() {
  config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.mouseaimon == 0 && g_cfg.mouseaimsens == MOUSEAIM_SENS_DEFAULT &&
        g_cfg.mouseaimvscale == MOUSEAIM_VSCALE_DEFAULT);

  // An out-of-range sensitivity clamps to the top of its range, an out-of-range vertical scale to the bottom.
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmouseaimon=1\nmouseaimsens=999\nmouseaimvscale=10\n", f);
  fclose(f);
  config_load(tb_path("Z:|does|not|exist.ini"));
  config_load(tmp);
  CHECK(g_cfg.mouseaimon == 1);
  CHECK(g_cfg.mouseaimsens == MOUSEAIM_SENS_MAX);
  CHECK(g_cfg.mouseaimvscale == MOUSEAIM_VSCALE_MIN);
  CHECK(config_save(tmp));

  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmouseaimsens=1\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.mouseaimsens == MOUSEAIM_SENS_MIN);
  remove(tmp);

  Config before = g_cfg;
  before.mouseaimon = 0;
  g_cfg.mouseaimon = 1;
  log_record_begin();
  config_log_state_changes(before, g_cfg);
  log_record_end();
  CHECK(log_record_count() == 1);
  CHECK(log_record_find("state: mouseaimon 0 -> 1") != nullptr);

  // `menuanim`: default ANIM_OFF, not a switch (any value 0..2 survives), clamped on load through anim_style_clamped.
  config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.menuanim == ANIM_OFF);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenuanim=1\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menuanim == ANIM_SIMPLE);
  CHECK(config_save(tmp));
  config_load(tmp);
  CHECK(g_cfg.menuanim == ANIM_SIMPLE);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenuanim=2\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menuanim == ANIM_GLASS);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenuanim=99\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menuanim == ANIM_GLASS);         // out of range clamps to the top
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\nmenuanim=-3\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menuanim == ANIM_OFF);
  remove(tmp);
  config_load(tb_path("Z:|does|not|exist.ini"));
}

void tb_0068_cfg_path_char() {
    char buf[260];
    CHECK(build_gamecfg_path(tb_path("C:|Games|Duke|bin|"), buf, sizeof(buf)));
    CHECK(streq(buf, tb_path("C:|Games|Duke|bin|..|gameroot|duke3d.cfg")));
    CHECK(build_gamecfg_path("", buf, sizeof(buf)));
    CHECK(streq(buf, tb_path("..|gameroot|duke3d.cfg")));
    // Over-long input truncates to a valid null-terminated string and reports false, never the CRT invalid-parameter handler.

    char tiny[8];
    CHECK(!build_gamecfg_path(tb_path("C:|Games|Duke|bin|"), tiny, sizeof(tiny)));
    CHECK(tiny[sizeof(tiny) - 1] == 0);
  }

// A NUL inside an ini line hides its newline from string functions; the next line must still be read.
void tb_0137_ini_nul_keeps_next_line() {
  char b[8];
  b[7] = 1; memcpy(b, "ab\0c\n\0", 6);           // fgets stopped at a newline: the mark survives
  CHECK(!config_line_maybe_truncated(b, sizeof(b)));
  memcpy(b, "abcdefg", 8);                       // filled the buffer, no newline: the line may go on
  CHECK(config_line_maybe_truncated(b, sizeof(b)));
  memcpy(b, "abcdef\n", 8);                      // filled the buffer exactly with its newline: complete
  CHECK(!config_line_maybe_truncated(b, sizeof(b)));

  fopen_s(&f, tmp, "wb");
  static const char ini[] = "[nukepanel]\nfoo\0bar=1\nmenukey=77\n";
  fwrite(ini, 1, sizeof(ini) - 1, f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.menukey == 77);
  remove(tmp);
  config_load(tb_path("Z:|does|not|exist.ini"));
}
