#pragma once
const unsigned C_PANEL   = 0xF014171A;
const unsigned C_RECESS  = 0xF01C2127;
const unsigned C_RAISED  = 0xFF333C46;
const unsigned C_BEVELHI = 0xFF48535F;
const unsigned C_BEVELLO = 0xFF0A0C0E;
const unsigned C_HAZARD  = 0xFFF2B01E;
const unsigned C_RED     = 0xFFD8402F;
const unsigned C_BLUE    = 0xFF3C9BD6;
const unsigned C_GREEN   = 0xFF6FBF5B;
const unsigned C_BONE    = 0xFFE8E4DA;
const unsigned C_BONEDIM = 0xFF9AA1A8;
const unsigned C_BONEFNT = 0xFF666E77;
const unsigned C_WELL      = 0xFF0E1114;  // darkest sunken steel: switch/slider wells, tab rail
const unsigned C_HAZARDDIM = 0xFF8A6512;  // the slider's filled track
const unsigned C_AIM       = 0xFFC061E8;  // aim marker: violet, unused elsewhere
const unsigned C_PLAYER    = 0xFF2FE0D0;  // free camera's own-body marker: cyan
const unsigned C_SETTINGS  = 0xFFB8A9E0;  // Settings tab rail accent: muted lavender

// In-world tint on switch faces, alpha 0x60 reads as a wash. Pushed toward full saturation so they survive a level texture.
const unsigned C_TINT_FLIP  = 0x6060FF60;  // flip: translucent green
const unsigned C_TINT_LEAVE = 0x60FF5050;  // leave: translucent red

// C_HAZARD's hue at alpha 0x40: 2-3 room panels overlap along one sightline.
const unsigned C_SECRET_VOLUME = 0x40F2B01E;

// C_PLAYER cyan at C_SECRET_VOLUME's alpha, so it reads as a landing, not another secret room.
const unsigned C_TELEPORT_MARK = 0x402FE0D0;

// Glass style (menuanim == 2) colours, the palette's only translucent-surface entries.
const unsigned C_GLASS_TINT = 0x99141C26;  // slab ground over the blurred backdrop; alpha is how much scene shows through
const unsigned C_GLASS_RIM  = 0xB0A8C8E0;  // one-pixel rim; brighter than C_BEVELHI since Glass has no bevel
const unsigned C_GLASS_TOP  = 0x40D8E8F8;  // top-edge sheen gradient
const unsigned C_GLASS_GLOW = 0x66A8D8FF;  // additive bloom

// True when canvas_hazard's stripe geometry is well formed: a non-positive period never advances the loop (glBegin left
// open) and a non-positive w/h gives a degenerate scissor.
inline bool canvas_hazard_params_ok(int w, int h, int period) {
  return w > 0 && h > 0 && period > 0;
}

// Red enemy, blue pickup. Keycards keep their colour in the NAME, so a red box always means enemy.
inline unsigned canvas_esp_colour(bool enemy) { return enemy ? C_RED : C_BLUE; }

inline unsigned canvas_esp_secret_colour() { return C_HAZARD; }

inline unsigned canvas_notice_colour() { return C_HAZARD; }

inline unsigned canvas_aim_colour() { return C_AIM; }

inline unsigned canvas_player_colour() { return C_PLAYER; }

inline unsigned canvas_esp_explodable_colour() { return C_HAZARD; }

inline unsigned canvas_esp_switch_colour() { return C_BLUE; }

inline unsigned canvas_health_colour(int hp, int maxhp) {
  if (maxhp <= 0 || hp <= 0) return C_RED;
  const long long pct = (long long)hp * 100 / maxhp;
  if (pct > 50) return C_GREEN;
  if (pct > 25) return C_HAZARD;
  return C_RED;
}

inline int canvas_bar_fill(int total, int hp, int maxhp) {
  if (total <= 0 || maxhp <= 0 || hp <= 0) return 0;
  if (hp >= maxhp) return total;
  return (int)((long long)total * hp / maxhp);
}

// Alpha for blur tap i of `taps` so the passes sum to exactly aTint. Cumulative rounding: aTint/taps plus a remainder goes
// negative on the last tap (aTint=5, taps=9) and a negative alpha wraps to a wrong colour byte.
inline int canvas_tap_alpha(int aTint, int taps, int i) {
  if (taps <= 0 || i < 0 || i >= taps) return 0;
  if (aTint < 0) aTint = 0;
  if (aTint > 255) aTint = 255;
  const int upto = (int)((double)aTint * (double)(i + 1) / (double)taps + 0.5);
  const int prev = (int)((double)aTint * (double)i / (double)taps + 0.5);
  return upto - prev;
}

inline unsigned canvas_with_alpha(unsigned rgba, int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;
  return (rgba & 0x00FFFFFFu) | ((unsigned)alpha << 24);
}

// Open-motion transform applied to device coords after the UI scale: d = ((p - c) * scale + c) + offset. Hit-testing stays
// logical. Identity is exact; canvas_begin resets it and menu_draw sets it each frame.
struct CanvasTransform { int dx, dy, cx, cy; double scale; };
void canvas_set_transform(int dx, int dy, double scale, int cx, int cy);

#ifdef NO_GL
// Test-build recording: canvas.cpp's NO_GL bodies append one CanvasOp per call so menu_draw's output is observable.
const int CANVAS_RECORD_MAX = 512;
enum { CANVAS_OP_RECT = 0, CANVAS_OP_BEVEL, CANVAS_OP_HAZARD,
       CANVAS_OP_LINE, CANVAS_OP_TEXT,
       CANVAS_OP_ROUNDRECT, CANVAS_OP_GRADIENT, CANVAS_OP_GLOW,
       CANVAS_OP_CAPTURE, CANVAS_OP_BACKDROP,
       CANVAS_OP_ROUNDOUTLINE };
struct CanvasOp {
  int kind;         // CANVAS_OP_*
  int x, y, w, h;   // TEXT: w is the rendered width, h is 0
  unsigned colour;  // BEVEL: the FILL colour
  int font;         // TEXT only, else -1
  char text[64];    // TEXT only, truncated
};
void canvas_record_begin();
void canvas_record_end();
int  canvas_record_count();
bool canvas_record_overflowed(); // ops were dropped: never ignore it
const CanvasOp* canvas_record_op(int i);
const CanvasOp* canvas_record_find_text(const char* s);
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour);
CanvasTransform canvas_record_transform();
#endif // NO_GL

// Primitive coordinates are LOGICAL; the canvas multiplies by this factor (pct clamped by ui_scale_clamped). menu_draw
// restores UI_SCALE_DEFAULT on exit so ESP markers are not scaled. canvas_text_width stays LOGICAL, else layout would apply
// the factor twice.
void canvas_set_scale(int pct);
int  canvas_scale();

void canvas_begin(int w, int h);
void canvas_end();
void canvas_rect(int x, int y, int w, int h, unsigned rgba);
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo);
void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba);
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period);
int canvas_text(int x, int y, const char* s, unsigned rgba, int font);
int canvas_text_width(const char* s, int font);

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba);
// A ring `thickness` logical px wide inside the rect, its own primitive: a filled round rect under the slab only reads as
// an edge under an opaque cover. Floors at one device pixel.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba);
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom);
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba);
bool canvas_backdrop_capture(int x, int y, int w, int h);
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius);
void canvas_backdrop_release();
