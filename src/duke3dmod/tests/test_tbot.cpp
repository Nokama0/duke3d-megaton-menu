#include "test_common.h"
#include "../tbot.h"
#include "../config.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// The triggerbot's pure core.
void tb_0107_triggerbot_core() {
  CHECK(tbot_when_clamped(0) == TBOT_WHEN_CROSSHAIR && tbot_when_clamped(1) == TBOT_WHEN_AIMBOT);
  CHECK(tbot_when_clamped(7) == TBOT_WHEN_CROSSHAIR && tbot_when_clamped(-1) == TBOT_WHEN_CROSSHAIR);
  CHECK(tbot_delay_ms_clamped(-5) == 0 && tbot_delay_ms_clamped(501) == 500 && tbot_delay_ms_clamped(137) == 137);
  CHECK(tbot_delay_tics(0) == 0);
  CHECK(tbot_delay_tics(16) == 0);      // 0.48 tics rounds down
  CHECK(tbot_delay_tics(17) == 1);      // 0.51 tics rounds up
  CHECK(tbot_delay_tics(33) == 1 && tbot_delay_tics(34) == 1);
  CHECK(tbot_delay_tics(50) == 2);      // 1.5 rounds up
  CHECK(tbot_delay_tics(500) == 15 && tbot_delay_tics(9999) == 15);

  CHECK(!tbot_when_crosshair(-1, true, true, 100, 4096).holds && streq(tbot_when_crosshair(-1, true, true, 100, 4096).reason, "nohit"));
  CHECK(!tbot_when_crosshair(5, false, true, 100, 4096).holds && streq(tbot_when_crosshair(5, false, true, 100, 4096).reason, "notenemy"));
  CHECK(!tbot_when_crosshair(5, true, false, 100, 4096).holds && streq(tbot_when_crosshair(5, true, false, 100, 4096).reason, "notenemy"));
  CHECK(!tbot_when_crosshair(5, true, true, 4097, 4096).holds && streq(tbot_when_crosshair(5, true, true, 4097, 4096).reason, "range"));
  CHECK( tbot_when_crosshair(5, true, true, 4096, 4096).holds && streq(tbot_when_crosshair(5, true, true, 4096, 4096).reason, "hit"));
  // Aimbot condition: the shot ray must land on a live/choking enemy so aimbot mode stops pulling on a lock the ray no
  // longer reaches. The ray is cast in every case; terms are (target, los, enemy, breakable).
  CHECK(!tbot_when_aimbot(-1, true, true, false).holds && streq(tbot_when_aimbot(-1, true, true, false).reason, "nolock"));
  CHECK(!tbot_when_aimbot(3, false, true, false).holds);
  CHECK( tbot_when_aimbot(3, true, true, false).holds && streq(tbot_when_aimbot(3, true, true, false).reason, "lock"));
  // Ray lands on nothing live while steering: "blocked", not "nolock".
  CHECK(!tbot_when_aimbot(3, true, false, false).holds && streq(tbot_when_aimbot(3, true, false, false).reason, "blocked"));
  // Target test comes first: no target reports "nolock" even with the ray
  // term also false, matching tbot_when_crosshair/tbot_decide ordering.
  CHECK(!tbot_when_aimbot(-1, true, false, false).holds && streq(tbot_when_aimbot(-1, true, false, false).reason, "nolock"));
  CHECK(!tbot_when_aimbot(-1, false, false, false).holds && streq(tbot_when_aimbot(-1, false, false, false).reason, "nolock"));
  CHECK(!tbot_when_aimbot(3, false, false, false).holds && streq(tbot_when_aimbot(3, false, false, false).reason, "nolock"));
  // No "not steering" exemption: the caller casts the shot's own ray in every case, so a force field (which cansee sees
  // through) stops the ray and the pull whoever is aiming.
  CHECK(!tbot_when_aimbot(3, true, false, false).holds && streq(tbot_when_aimbot(3, true, false, false).reason, "blocked"));
  CHECK( tbot_when_aimbot(3, true, true, false).holds && streq(tbot_when_aimbot(3, true, true, false).reason, "lock"));

  // Fifth term `rayHitsBreakable`: the triggerbot refused a shot at a target behind a window/vent the engine breaks
  // open on the very hit tested. Steering, no enemy, ray on a breakable obstacle: holds, reason "breaking".
  CHECK( tbot_when_aimbot(3, true, false, true).holds && streq(tbot_when_aimbot(3, true, false, true).reason, "breaking"));
  // Enemy term wins when both are true: "lock", not "breaking".
  CHECK( tbot_when_aimbot(3, true, true, true).holds && streq(tbot_when_aimbot(3, true, true, true).reason, "lock"));
  CHECK( tbot_when_aimbot(3, true, false, true).holds && streq(tbot_when_aimbot(3, true, false, true).reason, "breaking"));
  CHECK(!tbot_when_aimbot(-1, true, true, true).holds && streq(tbot_when_aimbot(-1, true, true, true).reason, "nolock"));
  CHECK(!tbot_when_aimbot(3, false, false, true).holds && streq(tbot_when_aimbot(3, false, false, true).reason, "nolock"));
  CHECK(!tbot_when_aimbot(3, true, false, false).holds && streq(tbot_when_aimbot(3, true, false, false).reason, "blocked"));

  // tbot_wall_breakable/tbot_sprite_breakable (tbot.h) reproduce checkhitwall (VA 0x0050fcd0) and checkhitsprite (VA
  // 0x00510ec0) verbatim; see docs/re/breakable-obstacles.md.
  {
    // Three tiles the engine's switch opens: 0x1f7 (glass), 0x197 (fan),
    // 0x1fe (coloured glass), given hittable cstat, a neighbour, and a gap.
    CHECK( tbot_wall_breakable(0x10, 0x1f7, 5, 0, 100, -100));
    CHECK( tbot_wall_breakable(0x10, 0x197, 5, 0, 100, -100));
    CHECK( tbot_wall_breakable(0x10, 0x1fe, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1f6, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1f8, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x196, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x198, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1fd, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1ff, 5, 0, 100, -100));
    // cstat 0 with an overpicnum other than the spark tile 0xe6 refuses
    // before any other term is consulted.
    CHECK(!tbot_wall_breakable(0x00, 0x1f7, 5, 0, 100, -100));
    // overpicnum 0xe6 (force-field spark) satisfies the hittable OR on cstat 0 but is none of the three case values: a
    // spark that opens nothing.
    CHECK(!tbot_wall_breakable(0x00, 0xe6, 5, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1f7, -1, 0, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1f7, 5, 0, 100, 100));
    // Hit at or below the next sector's floor: hz < floorz is strict.
    CHECK(!tbot_wall_breakable(0x10, 0x1f7, 5, 100, 100, -100));
    CHECK(!tbot_wall_breakable(0x10, 0x1f7, 5, 200, 100, -100));
    CHECK( tbot_wall_breakable(0x10, 0x1f7, 5, 99, 100, -100));

    // checkhitsprite's own list, every member true: a picnum is in when a player hitscan can reach
    // deletesprite(param_1) or clear the sprite's blocking/hittable bits. Runtime gates (damage, hit points, a roll) do
    // not exclude; 0xa1e hides via xrepeat = 0, dropping it from the half-width test: in by effect.
    static const int kMembers[] = {
      0x156, 0x157, 0x197, 0x204, 0x205, 0x227, 0x22c, 0x22d, 0x237, 0x239,
      0x23b, 0x253, 0x25a, 0x25f, 0x29d, 0x2a6, 0x2a8, 0x2ad, 0x2ae, 0x2b1,
      0x2b6, 0x2b7, 0x2b9, 0x2cc, 0x2f1, 0x2fd, 0x300, 0x301, 0x306, 0x30b,
      0x318, 0x365, 0x385, 0x386, 0x3ba, 0x3bb, 0x3bc, 0x3bd, 0x3cb, 0x3cc,
      0x3cd, 0x3ce, 0x3cf, 0x3d3, 0x3eb, 0x3f4, 0x3f5, 0x3f6, 0x401, 0x459,
      0x485, 0x486, 0x487, 0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d, 0x48e,
      0x548, 0x549, 0x54a, 0x54b, 0x54f, 0xa1e, 0x1003, 0x115c, 0x1171, 0x134f,
    };
    for (size_t i = 0; i < sizeof(kMembers) / sizeof(kMembers[0]); i++)
      CHECK(tbot_sprite_breakable(kMembers[i]));
    // Every non-member in the table's range (0x150-0x1360, highest member 0x134f) is false.
    for (int p = 0x150; p <= 0x1360; p++) {
      bool isMember = false;
      for (size_t i = 0; i < sizeof(kMembers) / sizeof(kMembers[0]); i++)
        if (kMembers[i] == p) { isMember = true; break; }
      CHECK(tbot_sprite_breakable(p) == isMember);
    }
    CHECK(!tbot_sprite_breakable(0x155)); CHECK(!tbot_sprite_breakable(0x158)); // 0x156-0x157
    CHECK(!tbot_sprite_breakable(0x196)); CHECK(!tbot_sprite_breakable(0x198)); // 0x197
    CHECK(!tbot_sprite_breakable(0x203)); CHECK(!tbot_sprite_breakable(0x206)); // 0x204-0x205
    CHECK(!tbot_sprite_breakable(0x226)); CHECK(!tbot_sprite_breakable(0x228)); // 0x227
    CHECK(!tbot_sprite_breakable(0x22b)); CHECK(!tbot_sprite_breakable(0x22e)); // 0x22c-0x22d
    CHECK(!tbot_sprite_breakable(0x236)); CHECK(!tbot_sprite_breakable(0x238)); // 0x237, 0x239
    CHECK(!tbot_sprite_breakable(0x23a)); CHECK(!tbot_sprite_breakable(0x23c)); // 0x23b
    CHECK(!tbot_sprite_breakable(0x252)); CHECK(!tbot_sprite_breakable(0x254)); // 0x253
    CHECK(!tbot_sprite_breakable(0x259)); CHECK(!tbot_sprite_breakable(0x25b)); // 0x25a
    CHECK(!tbot_sprite_breakable(0x25e)); CHECK(!tbot_sprite_breakable(0x260)); // 0x25f
    CHECK(!tbot_sprite_breakable(0x29c)); CHECK(!tbot_sprite_breakable(0x29e)); // 0x29d
    CHECK(!tbot_sprite_breakable(0x2a5)); CHECK(!tbot_sprite_breakable(0x2a7)); // 0x2a6, 0x2a8
    CHECK(!tbot_sprite_breakable(0x2a9)); CHECK(!tbot_sprite_breakable(0x2ac)); // 0x2a8, 0x2ad
    CHECK(!tbot_sprite_breakable(0x2af)); CHECK(!tbot_sprite_breakable(0x2b0)); // 0x2ae, 0x2b1
    CHECK(!tbot_sprite_breakable(0x2b2)); CHECK(!tbot_sprite_breakable(0x2b5)); // 0x2b1, 0x2b6
    CHECK(!tbot_sprite_breakable(0x2b8)); CHECK(!tbot_sprite_breakable(0x2ba)); // 0x2b7, 0x2b9
    CHECK(!tbot_sprite_breakable(0x2cb)); CHECK(!tbot_sprite_breakable(0x2cd)); // 0x2cc
    CHECK(!tbot_sprite_breakable(0x2f0)); CHECK(!tbot_sprite_breakable(0x2f2)); // 0x2f1
    CHECK(!tbot_sprite_breakable(0x2fc)); CHECK(!tbot_sprite_breakable(0x2fe)); // 0x2fd
    CHECK( tbot_sprite_breakable(0x300)); // 0x300 and 0x301 are adjacent members
    CHECK( tbot_sprite_breakable(0x301)); // -- each other's "neighbour" is true, not false
    CHECK(!tbot_sprite_breakable(0x2ff)); CHECK(!tbot_sprite_breakable(0x302)); // 0x300-0x301
    CHECK(!tbot_sprite_breakable(0x305)); CHECK(!tbot_sprite_breakable(0x307)); // 0x306
    CHECK(!tbot_sprite_breakable(0x30a)); CHECK(!tbot_sprite_breakable(0x30c)); // 0x30b
    CHECK(!tbot_sprite_breakable(0x317)); CHECK(!tbot_sprite_breakable(0x319)); // 0x318
    CHECK(!tbot_sprite_breakable(0x364)); CHECK(!tbot_sprite_breakable(0x366)); // 0x365
    CHECK(!tbot_sprite_breakable(0x384)); CHECK(!tbot_sprite_breakable(0x387)); // 0x385-0x386
    CHECK(!tbot_sprite_breakable(0x3b9)); CHECK(!tbot_sprite_breakable(0x3be)); // 0x3ba-0x3bd
    CHECK(!tbot_sprite_breakable(0x3ca)); CHECK(!tbot_sprite_breakable(0x3d0)); // 0x3cb-0x3cf
    CHECK(!tbot_sprite_breakable(0x3d2)); CHECK(!tbot_sprite_breakable(0x3d4)); // 0x3d3
    CHECK(!tbot_sprite_breakable(0x3ea)); CHECK(!tbot_sprite_breakable(0x3ec)); // 0x3eb
    CHECK(!tbot_sprite_breakable(0x3f3)); CHECK(!tbot_sprite_breakable(0x3f7)); // 0x3f4-0x3f6
    CHECK(!tbot_sprite_breakable(0x400)); CHECK(!tbot_sprite_breakable(0x402)); // 0x401
    CHECK(!tbot_sprite_breakable(0x458)); CHECK(!tbot_sprite_breakable(0x45a)); // 0x459
    CHECK(!tbot_sprite_breakable(0x484)); CHECK(!tbot_sprite_breakable(0x48f)); // 0x485-0x48e
    CHECK(!tbot_sprite_breakable(0x547)); CHECK(!tbot_sprite_breakable(0x54c)); // 0x548-0x54b
    CHECK(!tbot_sprite_breakable(0x54e)); CHECK(!tbot_sprite_breakable(0x550)); // 0x54f
    CHECK(!tbot_sprite_breakable(0xa1d)); CHECK(!tbot_sprite_breakable(0xa1f)); // 0xa1e
    CHECK(!tbot_sprite_breakable(0x1002)); CHECK(!tbot_sprite_breakable(0x1004)); // 0x1003
    CHECK(!tbot_sprite_breakable(0x115b)); CHECK(!tbot_sprite_breakable(0x115d)); // 0x115c
    CHECK(!tbot_sprite_breakable(0x1170)); CHECK(!tbot_sprite_breakable(0x1172)); // 0x1171
    CHECK(!tbot_sprite_breakable(0x134e)); CHECK(!tbot_sprite_breakable(0x1350)); // 0x134f
    // Non-enemy picnums checkhitsprite handles but a hitscan hit does not make passable, pinned false by name: cstat is
    // cleared only when the attacker's picnum is one of 0x659/0x1a/0x3d5/0x686/0xa2d, and the player's hitscan attacker
    // is SHOTSPARK1 (0xa23), not in that set.
    CHECK(!tbot_sprite_breakable(0x38c));
    CHECK(!tbot_sprite_breakable(0x38e));
    CHECK(!tbot_sprite_breakable(0x38f));
    CHECK(!tbot_sprite_breakable(0x3b7));
    CHECK(!tbot_sprite_breakable(0x3de));
    // Tile swap plus a spawn, cstat untouched, sprite still there: 0x268/0x26a/0x26b, 0x3e2-0x3e4, 0x233-0x236, 0x3c9,
    // 0x3d5, 0x54e. The replacement tile is sometimes itself a member, but this hit does not clear the ray, so the
    // current picnum is not breakable.
    CHECK(!tbot_sprite_breakable(0x268));
    CHECK(!tbot_sprite_breakable(0x26a));
    CHECK(!tbot_sprite_breakable(0x26b));
    CHECK(!tbot_sprite_breakable(0x233));
    CHECK(!tbot_sprite_breakable(0x234));
    CHECK(!tbot_sprite_breakable(0x235));
    CHECK(!tbot_sprite_breakable(0x236));
    CHECK(!tbot_sprite_breakable(0x3c9));
    CHECK(!tbot_sprite_breakable(0x3d5));
    CHECK(!tbot_sprite_breakable(0x3e2));
    CHECK(!tbot_sprite_breakable(0x3e3));
    CHECK(!tbot_sprite_breakable(0x3e4));
    CHECK(!tbot_sprite_breakable(0x54e));
    // 0x58c re-points param_1 at its owner into the badguy path; nothing is opened.
    CHECK(!tbot_sprite_breakable(0x58c));
  }

  // shoot's ray: vx = sintable[(ang+512)&2047], vy = sintable[ang&2047], vz = ((100-horiz-horizoff)<<5)<<6.
  short sintable[2048]; for (int i = 0; i < 2048; i++) sintable[i] = (short)floor(16384.0 * sin(i * 3.14159265358979323846 / 1024.0) + 0.5);
  int vx = 0, vy = 0, vz = 0;
  tbot_ray(0, 100, 0, sintable, &vx, &vy, &vz);
  CHECK(vx == 16384 && vy == 0 && vz == 0);                   // ang 0 faces +x, level
  tbot_ray(512, 100, 0, sintable, &vx, &vy, &vz);
  CHECK(vx == 0 && vy == 16384 && vz == 0);                   // ang 512 faces +y
  tbot_ray(1024, 100, 0, sintable, &vx, &vy, &vz);
  CHECK(vx == -16384 && vy == 0);
  tbot_ray(1536, 100, 0, sintable, &vx, &vy, &vz);
  CHECK(vx == 0 && vy == -16384);
  tbot_ray(0, 0, 0, sintable, &vx, &vy, &vz);
  // Engine's `<< 5` then `<< 6` written as multiplications, avoiding a
  // shift of a negative signed value in the pins as in tbot_ray itself.
  CHECK(vz == 100 * 32 * 64);                                 // looking fully up: horiz 0
  tbot_ray(0, 200, 0, sintable, &vx, &vy, &vz);
  CHECK(vz == -100 * 32 * 64);                                // fully down
  tbot_ray(0, 100, 50, sintable, &vx, &vy, &vz);
  CHECK(vz == -50 * 32 * 64);                                 // horizoff adds to the pitch
  tbot_ray(2048 + 512, 100, 0, sintable, &vx, &vy, &vz);
  CHECK(vx == 0 && vy == 16384);                              // ang wraps through & 2047

  const TbotCond hit = { true, "hit" };
  const TbotCond miss = { false, "nohit" };
  TbotState st = { 0, false };
  const char* why = nullptr;
  st.heldTics = 5; st.firing = true;
  CHECK(!tbot_decide(false, false, false, false, false, true, hit, 0, &st, &why) && streq(why, "off") && st.heldTics == 0 && !st.firing);
  st.heldTics = 5; CHECK(!tbot_decide(true, true,  false, false, false, true, hit, 0, &st, &why) && streq(why, "menu")   && st.heldTics == 0);
  st.heldTics = 5; CHECK(!tbot_decide(true, false, true,  false, false, true, hit, 0, &st, &why) && streq(why, "freeze") && st.heldTics == 0);
  st.heldTics = 5; CHECK(!tbot_decide(true, false, false, true,  false, true, hit, 0, &st, &why) && streq(why, "dead")   && st.heldTics == 0);
  st.heldTics = 5; CHECK(!tbot_decide(true, false, false, false, true,  true, hit, 0, &st, &why) && streq(why, "camera") && st.heldTics == 0);
  st.heldTics = 5; CHECK(!tbot_decide(true, false, false, false, false, false, hit, 0, &st, &why) && streq(why, "weapon") && st.heldTics == 0);
  st.heldTics = 5; CHECK(!tbot_decide(true, false, false, false, false, true, miss, 0, &st, &why) && streq(why, "nohit") && st.heldTics == 0);
  st.heldTics = 0; st.firing = false;
  CHECK( tbot_decide(true, false, false, false, false, true, hit, 0, &st, &why) && streq(why, "fire") && st.heldTics == 1 && st.firing);
  st.heldTics = 0; st.firing = false;
  CHECK(!tbot_decide(true, false, false, false, false, true, hit, 1, &st, &why) && streq(why, "delay") && st.heldTics == 1);
  CHECK( tbot_decide(true, false, false, false, false, true, hit, 1, &st, &why) && streq(why, "fire") && st.heldTics == 2);
  // Delay 15 (500 ms) fires on the 16th consecutive tic and not before.
  st.heldTics = 0; st.firing = false;
  { int firedAt = -1; for (int t = 1; t <= 20 && firedAt < 0; t++) if (tbot_decide(true, false, false, false, false, true, hit, 15, &st, &why)) firedAt = t; CHECK(firedAt == 16); }
  st.heldTics = 0; st.firing = false;
  CHECK(!tbot_decide(true, false, false, false, false, true, hit, 2, &st, &why));
  CHECK(!tbot_decide(true, false, false, false, false, true, miss, 2, &st, &why) && st.heldTics == 0 && !st.firing);
  CHECK(!tbot_decide(true, false, false, false, false, true, hit, 2, &st, &why) && st.heldTics == 1);
  // heldTics saturates rather than wrapping.
  st.heldTics = TBOT_HELD_TICS_CAP; st.firing = true;
  CHECK( tbot_decide(true, false, false, false, false, true, hit, 0, &st, &why) && st.heldTics == TBOT_HELD_TICS_CAP);
  // A null state or reason is refused, never dereferenced.
  CHECK(!tbot_decide(true, false, false, false, false, true, hit, 0, nullptr, &why));
  CHECK( tbot_decide(true, false, false, false, false, true, hit, 0, &st, nullptr));

  // Exhaustive over all six refusal flags plus the condition: the first refusal in order wins; pins priority as
  // test_target.cpp does for aim_should_run.
  {
    bool sawOff = false, sawMenu = false, sawFreeze = false, sawDead = false,
         sawCamera = false, sawWeapon = false, sawCondReason = false, sawFire = false;
    for (int onI = 0; onI < 2; onI++)
    for (int menuI = 0; menuI < 2; menuI++)
    for (int freezeI = 0; freezeI < 2; freezeI++)
    for (int deadI = 0; deadI < 2; deadI++)
    for (int camI = 0; camI < 2; camI++)
    for (int weapI = 0; weapI < 2; weapI++)
    for (int condI = 0; condI < 2; condI++) {
      const bool onOk = onI != 0, menuOpenX = menuI != 0, freezeX = freezeI != 0,
                 deadX = deadI != 0, camX = camI != 0, weapOk = weapI != 0;
      const TbotCond c = condI != 0 ? hit : miss;
      TbotState s = { 0, false };
      const char* r = nullptr;
      const bool fired = tbot_decide(onOk, menuOpenX, freezeX, deadX, camX, weapOk, c, 0, &s, &r);
      const char* expect = nullptr;
      if (!onOk) expect = "off";
      else if (menuOpenX) expect = "menu";
      else if (freezeX) expect = "freeze";
      else if (deadX) expect = "dead";
      else if (camX) expect = "camera";
      else if (!weapOk) expect = "weapon";
      else if (!c.holds) expect = c.reason;
      if (expect) {
        CHECK(!fired && streq(r, expect) && s.heldTics == 0 && !s.firing);
        if (streq(expect, "off")) sawOff = true;
        else if (streq(expect, "menu")) sawMenu = true;
        else if (streq(expect, "freeze")) sawFreeze = true;
        else if (streq(expect, "dead")) sawDead = true;
        else if (streq(expect, "camera")) sawCamera = true;
        else if (streq(expect, "weapon")) sawWeapon = true;
        else sawCondReason = true;
      } else {
        CHECK(fired && streq(r, "fire") && s.heldTics == 1 && s.firing);
        sawFire = true;
      }
    }
    CHECK(sawOff && sawMenu && sawFreeze && sawDead && sawCamera && sawWeapon && sawCondReason && sawFire);
  }
}

