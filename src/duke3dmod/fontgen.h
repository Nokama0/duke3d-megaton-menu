#pragma once
// The panel fonts rasterized at the applied UI scale. DLL only: GDI cannot link into test_host (packing: fontpack.h).
#include "font.h"

struct FontAtlas {
  int w, h;
  unsigned char* pixels;          // w*h alpha bytes, owned (fontgen_free)
  Glyph glyphs[FONT_COUNT][96];   // DEVICE pixels at this scale
  int height[FONT_COUNT];         // tmHeight per face, for the log
  int pct;
};

// ALL OR NOTHING: any face unavailable or unpackable returns false with `*out` untouched (a partial atlas would draw blank
// rectangles). False for UI_SCALE_DEFAULT, the baked atlas.
bool fontgen_build(int pct, FontAtlas* out);

void fontgen_free(FontAtlas* a);
