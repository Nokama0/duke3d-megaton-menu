// Test-build-only substitutes for symbols menu.cpp needs from DLL-only units. Must not define any canvas_* function:
// canvas.cpp is already compiled in (with /DNO_GL).
#include "input.h"
#include "wallclock.h"

UiInput g_ui = { 0, 0, false, false, false, false };

// input_menu_open() is TRUE by default (row-click tests assume an open panel) and settable; a test that changes it restores it.
static bool s_stubMenuOpen = true;
void menu_stub_set_open(bool open) { s_stubMenuOpen = open; }
bool input_menu_open() { return s_stubMenuOpen; }

unsigned wallclock_ms() { return 0; }
