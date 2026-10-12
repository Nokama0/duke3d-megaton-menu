#include "test_common.h"
#include "test_blocks.h"
#include "../autosave.h"

// Autosave: the whole "is a save due now" rule (autosave.cpp is DLL-only, it calls the game's dnSaveGame).
void tb_0130_autosave_rule() {
  const unsigned SIG_A = 0x1111u, SIG_B = 0x2222u;
  const unsigned MIN = AUTOSAVE_MS_PER_MIN;

  // Defaults: off, last save slot.
  CHECK(AUTOSAVE_MINS_DEFAULT == 0 && AUTOSAVE_MINS_MAX == 30);
  CHECK(AUTOSAVE_SLOT_DEFAULT == 9 && AUTOSAVE_SLOT_MIN == 0 && AUTOSAVE_SLOT_MAX == 9);
  CHECK(autosave_mins_clamped(-5) == 0 && autosave_mins_clamped(31) == 30 && autosave_mins_clamped(7) == 7);
  CHECK(autosave_slot_clamped(-1) == 0 && autosave_slot_clamped(10) == 9 && autosave_slot_clamped(3) == 3);

  // A null state is not a save.
  CHECK(autosave_step(0, 5, 1000, SIG_A, 1) == 0);

  // Off never saves.
  {
    AutosaveState s = { 0, 0, 0 };
    CHECK(autosave_step(&s, 0, 1000, SIG_A, 1) == 0);   // arming tic
    for (int i = 1; i <= 60; i++)
      CHECK(autosave_step(&s, 0, 1000 + (unsigned)i * MIN, SIG_A, 1) == 0);
  }

  // The first tic in a level arms the timer: no save at level start.
  {
    AutosaveState s = { 0, 0, 0 };
    CHECK(autosave_step(&s, 1, 5000, SIG_A, 1) == 0);
    CHECK(s.armed == 1 && s.levelSig == SIG_A && s.lastMs == 5000);
    CHECK(autosave_step(&s, 1, 5000 + MIN - 1, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 1, 5000 + MIN, SIG_A, 1) == 1);
    CHECK(s.lastMs == 5000 + MIN);                       // a due save restarts the clock
    CHECK(autosave_step(&s, 1, 5000 + MIN + 1, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 1, 5000 + 2 * MIN, SIG_A, 1) == 1);
  }

  // A level change restarts the timer and does not save on that tic.
  {
    AutosaveState s = { 0, 0, 0 };
    CHECK(autosave_step(&s, 1, 1000, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 1, 1000 + MIN - 10, SIG_B, 1) == 0);   // new map, nearly due
    CHECK(s.levelSig == SIG_B && s.lastMs == 1000 + MIN - 10);
    CHECK(autosave_step(&s, 1, 1000 + MIN, SIG_B, 1) == 0);        // old deadline is gone
    CHECK(autosave_step(&s, 1, 1000 + 2 * MIN - 10, SIG_B, 1) == 1);
  }

  // Out of play (paused, menus, camera, dead) never saves but the clock runs on: the save lands on the first in-play
  // tic past the interval.
  {
    AutosaveState s = { 0, 0, 0 };
    CHECK(autosave_step(&s, 2, 0, SIG_A, 1) == 0);
    for (int i = 1; i <= 4; i++)
      CHECK(autosave_step(&s, 2, (unsigned)i * MIN, SIG_A, 0) == 0);
    CHECK(autosave_step(&s, 2, 4 * MIN, SIG_A, 1) == 1);
  }

  for (int n = 1; n <= AUTOSAVE_MINS_MAX; n++) {
    AutosaveState s = { 0, 0, 0 };
    autosave_step(&s, n, 100, SIG_A, 1);
    CHECK(autosave_step(&s, n, 100 + (unsigned)n * MIN - 1, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, n, 100 + (unsigned)n * MIN, SIG_A, 1) == 1);
  }

  // The wallclock wraps; unsigned subtraction spans it, so a save straddling the wrap is on time.
  {
    AutosaveState s = { 0, 0, 0 };
    const unsigned late = 0xFFFFFF00u;
    CHECK(autosave_step(&s, 1, late, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 1, late + MIN - 1, SIG_A, 1) == 0);    // wrapped past 0 already
    CHECK(autosave_step(&s, 1, late + MIN, SIG_A, 1) == 1);
  }

  // An absurd hand-edited interval cannot overflow the deadline: the rule clamps like the loader.
  {
    AutosaveState s = { 0, 0, 0 };
    CHECK(autosave_step(&s, 100000, 0, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 100000, AUTOSAVE_MINS_MAX * MIN - 1, SIG_A, 1) == 0);
    CHECK(autosave_step(&s, 100000, AUTOSAVE_MINS_MAX * MIN, SIG_A, 1) == 1);
  }

  // The two ini keys: defaults, both clamps, round trip (autosaveslot has no row, so the ini is its only setter).
  {
    const Config savedCfg = g_cfg;
    config_load(tb_path("Z:|does|not|exist.ini"));
    CHECK(g_cfg.autosavemins == AUTOSAVE_MINS_DEFAULT && g_cfg.autosavemins == 0);
    CHECK(g_cfg.autosaveslot == AUTOSAVE_SLOT_DEFAULT);
    fopen_s(&f, tmp, "w");
    fputs("[nukepanel]\nautosavemins=999\nautosaveslot=42\n", f);
    fclose(f);
    config_load(tmp);
    CHECK(g_cfg.autosavemins == AUTOSAVE_MINS_MAX && g_cfg.autosaveslot == AUTOSAVE_SLOT_MAX);
    CHECK(config_save(tmp));
    config_load(tb_path("Z:|does|not|exist.ini"));   // poisoned before the reload
    config_load(tmp);
    CHECK(g_cfg.autosavemins == AUTOSAVE_MINS_MAX && g_cfg.autosaveslot == AUTOSAVE_SLOT_MAX);
    fopen_s(&f, tmp, "w");
    fputs("[nukepanel]\nautosavemins=-5\nautosaveslot=-1\n", f);
    fclose(f);
    config_load(tmp);
    CHECK(g_cfg.autosavemins == AUTOSAVE_MINS_MIN && g_cfg.autosaveslot == AUTOSAVE_SLOT_MIN);
    remove(tmp);
    g_cfg = savedCfg;
  }

  // The Settings row drives autosavemins and reaches both ends of its range.
  {
    const int savedMins = g_cfg.autosavemins;
    g_cfg.autosavemins = 0;
    CHECK(slider_row_reaches_ends(TAB_SETTINGS, "Autosave", 3440, 1440,
                                  AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX, &g_cfg.autosavemins, 0));
    g_cfg.autosavemins = savedMins;
  }
}
