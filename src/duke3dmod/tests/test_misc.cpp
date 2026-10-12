#include "test_common.h"

#include "test_blocks.h"



// To ADD a check, append a tb_ function to the area file it belongs in, declare it in test_blocks.h and call it from main() in
// test_host.cpp where the order rule holds (after what it depends on, usually the end).




void tb_0005_remove_tmp() {
remove(tmp);
}


void tb_0008_char_buf_2048_0_fopen_s() {
    char buf[2048] = {0};
    fopen_s(&f, tmp, "r");
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    CHECK(strstr(buf, "andnormalkey=99") != nullptr);
  }


void tb_0009_remove_tmp_2() {
remove(tmp);
}


void tb_0011_remove_tmp_3() {
remove(tmp);
}


// ui_bytes_any_nonzero: all-zero is false; one nonzero byte anywhere, or at either end, is true; an empty range is false.
void tb_0016_restore() {
    unsigned char allZero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(ui_bytes_any_nonzero(allZero, 8) == false);
    unsigned char oneSet[8] = { 0, 0, 0, 1, 0, 0, 0, 0 };
    CHECK(ui_bytes_any_nonzero(oneSet, 8) == true);
    unsigned char firstSet[8] = { 5, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(ui_bytes_any_nonzero(firstSet, 8) == true);
    unsigned char lastSet[8] = { 0, 0, 0, 0, 0, 0, 0, 9 };
    CHECK(ui_bytes_any_nonzero(lastSet, 8) == true);
    CHECK(ui_bytes_any_nonzero(allZero, 0) == false);
  }



// canvas_hazard_params_ok guards the stripe loop against a non-positive period (it would hang the render thread) and a
// non-positive w or h (degenerate scissor).
void tb_0030_canvas_hazard_params_ok_guards_stripe_loop_canvas_hazard() {
CHECK(!canvas_hazard_params_ok(620, 15, 0));
  CHECK(!canvas_hazard_params_ok(620, 15, -3));
  CHECK(!canvas_hazard_params_ok(0, 15, 9));
  CHECK(!canvas_hazard_params_ok(620, 0, 9));
  CHECK(canvas_hazard_params_ok(620, 15, 9));

  // Every printable ASCII glyph exists and advances; space has no pixels.
  for (int fnt = 0; fnt < FONT_COUNT; fnt++) {
    CHECK(font_height[fnt] > 0);
    for (int c = 0x20; c <= 0x7E; c++) {
      const Glyph& g = font_glyphs[fnt][c - 0x20];
      CHECK(g.adv > 0);
      CHECK(g.x >= 0 && g.y >= 0);
      CHECK(g.x + g.w <= FONT_ATLAS_W && g.y + g.h <= FONT_ATLAS_H);
    }
    CHECK(font_glyphs[fnt][0].w == 0);  // space is blank
  }
  // The mono face must be monospaced for numeric columns.
  for (int c = '0'; c <= '9'; c++)
    CHECK(font_glyphs[FONT_MONO][c - 0x20].adv == font_glyphs[FONT_MONO]['0' - 0x20].adv);
  CHECK(canvas_text_width("AB", FONT_UI) ==
        font_glyphs[FONT_UI]['A' - 0x20].adv + font_glyphs[FONT_UI]['B' - 0x20].adv);
  CHECK(canvas_text_width("", FONT_UI) == 0);
}