void tb_0108_triggerbot_config() {
  config_load(tb_path("Z:|does|not|exist.ini"));
  CHECK(g_cfg.tboton == 0 && g_cfg.tbotwhen == 0 && g_cfg.tbotdelay == 0);
  FILE* f = nullptr;
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\ntboton=1\ntbotwhen=1\ntbotdelay=137\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.tboton == 1 && g_cfg.tbotwhen == 1 && g_cfg.tbotdelay == 137);
  fopen_s(&f, tmp, "w");
  fputs("[nukepanel]\ntboton=5\ntbotwhen=7\ntbotdelay=9999\n", f);
  fclose(f);
  config_load(tmp);
  CHECK(g_cfg.tboton == 1 && g_cfg.tbotwhen == 0 && g_cfg.tbotdelay == 500);   // clamped, not passed through
  CHECK(config_save(tmp));
  { char buf[4096] = { 0 }; fopen_s(&f, tmp, "r"); const size_t n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); (void)n;
    CHECK(strstr(buf, "tboton=1\n") != nullptr && strstr(buf, "tbotwhen=0\n") != nullptr && strstr(buf, "tbotdelay=500\n") != nullptr); }
  bool found = false;
  for (int i = 0; i < config_switch_count(); i++) if (streq(config_switch_name(i), "tboton")) found = true;
  CHECK(found);
  config_load(tb_path("Z:|does|not|exist.ini"));   // leave the defaults behind for later units
}
