#pragma once
// BUILD_UNITS_PER_METRE, for EK_METRES: a field the player types METRES into stores WORLD UNITS.
#include "project.h"
#include <stdio.h>

// The numeric entry field: right-click a slider row and type the value. Every rule lives here so the suite pins it. `open`/
// `commit` are int, not bool: MSVC 19.50 /O2 loses a byte-sized bool member of a struct returned by value after
// constant-folding (docs/re/msvc-bool-fold.md).

enum { EK_NONE = 0, EK_INT = 1, EK_MILLI = 2, EK_METRES = 3 };

const int ENTRY_TEXT_CAP = 11;

// Largest whole part before the kind's multiply (x1000, x512) so it cannot overflow; extra digits are discarded, not refused.
const long long ENTRY_WHOLE_CAP = 2000000LL;

// CLICKAWAY is a COMMIT: clicking elsewhere after typing says the number; Escape is the only cancel.
enum {
  ENTRY_EV_OPEN = 0,
  ENTRY_EV_CHAR,
  ENTRY_EV_BACKSPACE,
  ENTRY_EV_ENTER,
  ENTRY_EV_ESCAPE,
  ENTRY_EV_CLICKAWAY,
  ENTRY_EV_DELETE,        // the selection, else the character AFTER the caret
  ENTRY_EV_LEFT,          // `ch` is the shift flag (also RIGHT/HOME/END)
  ENTRY_EV_RIGHT,
  ENTRY_EV_HOME,
  ENTRY_EV_END,
  ENTRY_EV_SELECT_ALL,
  ENTRY_EV_SET_CARET,     // `ch` is the INDEX, `rowKey` the shift flag
  ENTRY_EV_PASTE,         // `text` is the string
  ENTRY_EV_COPY,          // fills EntryResult.clip
  ENTRY_EV_CUT
};

// `prefill` decides a commit. The opening text is the row's DISPLAY, not always its value (EK_METRES: 5000 units opens as
// "9", reads back 4608), so Enter/clickaway commit only when the text DIFFERS from it; `edited` cannot decide (type then
// Backspace would arm a commit). Caret/anchor are boundaries 0..len; their order records drag direction.
struct EntryState {
  int  open;
  int  rowKey;                      // -1 when closed
  int  kind;
  int  lo, hi;                      // applied on commit
  int  len;
  int  edited;
  int  caret;                       // 0..len
  int  anchor;                      // the other end of the selection
  char text[ENTRY_TEXT_CAP + 1];
  // Written once by OPEN; the row's value can move under an open field.
  char prefill[ENTRY_TEXT_CAP + 1];
};

struct EntryResult {
  EntryState state;
  int commit;
  int value;                        // meaningful only when commit is 1
  // COPY/CUT only; the Windows clipboard is the DLL's business (clipboard.h).
  int  hasClip;
  char clip[ENTRY_TEXT_CAP + 1];
};

inline EntryState entry_begin() {
  EntryState s;
  s.open = 0;
  s.rowKey = -1;
  s.kind = EK_NONE;
  s.lo = 0;
  s.hi = 0;
  s.len = 0;
  s.edited = 0;
  s.caret = 0;
  s.anchor = 0;
  // Whole buffer: copied by value, and an uninitialised tail would make equal states compare unequal.
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) { s.text[i] = 0; s.prefill[i] = 0; }
  return s;
}

inline bool entry_text_is_prefill(const EntryState& s) {
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) {
    if (s.text[i] != s.prefill[i]) return false;
    if (s.text[i] == 0) return true;
  }
  return true;
}

inline int entry_sel_lo(const EntryState& s) { return s.caret < s.anchor ? s.caret : s.anchor; }
inline int entry_sel_hi(const EntryState& s) { return s.caret > s.anchor ? s.caret : s.anchor; }

// Whether a '.' survives outside [a, b): typing a point over a selection holding the only point is legal.
inline bool entry_has_dot_outside(const char* text, int len, int a, int b) {
  for (int i = 0; i < len; i++) {
    if (i >= a && i < b) continue;
    if (text[i] == '.') return true;
  }
  return false;
}

inline int entry_delete_range(EntryState* s, int a, int b) {
  if (!s || a < 0 || b > s->len || a >= b) return 0;
  const int n = b - a;
  for (int i = b; i <= s->len; i++) s->text[i - n] = s->text[i];
  s->len -= n;
  s->caret = a;
  s->anchor = a;
  return 1;
}

// One character in at the caret, over the selection if any. Rules are asked of the text as it WILL BE, so selecting all and
// typing a digit works. A refused character changes nothing.
inline int entry_type_char(EntryState* s, int ch) {
  if (!s) return 0;
  const int a = entry_sel_lo(*s), b = entry_sel_hi(*s);
  const int rem = s->len - (b - a);
  bool accept = false;
  if (ch >= '0' && ch <= '9') accept = true;
  // '-' at index 0 only, on a row that goes below zero: "-5-" and "--5" are refused by entry_parse.
  else if (ch == '-') accept = (a == 0 && s->lo < 0 && (b >= s->len || s->text[b] != '-'));
  else if (ch == '.') accept = (s->kind == EK_MILLI && !entry_has_dot_outside(s->text, s->len, a, b));
  if (!accept) return 0;
  if (rem >= ENTRY_TEXT_CAP) return 0;
  entry_delete_range(s, a, b);
  for (int i = s->len; i >= a; i--) s->text[i + 1] = s->text[i];
  s->text[a] = (char)ch;
  s->len++;
  s->caret = a + 1;
  s->anchor = s->caret;
  return 1;
}

