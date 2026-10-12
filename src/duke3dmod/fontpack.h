#pragma once
// Pure shelf packer for the runtime font atlas. SHELF, NOT A SORT: the glyph index IS the position in the array, so
// reordering by height would hand every glyph someone else's rectangle.
struct PackBox { int w, h; };
struct PackPos { short x, y; };

inline bool fontpack_shelf(const PackBox* boxes, int n, int atlasW, int atlasH, int pad,
                           PackPos* out) {
  if (!boxes || !out || n < 0 || atlasW <= 0 || atlasH <= 0 || pad < 0) return false;
  int penX = pad, penY = pad, rowH = 0;
  for (int i = 0; i < n; i++) {
    const int w = boxes[i].w, h = boxes[i].h;
    if (w <= 0 || h <= 0) { out[i].x = 0; out[i].y = 0; continue; }
    if (penX + w + pad > atlasW) { penX = pad; penY += rowH + pad; rowH = 0; }
    if (penX + w + pad > atlasW) return false;
    if (penY + h + pad > atlasH) return false;
    out[i].x = (short)penX; out[i].y = (short)penY;
    penX += w + pad;
    if (h > rowH) rowH = h;
  }
  return true;
}

// Size ladder, smallest first: 512x256 is the baked atlas at 100%; 2048x2048 is the GL 1.1 ceiling.
static const PackBox FONTPACK_LADDER[6] =
  { {512,256}, {512,512}, {1024,512}, {1024,1024}, {2048,1024}, {2048,2048} };

// Index of the FIRST rung the boxes fit (out and chosen filled for it), or -1. Smallest first (GPU upload).
inline int fontpack_choose(const PackBox* boxes, int n, int pad, PackPos* out, PackBox* chosen) {
  for (int r = 0; r < 6; r++) {
    if (fontpack_shelf(boxes, n, FONTPACK_LADDER[r].w, FONTPACK_LADDER[r].h, pad, out)) {
      if (chosen) *chosen = FONTPACK_LADDER[r];
      return r;
    }
  }
  return -1;
}
