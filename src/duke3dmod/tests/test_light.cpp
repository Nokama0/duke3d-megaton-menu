#include "test_common.h"
#include "../light.h"

// The fog and fullbright rules and brackets in light.h are pure, so this block pins every rule directly.

void tb_0082_phase_6_light_rules() {
  CHECK(!light_wants(false, false, false));
  CHECK(!light_wants(false, false, true));    // camera switch without the camera
  CHECK(!light_wants(false, true,  false));   // camera without its switch
  CHECK( light_wants(false, true,  true));
  CHECK( light_wants(true,  false, false));   // gameplay switch alone
  CHECK( light_wants(true,  true,  false));
  CHECK( light_wants(true,  false, true));
  CHECK( light_wants(true,  true,  true));
  CHECK(!fog_substitute(false, false) && !fog_substitute(false, true) &&
        !fog_substitute(true, false) && fog_substitute(true, true));
  // The getshadefactor site table is 8 bytes, not 3: "55 8B EC" alone is among the most common x86 prologues and would
  // match the wrong function on an unverified binary. Bytes 3..7 are the rest of the documented prologue, identical in
  // expected and replacement.
  CHECK(GETSHADEFACTOR_COUNT == 1 && GETSHADEFACTOR_SITES[0].len == 8);
  CHECK(GETSHADEFACTOR_SITES[0].rva == 0x000786F0);
  CHECK(GETSHADEFACTOR_SITES[0].expected[0] == 0x55 && GETSHADEFACTOR_SITES[0].expected[1] == 0x8B &&
        GETSHADEFACTOR_SITES[0].expected[2] == 0xEC && GETSHADEFACTOR_SITES[0].expected[3] == 0x83 &&
        GETSHADEFACTOR_SITES[0].expected[4] == 0xEC && GETSHADEFACTOR_SITES[0].expected[5] == 0x1C &&
        GETSHADEFACTOR_SITES[0].expected[6] == 0x0F && GETSHADEFACTOR_SITES[0].expected[7] == 0xBF);
  CHECK(GETSHADEFACTOR_SITES[0].replacement[0] == 0xD9 && GETSHADEFACTOR_SITES[0].replacement[1] == 0xE8 &&
        GETSHADEFACTOR_SITES[0].replacement[2] == 0xC3 && GETSHADEFACTOR_SITES[0].replacement[3] == 0x83 &&
        GETSHADEFACTOR_SITES[0].replacement[4] == 0xEC && GETSHADEFACTOR_SITES[0].replacement[5] == 0x1C &&
        GETSHADEFACTOR_SITES[0].replacement[6] == 0x0F && GETSHADEFACTOR_SITES[0].replacement[7] == 0xBF);
  // Bytes 3..7 are unreachable after the ret at byte 2, and the release must restore the ORIGINAL bytes there byte for
  // byte; pinned directly.
  for (int i = 3; i <= 7; i++) {
    CHECK(GETSHADEFACTOR_SITES[0].replacement[i] == GETSHADEFACTOR_SITES[0].expected[i]);
  }
  {
    unsigned char img[0x400];
    memset(img, 0xAA, sizeof img);
    write_field_bytes(img, 0x100 + rva::PS_VISIBILITY, 4, 512);
    FogBracket b = { 0, 0, 0, false };
    CHECK(fog_bracket_arm(&b, img, 0x100));
    CHECK(read_field_bytes(img, 0x100 + rva::PS_VISIBILITY, 4) == 0);
    CHECK(b.armed && b.saved == 512);
    CHECK(!fog_bracket_arm(&b, img, 0x100));            // already armed
    // The game moved the field while armed: the release still writes what was READ.
    write_field_bytes(img, 0x100 + rva::PS_VISIBILITY, 4, 7);
    CHECK(fog_bracket_release(&b));
    CHECK(read_field_bytes(img, 0x100 + rva::PS_VISIBILITY, 4) == 512);
    CHECK(!b.armed);
    CHECK(!fog_bracket_release(&b));                    // a second release is a no-op that reports false
    CHECK(!fog_bracket_arm(&b, 0, 0x100));               // null base refused
    int untouched = 0;
    for (unsigned i = 0; i < sizeof img; i++) if (i < 0x100 + rva::PS_VISIBILITY || i >= 0x100 + rva::PS_VISIBILITY + 4) untouched += (img[i] == 0xAA);
    CHECK(untouched == (int)sizeof img - 4);
  }
  {
    static unsigned char img[0x00200000];                // covers both RVAs
    memset(img, 0x11, sizeof img);
    img[rva::NOFOG_RVA] = 0;
    write_field_bytes(img, rva::R_USETILESHADES_RVA, 4, 1);
    LightLevers l = { 0, 0, 0, false };
    CHECK(light_levers_arm(&l, img));
    CHECK(img[rva::NOFOG_RVA] == 1);
    CHECK(read_field_bytes(img, rva::R_USETILESHADES_RVA, 4) == 0);
    CHECK(l.armed && l.savedNofog == 0 && l.savedTileShades == 1);
    CHECK(!light_levers_arm(&l, img));
    // The game had tile shades OFF already (accurate lighting off): restore writes 0, not 1.
    CHECK(light_levers_release(&l));
    CHECK(img[rva::NOFOG_RVA] == 0 && read_field_bytes(img, rva::R_USETILESHADES_RVA, 4) == 1);
    write_field_bytes(img, rva::R_USETILESHADES_RVA, 4, 0);
    CHECK(light_levers_arm(&l, img) && l.savedTileShades == 0);
    CHECK(light_levers_release(&l));
    CHECK(read_field_bytes(img, rva::R_USETILESHADES_RVA, 4) == 0);
    CHECK(!light_levers_release(&l));
    CHECK(!light_levers_arm(&l, 0));
  }
  light_reset_available();
  CHECK(light_available());
  light_mark_unavailable();
  CHECK(!light_available());
  light_reset_available();
  CHECK(light_available());

  // light_row_unavailable mirrors nocull_row_unavailable's query shape (nocull.h).
  light_reset_available();
  CHECK(!light_row_unavailable());
  light_mark_unavailable();
  CHECK(light_row_unavailable());
  light_reset_available();

  // The bglDisable loader pointer RVA and the GL_FOG enum value are pinned because nothing else here is testable
  // without a GL context.
  CHECK(rva::BGLDISABLE_PTR_RVA == 0x017FF4F8);
  CHECK(GL_FOG_ENUM == 0x0B60);
}