// Text -> value in the row's units, clamped. By hand: atoi/strtol are undefined on overflow and a player can hold a key. *ok
// tells "empty" from "clamped to zero": "", "-" and "." are refusals and must not overwrite the row.
inline int entry_parse(const char* s, int kind, int lo, int hi, bool* ok) {
  if (ok) *ok = false;
  if (!s || kind == EK_NONE) return 0;

  int i = 0;
  bool neg = false;
  if (s[0] == '-') { neg = true; i = 1; }

  long long whole = 0;
  int digits = 0;
  for (; s[i] >= '0' && s[i] <= '9'; i++) {
    digits++;
    if (whole < ENTRY_WHOLE_CAP) {
      whole = whole * 10 + (long long)(s[i] - '0');
      if (whole > ENTRY_WHOLE_CAP) whole = ENTRY_WHOLE_CAP;
    }
  }

  // Fraction, EK_MILLI only: three digits kept, the rest dropped, not rounded (rounding would move the value mid-typing).
  long long frac = 0;
  if (kind == EK_MILLI && s[i] == '.') {
    i++;
    int fd = 0;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
      digits++;
      if (fd < 3) { frac = frac * 10 + (long long)(s[i] - '0'); fd++; }
    }
    while (fd < 3) { frac *= 10; fd++; }
  }

  if (s[i] != 0) return 0;
  if (digits == 0) return 0;

  long long v;
  if (kind == EK_MILLI)       v = whole * 1000LL + frac;
  else if (kind == EK_METRES) v = whole * (long long)BUILD_UNITS_PER_METRE;
  else                        v = whole;
  if (neg) v = -v;

  if (ok) *ok = true;
  return clamp_int(clamp_ll_to_int(v), lo, hi);
}

// Not always entry_parse's inverse: EK_METRES rounds DOWN (5000 units -> "9" -> 4608); the prefill comparison keeps that from
// moving a row the player only looked at.
inline void entry_format(char* out, size_t cap, int value, int kind) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (kind == EK_MILLI) {
    // Sign separate, magnitude widened first: "%d.%03d" on a negative would lose the sign.
    const long long v = (long long)value;
    const long long a = v < 0 ? -v : v;
    _snprintf_s(out, cap, _TRUNCATE, "%s%lld.%03lld", v < 0 ? "-" : "", a / 1000, a % 1000);
  } else if (kind == EK_METRES) {
    _snprintf_s(out, cap, _TRUNCATE, "%d", value / BUILD_UNITS_PER_METRE);
  } else if (kind == EK_INT) {
    _snprintf_s(out, cap, _TRUNCATE, "%d", value);
  }
}

const int SDL_SCANCODE_RETURN    = 40;
const int SDL_SCANCODE_ESCAPE    = 41;
const int SDL_SCANCODE_BACKSPACE = 42;
const int SDL_SCANCODE_KP_ENTER  = 88;

// SDL_SCANCODE_A/C/LCTRL/LSHIFT live in input.h at the same values (MSVC refuses a duplicate `const int`). Modifiers are
// input.cpp's alone.
const int SDL_SCANCODE_V      = 25;
const int SDL_SCANCODE_X      = 27;
const int SDL_SCANCODE_INSERT = 73;
const int SDL_SCANCODE_HOME   = 74;
const int SDL_SCANCODE_DELETE = 76;
const int SDL_SCANCODE_END    = 77;
const int SDL_SCANCODE_RIGHT  = 79;
const int SDL_SCANCODE_LEFT   = 80;
const int SDL_SCANCODE_RCTRL  = 228;
const int SDL_SCANCODE_RSHIFT = 229;

// Paste read size (input.cpp's stack buffer): 64 is short enough for a hook inside the game's event drain.
const int ENTRY_PASTE_CAP = 64;

// Caret index nearest a pixel; `advance` is one glyph's width (FONT_MONO).
inline int entry_index_at(int textLen, int textX, int px, int advance) {
  if (textLen < 0) textLen = 0;
  if (advance <= 0) return 0;
  const int d = px - textX;
  if (d <= 0) return 0;
  const int i = (d + advance / 2) / advance;
  return i > textLen ? textLen : i;
}

// SDL scancode -> typed character. NumLock is not consulted: with it off SDL reports arrow/Home/End scancodes.
inline char entry_char_of_scancode(int sc) {
  if (sc >= 30 && sc <= 38) return (char)('1' + (sc - 30));
  if (sc == 39) return '0';
  if (sc >= 89 && sc <= 97) return (char)('1' + (sc - 89));
  if (sc == 98) return '0';
  if (sc == 45 || sc == 86) return '-';
  if (sc == 55 || sc == 99) return '.';
  return 0;
}

