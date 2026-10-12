#include "test_common.h"
#include "../shot.h"

// Screenshot mode. Every decision the DLL half makes is a pure rule in shot.h: activation, edges, the view state
// machine, hook behaviour and the four thunk fingerprints.

void tb_0083_phase_6_screenshot_rules() {
  CHECK(!shot_active(false, false, false) && !shot_active(false, true, false) &&
        !shot_active(true, false, false) && shot_active(true, true, false));
  CHECK(!shot_active(true, true, true));        // the game's menu releases it
  CHECK(shot_edge(false, false) == SHOT_EDGE_NONE && shot_edge(true, true) == SHOT_EDGE_NONE);
  CHECK(shot_edge(false, true) == SHOT_EDGE_ON && shot_edge(true, false) == SHOT_EDGE_OFF);
  ShotView v = { false, 0 };
  long w = -1;
  CHECK(shot_view_step(&v, SHOT_EDGE_NONE, 8, &w) == SHOT_ACT_NONE && !v.held);
  CHECK(shot_view_step(&v, SHOT_EDGE_OFF, 8, &w) == SHOT_ACT_NONE);        // never activated: restores nothing
  CHECK(shot_view_step(&v, SHOT_EDGE_ON, 8, &w) == SHOT_ACT_WRITE_ZERO_AND_VSCRN);
  CHECK(v.held && v.savedSize == 8 && w == 0);
  CHECK(shot_view_step(&v, SHOT_EDGE_NONE, 0, &w) == SHOT_ACT_NONE);       // steady: no rewrite
  CHECK(shot_view_step(&v, SHOT_EDGE_ON, 0, &w) == SHOT_ACT_NONE);         // a repeated ON edge cannot overwrite the save
  CHECK(v.savedSize == 8);
  CHECK(shot_view_step(&v, SHOT_EDGE_OFF, 0, &w) == SHOT_ACT_RESTORE_AND_VSCRN);
  CHECK(!v.held && w == 8);
  CHECK(shot_view_step(&v, SHOT_EDGE_OFF, 8, &w) == SHOT_ACT_NONE);        // a second OFF restores nothing
  // A size the game changed while held is not what we restore: the saved one is.
  CHECK(shot_view_step(&v, SHOT_EDGE_ON, 4, &w) == SHOT_ACT_WRITE_ZERO_AND_VSCRN && v.savedSize == 4);
  CHECK(shot_view_step(&v, SHOT_EDGE_OFF, 12, &w) == SHOT_ACT_RESTORE_AND_VSCRN && w == 4);
  CHECK(shot_hides_overlays(true) && !shot_hides_overlays(false));
  CHECK(!shot_call_original(true) && shot_call_original(false));
  // thunk_decode_target(opcodeAddr, rel32) is common.h's; the rel32 is the little-endian long in bytes 1..4 of each
  // pattern.
  #define P6_REL32(b) ((unsigned)(b)[1] | ((unsigned)(b)[2] << 8) | ((unsigned)(b)[3] << 16) | ((unsigned)(b)[4] << 24))
  CHECK(rva::THUNK_DISPLAYWEAPON_BYTES[0] == 0xE9 && rva::THUNK_DISPLAYMASKS_BYTES[0] == 0xE9 &&
        rva::THUNK_DISPLAYREST_BYTES[0] == 0xE9 && rva::THUNK_VSCRN_BYTES[0] == 0xE9);
  CHECK(thunk_decode_target(rva::THUNK_DISPLAYWEAPON_RVA, P6_REL32(rva::THUNK_DISPLAYWEAPON_BYTES)) == rva::DISPLAYWEAPON_BODY_RVA);
  CHECK(thunk_decode_target(rva::THUNK_DISPLAYMASKS_RVA,  P6_REL32(rva::THUNK_DISPLAYMASKS_BYTES))  == rva::DISPLAYMASKS_BODY_RVA);
  CHECK(thunk_decode_target(rva::THUNK_DISPLAYREST_RVA,   P6_REL32(rva::THUNK_DISPLAYREST_BYTES))   == rva::DISPLAYREST_BODY_RVA);
  CHECK(thunk_decode_target(rva::THUNK_VSCRN_RVA,         P6_REL32(rva::THUNK_VSCRN_BYTES))         == rva::VSCRN_BODY_RVA);
  #undef P6_REL32
  CHECK(rva::UD_SCREEN_SIZE == 1508 && rva::UD_CROSSHAIR == 1516 && rva::PS_GM == 844);
  // shot_frame_reads_allowed gates the two blind `ud` reads and the crosshair substitution: an unverified frame allows
  // neither.
  CHECK(shot_frame_reads_allowed(true) && !shot_frame_reads_allowed(false));
}
