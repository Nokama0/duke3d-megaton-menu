#include "test_common.h"
#include "test_blocks.h"
#include "../secretpath.h"
#include "../trigger.h"

// Path to a secret: the search, on fixtures built by hand so every refusal below is a decision of spath_search's own and
// not a property of some level.
void tb_0135_secret_path() {
  // The constants, as literals so a moved one takes its own check with it.
  CHECK(SPATH_MAX_SECTORS == 1024 && SPATH_MAX_SECTORS < rva::MAX_SECTORS);
  CHECK(SPATH_MAX_HOPS == 64);
  CHECK(SPATH_STEP_UP == 0x1800);          // one step
  CHECK(SPATH_MIN_GAP == 0x2800);          // a crouching player
  CHECK(SPATH_STEP_UP < SPATH_MIN_GAP);
  CHECK(SPATH_EDGE_WALK == 0 && SPATH_EDGE_TELEPORT == 1);
  CHECK(TRIG_SECRET_LOTAG == 32767);

  // The door tag list: 20-23 and 25-31, and nothing else under 64.
  {
    int doors = 0;
    for (int lotag = 0; lotag < 64; lotag++) doors += spath_lotag_is_door(lotag);
    CHECK(doors == 11);
    CHECK(spath_lotag_is_door(20) && spath_lotag_is_door(23));
    CHECK(!spath_lotag_is_door(24));
    CHECK(spath_lotag_is_door(25) && spath_lotag_is_door(31));
    CHECK(!spath_lotag_is_door(19) && !spath_lotag_is_door(32));
    CHECK(!spath_lotag_is_door(0) && !spath_lotag_is_door(1) && !spath_lotag_is_door(2));
    CHECK(!spath_lotag_is_door(17));                 // a platform keeps its own gap
    CHECK(!spath_lotag_is_door(TRIG_SECRET_LOTAG));
    CHECK(!spath_lotag_is_door(-1));
  }

  // The edge rule on its own. Build z grows downward: floorz is the larger number.
  {
    const SPathSector room = { 0, -0x4000, 0 };
    const SPathSector stepOk = { -SPATH_STEP_UP, -0x4000 - SPATH_STEP_UP, 0 };
    const SPathSector stepHigh = { -SPATH_STEP_UP - 1, -0x4000, 0 };
    const SPathSector tight = { 0, -SPATH_MIN_GAP + 1, 0 };
    const SPathSector exact = { 0, -SPATH_MIN_GAP, 0 };
    const SPathSector shut = { 0, 0, 0 };                  // a closed door
    CHECK(spath_edge_passable(room, room, 0) == 1);
    CHECK(spath_edge_passable(room, stepOk, 0) == 1);      // exactly one step
    CHECK(spath_edge_passable(room, stepHigh, 0) == 0);
    CHECK(spath_edge_passable(room, exact, 0) == 1);       // exactly a crouch
    CHECK(spath_edge_passable(room, tight, 0) == 0);
    CHECK(spath_edge_passable(room, shut, 0) == 0);
    CHECK(spath_edge_passable(room, shut, 1) == 1);
    CHECK(spath_edge_passable(room, tight, 1) == 1);
    // A door's GAP is forgiven, its STEP is not: a raised floor door reads as a wall, which the hint cannot know.
    CHECK(spath_edge_passable(room, stepHigh, 1) == 0);
    // A drop is free however far it falls (and one-way in the level, which the search cannot see either).
    const SPathSector below = { 0x40000, 0x40000 - 0x4000, 0 };
    CHECK(spath_edge_passable(room, below, 0) == 1);
    // Extremes: a hand-built fixture reaches these, and neither may overflow into the opposite answer.
    const SPathSector low = { -2147483647 - 1, -2147483647 - 1, 0 };
    const SPathSector high = { 2147483647, -2147483647 - 1, 0 };
    CHECK(spath_edge_passable(room, low, 0) == 0);
    CHECK(spath_edge_passable(room, high, 0) == 1);
  }

  const SPathSector flat = { 0, -0x4000, 0 };

  // A straight corridor of four sectors finds the far one in three hops.
  {
    SPathSector sec[4] = { flat, flat, flat, flat };
    const SPathEdge edges[6] = {
      { 0, 1, SPATH_EDGE_WALK, 100, 0, 0 }, { 1, 0, SPATH_EDGE_WALK, 100, 0, 0 },
      { 1, 2, SPATH_EDGE_WALK, 200, 0, 0 }, { 2, 1, SPATH_EDGE_WALK, 200, 0, 0 },
      { 2, 3, SPATH_EDGE_WALK, 300, 0, 0 }, { 3, 2, SPATH_EDGE_WALK, 300, 0, 0 },
    };
    const unsigned char isDoor[4] = { 0, 0, 0, 0 };
    unsigned char isGoal[4] = { 0, 0, 0, 1 };
    SPathEdge out[8];
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, out, 8) == 3);
    CHECK(out[0].from == 0 && out[0].to == 1 && out[0].x == 100);
    CHECK(out[1].from == 1 && out[1].to == 2 && out[1].x == 200);
    CHECK(out[2].from == 2 && out[2].to == 3 && out[2].x == 300);
    CHECK(out[0].kind == SPATH_EDGE_WALK);

    isGoal[2] = 1;
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, out, 8) == 2);
    CHECK(out[1].to == 2);
    isGoal[2] = 0;

    isGoal[3] = 0;
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, out, 8) == 0);
    isGoal[3] = 1;

    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 3, out, 8) == 0);

    // outCap smaller than the path truncates to the NEAR end and writes nothing past the array.
    out[1].from = -99; out[1].to = -99;
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, out, 1) == 3);
    CHECK(out[0].from == 0 && out[0].to == 1);
    CHECK(out[1].from == -99 && out[1].to == -99);

    CHECK(spath_search(0, 6, sec, 4, isDoor, isGoal, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, 0, 4, isDoor, isGoal, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, 0, isGoal, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, isDoor, 0, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, 0, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, -1, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 4, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 0, isDoor, isGoal, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, SPATH_MAX_SECTORS + 1, isDoor, isGoal, 0, out, 8) == 0);
    CHECK(spath_search(edges, 6, sec, 4, isDoor, isGoal, 0, out, 0) == 0);
    CHECK(spath_search(edges, -1, sec, 4, isDoor, isGoal, 0, out, 8) == 0);
    const SPathEdge bad[2] = { { 0, 99, SPATH_EDGE_WALK, 0, 0, 0 },
                               { 0, -1, SPATH_EDGE_WALK, 0, 0, 0 } };
    CHECK(spath_search(bad, 2, sec, 4, isDoor, isGoal, 0, out, 8) == 0);
  }

  // A step too high is refused and the search takes the longer way round: 0 -> 1 -> 4 is two hops but 1's floor is one
  // unit past a step; 0 -> 2 -> 3 -> 4 is three flat hops.
  {
    SPathSector sec[5] = { flat, flat, flat, flat, flat };
    sec[1].floorz = -SPATH_STEP_UP - 1;
    const SPathEdge edges[8] = {
      { 0, 1, SPATH_EDGE_WALK, 10, 0, 0 }, { 1, 4, SPATH_EDGE_WALK, 11, 0, 0 },
      { 0, 2, SPATH_EDGE_WALK, 20, 0, 0 }, { 2, 3, SPATH_EDGE_WALK, 21, 0, 0 },
      { 3, 4, SPATH_EDGE_WALK, 22, 0, 0 }, { 2, 0, SPATH_EDGE_WALK, 20, 0, 0 },
      { 3, 2, SPATH_EDGE_WALK, 21, 0, 0 }, { 4, 3, SPATH_EDGE_WALK, 22, 0, 0 },
    };
    const unsigned char isDoor[5] = { 0, 0, 0, 0, 0 };
    const unsigned char isGoal[5] = { 0, 0, 0, 0, 1 };
    SPathEdge out[8];
    CHECK(spath_search(edges, 8, sec, 5, isDoor, isGoal, 0, out, 8) == 3);
    CHECK(out[0].to == 2 && out[1].to == 3 && out[2].to == 4);
    // With 1 a step lower the short way wins, so the step decided the check above.
    sec[1].floorz = -SPATH_STEP_UP;
    CHECK(spath_search(edges, 8, sec, 5, isDoor, isGoal, 0, out, 8) == 2);
    CHECK(out[0].to == 1 && out[1].to == 4);

    // A gap too small does the same (sector 1's ceiling one unit inside a crouch).
    sec[1].floorz = 0;
    sec[1].ceilingz = -SPATH_MIN_GAP + 1;
    CHECK(spath_search(edges, 8, sec, 5, isDoor, isGoal, 0, out, 8) == 3);
    CHECK(out[0].to == 2);
    const unsigned char doors[5] = { 0, 1, 0, 0, 0 };
    CHECK(spath_search(edges, 8, sec, 5, doors, isGoal, 0, out, 8) == 2);
    CHECK(out[0].to == 1);
    sec[1].ceilingz = 0;
    CHECK(spath_search(edges, 8, sec, 5, doors, isGoal, 0, out, 8) == 2);
    CHECK(spath_search(edges, 8, sec, 5, isDoor, isGoal, 0, out, 8) == 3);
  }

  // A teleport edge is taken and counts as one hop; sector 1 is shut solid and a level above 0.
  {
    SPathSector sec[2] = { flat, { -0x40000, -0x40000, 0 } };
    const SPathEdge walk = { 0, 1, SPATH_EDGE_WALK, 5, 6, 7 };
    const SPathEdge tele = { 0, 1, SPATH_EDGE_TELEPORT, 5, 6, 7 };
    const unsigned char isDoor[2] = { 0, 0 };
    const unsigned char isGoal[2] = { 0, 1 };
    SPathEdge out[4];
    CHECK(spath_search(&walk, 1, sec, 2, isDoor, isGoal, 0, out, 4) == 0);
    CHECK(spath_search(&tele, 1, sec, 2, isDoor, isGoal, 0, out, 4) == 1);
    CHECK(out[0].kind == SPATH_EDGE_TELEPORT && out[0].x == 5 && out[0].y == 6 && out[0].z == 7);
  }

  // A cycle terminates: three rooms round a ring, with and without a goal.
  {
    SPathSector sec[4] = { flat, flat, flat, flat };
    const SPathEdge edges[5] = {
      { 0, 1, SPATH_EDGE_WALK, 1, 0, 0 }, { 1, 2, SPATH_EDGE_WALK, 2, 0, 0 },
      { 2, 0, SPATH_EDGE_WALK, 3, 0, 0 }, { 1, 0, SPATH_EDGE_WALK, 1, 0, 0 },
      { 2, 3, SPATH_EDGE_WALK, 4, 0, 0 },
    };
    const unsigned char isDoor[4] = { 0, 0, 0, 0 };
    unsigned char isGoal[4] = { 0, 0, 0, 1 };
    SPathEdge out[8];
    CHECK(spath_search(edges, 5, sec, 4, isDoor, isGoal, 0, out, 8) == 3);
    CHECK(out[0].to == 1 && out[1].to == 2 && out[2].to == 3);
    isGoal[3] = 0;
    CHECK(spath_search(edges, 5, sec, 4, isDoor, isGoal, 0, out, 8) == 0);
  }

  // The rescan gate: a first run, a level change, a room change, then the period. Nothing else may fire it.
  {
    CHECK(spath_rescan_due(0, 7u, 7u, 3, 3, 1000u, 1000u, TRIG_SCAN_PERIOD_MS) == 1);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 1000u, 1000u, TRIG_SCAN_PERIOD_MS) == 0);
    CHECK(spath_rescan_due(1, 8u, 7u, 3, 3, 1000u, 1000u, TRIG_SCAN_PERIOD_MS) == 1);
    CHECK(spath_rescan_due(1, 7u, 7u, 4, 3, 1000u, 1000u, TRIG_SCAN_PERIOD_MS) == 1);
    // The period is inclusive at its own edge: 999 ms elapsed is not due and 1000 is.
    CHECK(TRIG_SCAN_PERIOD_MS == 1000u);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 1999u, 1000u, TRIG_SCAN_PERIOD_MS) == 0);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 2000u, 1000u, TRIG_SCAN_PERIOD_MS) == 1);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 5000u, 1000u, TRIG_SCAN_PERIOD_MS) == 1);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 1500u, 1000u, 1000u) == 0);
    // Across the wall clock's wrap: unsigned subtraction, so 4294967295 -> 200 is 201 ms.
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 200u, 4294967295u, 1000u) == 0);
    CHECK(spath_rescan_due(1, 7u, 7u, 3, 3, 1200u, 4294967295u, 1000u) == 1);
  }

  CHECK(spath_count_clamped(-1) == 0);
  CHECK(spath_count_clamped(0) == 0);
  CHECK(spath_count_clamped(7) == 7);
  CHECK(spath_count_clamped(SPATH_MAX_HOPS) == SPATH_MAX_HOPS);
  CHECK(spath_count_clamped(SPATH_MAX_HOPS + 1) == SPATH_MAX_HOPS);
  CHECK(spath_count_clamped(2147483647) == SPATH_MAX_HOPS);
  CHECK(!strcmp(spath_room_word(1), "room"));
  CHECK(!strcmp(spath_room_word(0), "rooms"));
  CHECK(!strcmp(spath_room_word(2), "rooms"));
  // The whole label at the widest hop count stays inside the panel's 63-character hint budget.
  {
    char lab[64];
    _snprintf_s(lab, sizeof(lab), _TRUNCATE, "Secret: %d %s",
                SPATH_MAX_HOPS, spath_room_word(SPATH_MAX_HOPS));
    CHECK(strlen(lab) < 63);
    CHECK(!strcmp(lab, "Secret: 64 rooms"));
  }

  // The snapshot's point array is the search's own cap, so a published path can never name a point it cannot hold.

  {
    SPathSnapshot s;
    CHECK((int)(sizeof(s.p) / sizeof(s.p[0])) == SPATH_MAX_HOPS);
  }
}