// One step, one event, state in and out. Caret events: LEFT/RIGHT/HOME/END take `ch` as the shift flag; SET_CARET takes `ch` as
// the index and `rowKey` as the shift flag (0 moves both ends, the drag's start).
inline EntryResult entry_step(EntryState s, int ev, int ch, int rowKey,
                              int kind, int lo, int hi, const char* text) {
  EntryResult r;
  r.state = s;
  r.commit = 0;
  r.value = 0;
  r.hasClip = 0;
  for (int i = 0; i <= ENTRY_TEXT_CAP; i++) r.clip[i] = 0;

  if (ev == ENTRY_EV_OPEN) {
    if (kind == EK_NONE) { r.state = entry_begin(); return r; }
    r.state.open = 1;
    r.state.rowKey = rowKey;
    r.state.kind = kind;
    r.state.lo = lo;
    r.state.hi = hi;
    r.state.edited = 0;
    int n = 0;
    if (text) while (n < ENTRY_TEXT_CAP && text[n]) { r.state.text[n] = text[n]; n++; }
    r.state.text[n] = 0;
    r.state.len = n;
    for (int i = 0; i <= n; i++) r.state.prefill[i] = r.state.text[i];
    // Caret at the end: the prefill is a display, not a proposal, so select-all would cost an append's first keystroke.
    r.state.caret = n;
    r.state.anchor = n;
    return r;
  }

  if (!s.open) return r;

  if (ev == ENTRY_EV_ESCAPE) { r.state = entry_begin(); return r; }

  const int selLo = entry_sel_lo(s), selHi = entry_sel_hi(s);

  if (ev == ENTRY_EV_BACKSPACE) {
    const int a = selHi > selLo ? selLo : (selLo > 0 ? selLo - 1 : 0);
    if (entry_delete_range(&r.state, a, selHi > selLo ? selHi : selLo)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_DELETE) {
    const int b = selHi > selLo ? selHi : (selHi < r.state.len ? selHi + 1 : selHi);
    if (entry_delete_range(&r.state, selLo, b)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_CHAR) {
    if (entry_type_char(&r.state, ch)) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_PASTE) {
    // Every character through the keyboard's rule, in order, so a copied "FOV: 90" delivers "90". The selection goes first.
    if (!text || !text[0]) return r;
    if (entry_delete_range(&r.state, selLo, selHi)) r.state.edited = 1;
    for (int i = 0; text[i]; i++)
      if (entry_type_char(&r.state, (unsigned char)text[i])) r.state.edited = 1;
    return r;
  }

  if (ev == ENTRY_EV_COPY || ev == ENTRY_EV_CUT) {
    const int a = selHi > selLo ? selLo : 0;
    const int b = selHi > selLo ? selHi : r.state.len;
    int n = 0;
    for (int i = a; i < b && n < ENTRY_TEXT_CAP; i++) r.clip[n++] = r.state.text[i];
    r.clip[n] = 0;
    r.hasClip = n > 0 ? 1 : 0;
    if (ev == ENTRY_EV_CUT && selHi > selLo
        && entry_delete_range(&r.state, selLo, selHi)) r.state.edited = 1;
    return r;
  }

  // Caret and selection movement is not an edit.
  if (ev == ENTRY_EV_LEFT || ev == ENTRY_EV_RIGHT) {
    const int shift = ch ? 1 : 0;
    if (!shift && selHi > selLo) {
      const int at = (ev == ENTRY_EV_LEFT) ? selLo : selHi;
      r.state.caret = at;
      r.state.anchor = at;
      return r;
    }
    int at = r.state.caret + (ev == ENTRY_EV_LEFT ? -1 : 1);
    if (at < 0) at = 0;
    if (at > r.state.len) at = r.state.len;
    r.state.caret = at;
    if (!shift) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_HOME || ev == ENTRY_EV_END) {
    const int at = (ev == ENTRY_EV_HOME) ? 0 : r.state.len;
    r.state.caret = at;
    if (!ch) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_SELECT_ALL) {
    r.state.anchor = 0;
    r.state.caret = r.state.len;
    return r;
  }

  if (ev == ENTRY_EV_SET_CARET) {
    int at = ch;
    if (at < 0) at = 0;
    if (at > r.state.len) at = r.state.len;
    r.state.caret = at;
    if (!rowKey) r.state.anchor = at;
    return r;
  }

  if (ev == ENTRY_EV_ENTER || ev == ENTRY_EV_CLICKAWAY) {
    // Still showing its prefill said nothing: keep the row's exact value.
    if (entry_text_is_prefill(s)) { r.state = entry_begin(); return r; }
    bool ok = false;
    const int v = entry_parse(s.text, s.kind, s.lo, s.hi, &ok);
    r.state = entry_begin();
    if (ok) { r.commit = 1; r.value = v; }
    return r;
  }

  return r;
}
