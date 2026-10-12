#pragma once
// Shared helpers and the engine RVA table. Pure, so test_host includes it. VA = RVA + 0x400000 throughout.
#include <stddef.h>

inline int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Saturate rather than wrap, so a hostile game-memory value can't signed-overflow first.
inline int clamp_ll_to_int(long long v) {
  if (v >  2147483647LL)      return  2147483647;
  if (v < -2147483647LL - 1LL) return -2147483647 - 1;
  return (int)v;
}

// Truncate a double to int without UB (out-of-range or NaN casts are UB; project_world calls this every frame).
inline int clamp_double_to_int(double v) {
  if (!(v > -2147483648.0)) return -2147483647 - 1;
  if (v > 2147483647.0)     return  2147483647;
  return (int)v;
}

// rel32 of a 5-byte E9 JMP thunk: the CPU targets opcodeAddr + 5 + rel32 (modulo 2^32).
inline unsigned thunk_encode_rel32(unsigned opcodeAddr, unsigned targetAddr) {
  return targetAddr - (opcodeAddr + 5);
}
inline unsigned thunk_decode_target(unsigned opcodeAddr, unsigned rel32) {
  return opcodeAddr + 5 + rel32;
}

// Per-walk sprite-list cycle guard: a status list holds at most maxSteps sprites, more means nextspritestat cycles.
// Valid for ONE walk at a time (esp.cpp's and aim.cpp's walks never nest).
struct WalkCounter {
  int  steps;
  bool corrupted;
};
inline void walk_counter_begin(WalkCounter& wc) {
  wc.steps = 0;
  wc.corrupted = false;
}
inline bool walk_counter_advance(WalkCounter& wc, int maxSteps) {
  wc.steps++;
  if (wc.steps > maxSteps) {
    wc.corrupted = true;
    return false;
  }
  return true;
}

namespace rva {
  const unsigned RENDMODE     = 0x00A14C68;
  const unsigned BPP          = 0x00A2E854;
  const unsigned XDIM         = 0x01A8E744;
  const unsigned YDIM         = 0x01967DC4;
  // Read only: setaspect is the sole writer. polymost_drawsprite scales its depth axis by this global.
  const unsigned VIEWINGRANGE = 0x01AF7094;

  // setaspect (VA 0x00444A80): writing viewingrange directly leaves viewingrangerecip, xdimenscale, xdimscale and
  // ydimenscale stale. Intercepted via THUNK_SETASPECT_RVA (overlay.cpp's hooked_setaspect).
  const unsigned SETASPECT    = 0x00044A80;

  // setaspect's call-site thunk (VA 0x004016FE, E9 7D 33 04 00 = JMP 0x00444A80), so hooked_setaspect substitutes
  // viewingrange in phase.
  const unsigned THUNK_SETASPECT_RVA = 0x000016FE;
  const unsigned char THUNK_SETASPECT_BYTES[5] = { 0xE9, 0x7D, 0x33, 0x04, 0x00 };

  // setaspect_new: thunk VA 0x004024F5, JMP 0x00444B40. Not hooked: it likely calls setaspect.
  const unsigned THUNK_SETASPECT_NEW_RVA = 0x000024F5;
  const unsigned char THUNK_SETASPECT_NEW_BYTES[5] = { 0xE9, 0x46, 0x26, 0x04, 0x00 };

  // setaspect's second parameter; hooked_setaspect passes it through for the ultrawide correction.
  const unsigned YXASPECT     = 0x018911EC;

  // xdimenscale = scale(xdimen, yxaspect, 320); setaspect writes it at VA 0x00444AEE. VIEWINGRANGERECIP depends on it.
  const unsigned XDIMENSCALE  = 0x01893F30;

  // divscale32(1, viewingrange); written at VA 0x00444AB1.
  const unsigned VIEWINGRANGERECIP = 0x01978290;

  const unsigned UD           = 0x01764040;
  const unsigned UD_MULTIMODE = 0x938;      // offset within ud
  const unsigned NUMPLAYERS   = 0x017743BC;
  const unsigned TOTALCLOCK   = 0x0187DE88;
  const unsigned PAUSE_ON     = 0x017645B8;

  const unsigned RVA_KEYSTATUS = 0x01772DC0;  // uchar[256]
  const unsigned RVA_MOUSEX    = 0x00A2E868;  // long
  const unsigned RVA_MOUSEY    = 0x00A2E86C;  // long
  const unsigned RVA_MOUSEB    = 0x00A2E870;  // long
  const unsigned RVA_JOYB      = 0x00A2E878;  // long

  // readmousexy() zeroes deltas whenever mouseacquired==0 || appactive==0 || moustat==0. WIDTHS MATTER: mouseacquired/
  // moustat are uchar, CONTROL_*Enabled longs.
  const unsigned RVA_MOUSEACQUIRED           = 0x00A2E881;  // uchar
  const unsigned RVA_MOUSTAT                 = 0x00A2E882;  // uchar
  const unsigned RVA_CONTROL_MOUSEENABLED    = 0x001DE264;  // long
  const unsigned RVA_CONTROL_JOYSTICKENABLED = 0x001DE25C;  // long

  // CONTROL_GetInput funnels every input source into one 0x18-byte ControlInfo (VA 0x00401F91, JMP 0x0040B830).
  const unsigned THUNK_CONTROL_GETINPUT_RVA = 0x00001F91;
  const unsigned char THUNK_CONTROL_GETINPUT_BYTES[5] = { 0xE9, 0x9A, 0x98, 0x00, 0x00 };

  // Not the movement choke point (ControlInfo dumped zero while the player moved). Real path: faketimerhandler ->
  // getinput(myconnectindex) fills `loc`. Thunk VA 0x004036DE, JMP 0x004F9080.
  const unsigned THUNK_GETINPUT_RVA = 0x000036DE;
  const unsigned char THUNK_GETINPUT_BYTES[5] = { 0xE9, 0x9D, 0x59, 0x0F, 0x00 };

  // processinput (sole caller domovethings) writes horiz/ohoriz/horizoff/ohorizoff/look_ang/return_to_center each tic;
  // restoring from the render path raced it. VA 0x00402A63, JMP 0x004FA450.
  const unsigned THUNK_PROCESSINPUT_RVA = 0x00002A63;
  const unsigned char THUNK_PROCESSINPUT_BYTES[5] = { 0xE9, 0xE8, 0x79, 0x0F, 0x00 };

  // `loc`, 12 bytes, zeroed by hooked_getinput after the real getinput; avg* are the engine's, untouched.
  const unsigned RVA_LOC = 0x0172AF90;

  // `bits` is the unsigned long at +8 inside `loc`; fire is bit 4 (triggerbot.md C).
  const unsigned      LOC_BITS_OFFSET = 8;
  const unsigned long INPUT_BIT_FIRE  = 4;

  // Hold-view: `avel` (yaw) +0, `horz` (pitch) +1, signed chars, zeroed while the aimbot steers; fvel/svel/bits excluded
  // so holding view doesn't stop walking, strafing or shooting.
  const unsigned      LOC_AVEL_OFFSET = 0;
  const unsigned      LOC_HORZ_OFFSET = 1;

  // Player array `ps` (VA 0x019CBB60, stride 940) and myconnectindex. The freeze covers all six view fields (three moved
  // the recentring snap from open to close). WIDTHS MATTER: horiz/ohoriz/ohorizoff longs, look_ang/horizoff shorts.
  const unsigned RVA_PS                     = 0x015CBB60;
  const unsigned PS_STRIDE                  = 940;         // 0x3AC bytes per player
  const unsigned RVA_MYCONNECTINDEX         = 0x017743B8;  // long
  const unsigned PS_OFFSET_HORIZ            = 0x220;       // long  (4 bytes)
  const unsigned PS_OFFSET_OHORIZ           = 0x224;       // long  (4 bytes)
  const unsigned PS_OFFSET_OHORIZOFF        = 0x228;       // long  (4 bytes)
  const unsigned PS_OFFSET_LOOK_ANG         = 0x294;       // short (2 bytes)
  const unsigned PS_OFFSET_HORIZOFF         = 0x2BE;       // short (2 bytes)
  const unsigned PS_OFFSET_RETURN_TO_CENTER = 0x398;       // char  (1 byte)

  const unsigned PS_OFFSET_ANG    = 0x28C;  // short (2 bytes), current yaw
  const unsigned PS_OFFSET_OANG   = 0x28E;  // short (2 bytes), previous yaw (render interpolation)
  const unsigned PS_OFFSET_ANGVEL = 0x290;  // short (2 bytes), angular velocity

  const unsigned IAT_LOADLIBRARYA           = 0x01B19E5C;
  const unsigned IAT_SDL_GETKEYBOARDSTATE   = 0x01B1A754;
  const unsigned IAT_SDL_GETMOUSESTATE      = 0x01B1A6E0;
  const unsigned IAT_SDL_WARPMOUSEINWINDOW  = 0x01B1A6E4;
  const unsigned IAT_SDL_POLLEVENT          = 0x01B1A760;
  const unsigned IAT_SDL_GL_SWAPWINDOW      = 0x01B1A768;

  const unsigned SPRITE           = 0x019D4B60;  // spritetype[16384]
  const unsigned SPRITE_STRIDE    = 44;
  const unsigned SECTOR           = 0x01893F80;  // sectortype[4096]
  const unsigned SECTOR_STRIDE    = 40;
  const unsigned HEADSPRITESTAT   = 0x018907C0;  // short[1025]
  const unsigned NEXTSPRITESTAT   = 0x01A91760;  // short[16384]
  const unsigned NUMSECTORS       = 0x018907A0;  // short
  const unsigned GLOBALPOSX       = 0x01ADC9A4;  // long
  const unsigned GLOBALPOSY       = 0x01967DCC;  // long
  const unsigned GLOBALPOSZ       = 0x01893F20;  // long
  const unsigned GLOBALANG        = 0x019D4B5E;  // short
  const unsigned GLOBALHORIZ      = 0x018D18BC;  // long
  const unsigned HALFXDIMEN       = 0x01AD6440;  // long

  // Polymost's nine per-frame projection doubles, written by polymost_drawrooms (VA 0x0047a2d0) once per frame;
  // recomputed formulas were wrong at every FOV. Writers: polymost_drawrooms and polymost_glreset (dorotatesprite
  // saves/restores).
  const unsigned GCOSANG          = 0x01774350;  // gcosang,  write 0x0047a51f
  const unsigned GSINANG          = 0x017732D0;  // gsinang,  write 0x0047a531
  const unsigned GCOSANG2         = 0x01773280;  // gcosang2, write 0x0047a549
  const unsigned GSINANG2         = 0x01774340;  // gsinang2, write 0x0047a561
  const unsigned GHALFX           = 0x00A14C58;  // ghalfx,   write 0x0047a56d
  const unsigned GHORIZ           = 0x00A14C60;  // ghoriz,   LAST write 0x0047a638
  const unsigned GSHANG           = 0x01773290;  // gshang,   write 0x0047a5fa
  const unsigned GCHANG           = 0x01773288;  // gchang,   write 0x0047a61e
  const unsigned GYXSCALE         = 0x009D0C38;  // gyxscale, write 0x0047a4dd

  // The two scale doubles drawpoly multiplies every vertex by (polymost-depth.md A), read by world.cpp at the
  // polymost_scansector call site. grhalfxdown10 = 1 / (ghalfx * 1024).
  const unsigned GRHALFXDOWN10    = 0x00A04C40;  // double, VA 0x00e04c40 (facts A)
  const unsigned GVIEWXRANGE      = 0x00A04C48;  // double, VA 0x00e04c48

  // ydimen: the 3D viewport height, distinct from YDIM; swapping them silently moves the projection's vertical origin.
  // setview (VA 0x004449a3) stores `windowy2 - windowy1 + 1`; polymost_drawrooms reads `ydimen >> 1` (VA 0x0047a5ad).
  const unsigned YDIMEN           = 0x018CFD5C;  // long

  // YDIMENSCALE (VA 0x01C93F5C) is absent on purpose: only dorotatesprite reads it.
  const unsigned WINDOWX1         = 0x019D2624;  // long
  const unsigned WINDOWY1         = 0x01893F2C;  // long
  const unsigned SINTABLE         = 0x01A8B020;  // short[2048]
  const unsigned THUNK_BADGUY_RVA = 0x00003BCF;
  const unsigned BADGUY           = 0x000B9C50;  // expected recovered target
  // tilesizx/tilesizy: ART tile pixel sizes by picnum; a face sprite's world size is tilesiz * repeat
  // (polymost_drawsprite, VA 0x00482d02-0x00482d9a). Both short[9500] (a long read folds two tiles).
  const unsigned TILESIZX         = 0x01993380;  // short[9500]
  const unsigned TILESIZY         = 0x01A861C0;  // short[9500]
  const int      MAX_TILES        = 9500;
  const int      MAX_SPRITES      = 16384;

  // actorscrptr: per picnum a pointer to the actor's compiled CON code, or null; its first long is `strength` (max
  // health): spawn() (VA 0x004c1e00) sets `sprite[i].extra = (short)*actorscrptr[picnum]` (VA 0x004c23d4). No baked table.
  const unsigned ACTORSCRPTR      = 0x017575A0;  // long *[9500]

  // script: the compiled CON program (long[23056], VA 0x019D94E0); every actorscrptr entry points into it (parsecommand
  // VA 0x004deba8), so the bound is a real containment test.
  const unsigned SCRIPT           = 0x015D94E0;  // long[23056]
  const int      SCRIPT_LONGS     = 23056;
  const int      MAX_SECTORS      = 4096;
  const int      MAX_STATNUM      = 1024;

  // wall[] and numwalls, re-derived three ways (PDB, 32-byte walltype with x/y at +0/+4, prelevel's SHL 5 at VA
  // 0x005079f4). NUMWALLS sits four bytes before sprite[] and two before globalang; the suite pins all three apart.
  const unsigned WALL             = 0x018D18E0;  // walltype[16384]
  const unsigned WALL_STRIDE      = 32;          // sizeof(walltype)
  const unsigned NUMWALLS         = 0x019D4B5C;  // short
  const int      MAX_WALLS        = 16384;

  // walltype fields the wall scan needs: checkhitwall picnum +16/overpicnum +18, checkhitswitch lotag +26/hitag +28,
  // polymost_scansector point2 +8/nextsector +12.
  const unsigned WAL_POINT2     = 8;    // short
  const unsigned WAL_NEXTSECTOR = 12;   // short
  const unsigned WAL_CSTAT      = 14;   // short
  const unsigned WAL_PICNUM     = 16;   // short
  // overpicnum: the tile checkhitwall switches on; tbot_wall_breakable reads it.
  const unsigned WAL_OVERPICNUM = 18;   // short
  const unsigned WAL_PAL        = 21;   // char
  const unsigned WAL_LOTAG      = 26;   // short
  const unsigned WAL_HITAG      = 28;   // short


  // hittype[] (PDB weaponhit[16384]), the actor side table, struct 72 bytes (VA 0x019F2880). bposx/y/z is the previous
  // tic's position written by the engine's actor mover; its difference to sprite[] is the per-tic velocity.
  const unsigned RVA_HITTYPE     = 0x015F2880;
  const unsigned HITTYPE_STRIDE  = 72;
  const unsigned HITTYPE_FLOORZ  = 20;
  const unsigned HITTYPE_BPOSX   = 36;
  const unsigned HITTYPE_BPOSY   = 40;
  const unsigned HITTYPE_BPOSZ   = 44;
  // weaponhit layout (72 bytes): cgg +0, picnum +2, ang +4, extra +6, owner +8, movflag +10, tempang +12, actorstayput
  // +14, dispicnum +16, timetosleep +18, floorz +20, ceilingz +24, lastvx +28, lastvy +32, bposx/y/z +36/+40/+44,
  // temp_data[6] +48. dispicnum is a short; the suite pins it apart from its neighbours.
  const unsigned HITTYPE_DISPICNUM = 16;
  // temp_data[6]: the CON VM's per-actor scratch; [4] is the current ACTION, [1] the counter beside it.
  const unsigned HITTYPE_TEMPDATA       = 48;
  const int      HITTYPE_TEMPDATA_COUNT = 6;

  const unsigned CANSEE_BODY_RVA   = 0x0003EEA0;
  const unsigned GETANGLE_BODY_RVA = 0x0003E550;

  // shoot(short i, short atwith): thunk VA 0x0040316B = JMP 0x004F3640, for silent aim.
  const unsigned THUNK_SHOOT_RVA = 0x0000316B;
  const unsigned char THUNK_SHOOT_BYTES[5] = { 0xE9, 0xD0, 0x04, 0x0F, 0x00 };
  const unsigned SHOOT_BODY_RVA = 0x000F3640;

  const unsigned PS_AUTO_AIM     = 644;   // long
  const unsigned PS_CURSECTNUM   = 658;   // short
  // got_access: a short, bits 1 blue / 2 red / 4 yellow, tested by checkhitswitch (VA 0x0050ebc7).
  const unsigned PS_GOT_ACCESS   = 732;   // short
  const unsigned PS_CURR_WEAPON  = 696;   // short
  // kickback_pic: the per-weapon frame counter processinput advances once a tic; written from hooked_processinput's
  // tail, local player only (processinput holds a pointer: ADD ECX, 0x2da at VA 0x004fa48c).
  const unsigned PS_KICKBACK_PIC = 730;   // short
  const unsigned PS_PYOFF        = 580;   // long, 0x244
  const unsigned PS_HORIZOFF     = 702;   // short
  const unsigned PS_I            = 742;   // short, the player's own sprite index
  // newowner: the viewscreen/camera sprite index, -1 in normal play (VA 0x004fb20d); >= 0 overwrites ps->pos/ang.
  const unsigned PS_NEWOWNER     = 708;   // short, 0x2C4
  const unsigned PS_DEAD_FLAG    = 812;   // short


  // displayrooms(short snum, long smoothratio): the render-clock detour the free camera substitutes around.
  const unsigned THUNK_DISPLAYROOMS_RVA = 0x0000182A;
  const unsigned char THUNK_DISPLAYROOMS_BYTES[5] = { 0xE9, 0xB1, 0xF2, 0x0B, 0x00 };
  const unsigned DISPLAYROOMS_BODY_RVA  = 0x000C0AE0;

  // displayrooms' floor/ceiling/pitch clamps: four 2-byte conditional jumps in one 4 KB page. Making each unconditional
  // (EB, same length and displacement) skips the clamp (freecam-accuracy.md G).
  const unsigned CLAMP_BRIDGE_RVA   = 0x000C1455;
  const unsigned CLAMP_SECTOR_RVA   = 0x000C149D;
  const unsigned CLAMP_HORIZ_HI_RVA = 0x000C14F8;
  const unsigned CLAMP_HORIZ_LO_RVA = 0x000C150C;

  // screenpeek: the player index displayrooms receives as `snum`; a short.
  const unsigned RVA_SCREENPEEK = 0x015F1C78;

  // Build helpers, real bodies (cdecl): updatesectorz(long x,long y,long z,short* sectnum), getflorzofslope/
  // getceilzofslope(short sectnum,long x,long y). updatesectorz's `sectnum` is -1 outside every sector (the "solid
  // space" signal the teleport refuses on).
  const unsigned UPDATESECTORZ_BODY_RVA = 0x00043910;
  const unsigned GETFLORZ_BODY_RVA      = 0x00046750;
  const unsigned GETCEILZ_BODY_RVA      = 0x000465F0;

  const unsigned PS_POSX  = 532;   // long, 0x214
  const unsigned PS_POSY  = 536;   // long, 0x218
  const unsigned PS_POSZ  = 540;   // long, 0x21C -- the eye
  const unsigned PS_OPOSX = 568;   // long, 0x238
  const unsigned PS_OPOSY = 572;   // long, 0x23C
  const unsigned PS_OPOSZ = 576;   // long, 0x240
  // opyoff: zeroing both stops the camera inheriting view bob (displayrooms, VA 0x004C0D06-0x004C0D22).
  const unsigned PS_OPYOFF = 584;  // long, 0x248
  // poszv: vertical velocity, zeroed by the teleport (processinput, VA 0x004FC2E7).
  const unsigned PS_POSZV  = 596;  // long, 0x254
  // Screen roll pair: left alone a free camera inherits the player's damage roll.
  const unsigned PS_OROTSCRNANG = 808;  // short, 0x328
  const unsigned PS_ROTSCRNANG  = 810;  // short, 0x32A
  // over_shoulder_on: a precondition, not substituted: non-zero makes displayrooms call view().
  const unsigned PS_OVER_SHOULDER_ON = 746;  // short, 0x2EA

  // krand's thunk: all six spread call sites below are a 5-byte `E8 rel32` decoding to it.
  const unsigned KRAND_THUNK_RVA = 0x000018D9;

  // One spread call site: where `call krand` lives and the centre of the following `MOV reg, centre; SUB reg, EAX`.
  // Replacing the call with `B8 <centre> 00 00 00` makes `centre - (krand() & mask)` 0 without advancing krand.
  struct AccSite { unsigned rva; unsigned char centre; };

  // Six sites by weapon. Yaw sites mask 0x1f, centre 16; pitch sites mask 0xff, centre 128. Chaingun and shotgun share
  // the hitscan pair. The expander's ACTOR sites (VA 0x004F5ED5, 0x004F5EEC) are enemy fire: absent.
  const AccSite ACC_PISTOL[2]   = { { 0x000F422C, 0x10 }, { 0x000F4273, 0x80 } };
  const AccSite ACC_HITSCAN[2]  = { { 0x000F428C, 0x10 }, { 0x000F42DC, 0x80 } };
  const AccSite ACC_EXPANDER[2] = { { 0x000F5E00, 0x10 }, { 0x000F5E47, 0x80 } };

  // hitscan(xs, ys, zs, sectnum, vx, vy, vz, &hitsect, &hitwall, &hitsprite, &hitx, &hity, &hitz, cliptype), cdecl
  // (freecam-accuracy.md F). VA 0x0043F180.
  const unsigned HITSCAN_BODY_RVA = 0x0003F180;
  const unsigned DNSAVEGAME_BODY_RVA = 0x000EEFD0;
  const unsigned char DNSAVEGAME_BODY_BYTES[12] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xC7, 0x45, 0xF0, 0x00, 0x00, 0x00 };

  // CLIPMASK1: the player's own shot mask, so the teleport ray stops where a shot would.
  const unsigned HITSCAN_CLIPTYPE = 0x01000040;

  // polymost_scansector's one seed call inside polymost_drawrooms (freecam-accuracy.md G): `E8 A2 64 00 00`. Redirected
  // by hook_call_site: the DLL's only install-time .text patch outside the thunks.
  const unsigned NOCULL_CALL_SITE_RVA = 0x0007AEE9;
  const unsigned char NOCULL_CALL_SITE_BYTES[5] = { 0xE8, 0xA2, 0x64, 0x00, 0x00 };
  const unsigned POLYMOST_SCANSECTOR_BODY_RVA = 0x00081390;
  // domost, Polymost's column-claim routine (dollhouse.h); all 45 callers in polymost_drawalls use this thunk.
  const unsigned THUNK_DOMOST_RVA = 0x0000427D;
  const unsigned char THUNK_DOMOST_BYTES[5] = { 0xE9, 0x1E, 0x52, 0x07, 0x00 };
  const unsigned DOMOST_BODY_RVA  = 0x000794A0;
  const unsigned DOMOSTPOLYMETHOD = 0x00A14CC0;   // long; -1 = claim without paint
  const unsigned DRAWALLS_CALL_SITE_RVA = 0x0007B0CB;
  const unsigned char DRAWALLS_CALL_SITE_BYTES[5] = { 0xE8, 0x90, 0x00, 0x00, 0x00 };
  const unsigned POLYMOST_DRAWALLS_BODY_RVA = 0x0007B160;
  const unsigned THESECTOR  = 0x018CA840;
  const unsigned P2         = 0x019CF620;
  const unsigned THEWALL    = 0x019782A0;
  // The mirror path's drawalls(0) call (VA 0x0047AF13) runs even when the mirror pass scanned nothing, so it is skipped
  // with the seed.
  const unsigned DRAWALLS_MIRROR_CALL_SITE_RVA = 0x0007AF13;
  const unsigned char DRAWALLS_MIRROR_CALL_SITE_BYTES[5] = { 0xE8, 0x48, 0x02, 0x00, 0x00 };
  const unsigned GETFLORZOFSLOPE_BODY_RVA = 0x00046750;
  const unsigned GETCEILZOFSLOPE_BODY_RVA = 0x000465F0;
  const unsigned char ZOFSLOPE_BODY_BYTES[12] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x56, 0x0F, 0xBF, 0x45, 0x08, 0x6B };
  const unsigned VCNT       = 0x00A10C54;
  const int      VSPMAX     = 4096;
  // globalorientation: the plane or wall stat being drawn; bit 0 = parallax sky.
  const unsigned GLOBALORIENTATION = 0x0189079C;
  const unsigned GLOBALPICNUM_S    = 0x01AA901C;
  // The texture mapping's depth plane gdx*sx + gdy*sy + gdo = 1/depth (drawpoly's divisor); a sky band paints only where positive.
  const unsigned GDX = 0x01774348;
  const unsigned GDY = 0x017732C0;
  const unsigned GDO = 0x017732B0;
  const unsigned GUX = 0x01773278, GUY = 0x017732E0, GUO = 0x017732A8;   // doubles
  const unsigned GVX = 0x01774330, GVY = 0x01774328, GVO = 0x017732C8;   // doubles
  const unsigned GLOBALPAL_L   = 0x01A8ABFC;
  const unsigned SKYCLAMPHACK  = 0x00A14CBC;   // long: drawpoly clamps the texture when set
  const unsigned THUNK_DRAWPOLY_RVA = 0x00002F36;
  const unsigned char THUNK_DRAWPOLY_BYTES[5] = { 0xE9, 0x55, 0x22, 0x07, 0x00 };
  const unsigned DRAWINGSKYBOX = 0x00A14CB4;   // long: non-zero on a skybox face
  // Which expansion is loaded, set once by set_addon (VA 0x004CBDD0): 0 base, 1 Duke It Out In D.C., 2 Nuclear Winter,
  // 3 Duke Caribbean. Expansions reuse base picnums for different creatures (esp_names.h).
  const unsigned ADDON = 0x00A2F704;
  const unsigned POW2XSPLIT    = 0x00A14CB8;
  const unsigned GLOBALSHADE_L     = 0x017FF550;
  const unsigned BUNCHFIRST = 0x01A8E760;

  // void __cdecl drawpoly(double* px, double* py, long n, long method): Polymost's polygon painter, called directly by
  // dollhouse.cpp's mode 4; the twelve bytes are its fingerprint. VA 0x00475190.
  const unsigned DRAWPOLY_BODY_RVA = 0x00075190;
  const unsigned char DRAWPOLY_BODY_BYTES[12] = {
      0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x04, 0x00, 0x00, 0x56, 0xD9, 0xEE };

  // gotsector: the per-frame visited-sector bitset (bit `s & 7` of byte `s >> 3`); uchar[512].
  const unsigned GOTSECTOR = 0x01890FE0;

  // polymost_scansector appends to these two counters unbounded: the arrays hold exactly MAXWALLSB entries and the
  // shorts go negative at 32767. The draw-all-sectors sweep removes vanilla's bound, so nocull_budget_allows limits it.
  const unsigned NUMSCANS   = 0x01AD8980;
  const unsigned NUMBUNCHES = 0x019D2620;
  const int      MAXWALLSB  = 6144;

  // A visible mirror runs preparemirror + a second drawrooms first, so polymost_drawrooms (and our seed hook) runs
  // twice. inprepamirror is one byte, read at VA 0x0047AEF1 (set on the mirror pass).
  const unsigned INPREPAREMIRROR = 0x0032B4A6;

  // Damage numbers (damage-triggers-weapons.md A): ifhitbyweapon is the one function that lowers an actor's health.
  // Damage this tic is hittype[i].extra read before the call (cleared to -1 on exit); health after is sprite[i].extra.
  const unsigned THUNK_IFHITBYWEAPON_RVA = 0x00003E9F;
  const unsigned char THUNK_IFHITBYWEAPON_BYTES[5] = { 0xE9, 0x5C, 0x5B, 0x09, 0x00 };
  const unsigned IFHITBYWEAPON_BODY_RVA  = 0x00099A00;
  // The hittype[] shorts ifhitbyweapon touches: picnum, ang, extra (summed damage), owner.
  const unsigned HITTYPE_PICNUM = 2, HITTYPE_ANG = 4, HITTYPE_EXTRA = 6, HITTYPE_OWNER = 8;

  const unsigned PS_WEAPON_POS     = 828;   // short (facts D)
  const unsigned PS_LAST_WEAPON    = 698;   // short, recorded only; nothing reads it
  const unsigned PS_HOLSTER_WEAPON = 891;   // char, recorded only; nothing reads it
}

// hitscan's C signature (freecam-accuracy.md F): cdecl, 14 args, three short* and three long* out-parameters; named so
// hitscan_call can take it (engine.cpp can't link into test_host).
typedef long (__cdecl* HitscanRawFn)(long, long, long, short,
                                     long, long, long,
                                     short*, short*, short*,
                                     long*, long*, long*,
                                     unsigned long);

// Seeds every out-parameter fail closed: `hitsect` starts at -1 because the refusal chain keys on `hitsect < 0`, and a
// body that returned unwritten would otherwise report a floor hit at the origin. False for a null `fn` or out-parameter.
inline bool hitscan_call(HitscanRawFn fn, long xs, long ys, long zs, short sectnum,
                         long vx, long vy, long vz, unsigned long cliptype,
                         int* hitsect, int* hitwall, int* hitsprite,
                         int* hitx, int* hity, int* hitz) {
  if (!fn || !hitsect || !hitwall || !hitsprite || !hitx || !hity || !hitz) return false;
  short hs = -1, hw = -1, hsp = -1;
  long hx = 0, hy = 0, hz = 0;
  fn(xs, ys, zs, sectnum, vx, vy, vz, &hs, &hw, &hsp, &hx, &hy, &hz, cliptype);
  *hitsect = hs;
  *hitwall = hw;
  *hitsprite = hsp;
  *hitx = (int)hx;
  *hity = (int)hy;
  *hitz = (int)hz;
  return true;
}

// A slot past the end would read the next actor's cgg/picnum, plausible memory.
inline bool hittype_temp_data_slot_ok(int slot) {
  return slot >= 0 && slot < rva::HITTYPE_TEMPDATA_COUNT;
}
inline unsigned hittype_temp_data_offset(int slot) {
  return rva::HITTYPE_TEMPDATA + 4u * (unsigned)slot;
}

const int      ACTION_LONGS = 5;
const unsigned ACTION_BYTES = 20;          // startframe, numframes, viewtype, incval, delay
const unsigned ACTION_OFF_STARTFRAME = 0;
const unsigned ACTION_OFF_NUMFRAMES  = 4;
const unsigned ACTION_OFF_VIEWTYPE   = 8;
const unsigned ACTION_OFF_INCVAL     = 12;
const unsigned ACTION_OFF_DELAY      = 16;

// Non-zero when `bytes` at `addr` are committed and readable. Injected like AccProtectFn; null until installed.
typedef int (*ActionQueryFn)(const void* addr, unsigned bytes);
inline ActionQueryFn& action_query_fn() { static ActionQueryFn fn = 0; return fn; }

// True when the values look like a real CON action rather than garbage from a readable but wrong pointer.
inline bool action_plausible(int startframe, int numframes, int viewtype) {
  const bool viewtypeOk = viewtype == 1 || viewtype == 3 || viewtype == 5 ||
                           viewtype == 7 || viewtype == 8;
  const bool numframesOk = numframes >= 1 && numframes <= 64;
  const bool startframeOk = startframe >= 0 && startframe <= 1023;
  return viewtypeOk && numframesOk && startframeOk;
}

// `readable` and `plausible` are separate: readable but implausible differs from unreadable (t4 untrustworthy).
struct ActorAction {
  int startframe, numframes, viewtype, incval, delay;
  bool readable;    // the query said the 20 bytes are committed and readable
  bool plausible;   // and the five values passed the gate above
};

// Reads the five longs at `t4` (a pointer into the CON heap buffer), fail closed at every stage.
inline ActorAction action_read(unsigned t4) {
  ActorAction out = {};
  if (t4 == 0) return out;
  const ActionQueryFn query = action_query_fn();
  if (!query) return out;
  const unsigned char* p = (const unsigned char*)(size_t)t4;
  if (!query((const void*)p, ACTION_BYTES)) return out;
  out.startframe = *(const long*)(p + ACTION_OFF_STARTFRAME);
  out.numframes  = *(const long*)(p + ACTION_OFF_NUMFRAMES);
  out.viewtype   = *(const long*)(p + ACTION_OFF_VIEWTYPE);
  out.incval     = *(const long*)(p + ACTION_OFF_INCVAL);
  out.delay      = *(const long*)(p + ACTION_OFF_DELAY);
  out.readable   = true;
  out.plausible  = action_plausible(out.startframe, out.numframes, out.viewtype);
  return out;
}

// True when `sectnum` is a sector this DLL may read: bounded by both the live numsectors and the static array.
inline bool sector_index_ok(int sectnum, int numsectors) {
  return sectnum >= 0 && sectnum < numsectors && sectnum < rva::MAX_SECTORS;
}

// True when walls [wallptr, wallptr + wallnum) lie inside the live wall array (a corrupt sector could send a centroid
// loop far past it every frame). wallnum must be positive (the caller divides by it).
inline bool wall_range_ok(int wallptr, int wallnum, int numwalls) {
  if (numwalls <= 0 || numwalls > rva::MAX_WALLS) return false;
  if (wallnum <= 0 || wallnum > numwalls) return false;
  return wallptr >= 0 && wallptr <= numwalls - wallnum;
}

// PHEIGHT is the resting eye height above the floor in Build z (`floorz - (i << 8)`, i = 40; VA 0x004fb67a, 0x004fbf80,
// 0x004fc287). PHEIGHT_MARGIN is the ceiling side, `ceilingz + 0x400` (VA 0x004fb8a5), also applied by displayrooms
// (freecam-accuracy.md E1, E2).
const int PHEIGHT        = 0x2800;
const int PHEIGHT_MARGIN = 0x400;

const long MAX_PLAYER_INDEX = 15;

// The nine player-struct fields the view freeze (input.cpp) saves, holds while the panel owns input and writes back on
// close, in VIEW_FIELD_WIDTHS order.
enum ViewField {
  VF_HORIZ, VF_OHORIZ, VF_OHORIZOFF, VF_LOOK_ANG, VF_HORIZOFF, VF_RETURN_TO_CENTER,
  VF_ANG, VF_OANG, VF_ANGVEL,
  VF_COUNT
};

const int VIEW_FIELD_WIDTHS[VF_COUNT] = { 4, 4, 4, 2, 2, 1, 2, 2, 2 };

// VW_FREEZE_COLLAPSE collapses the three interpolated pairs (a held view must not leave a pair disagreeing or Build
// interpolates forever); VW_AIM_TRACK must not (it rewrites every tic). Both zero angvel/return_to_center.
enum ViewWriteMode { VW_FREEZE_COLLAPSE = 0, VW_AIM_TRACK = 1 };

enum ViewWriteSource {
  VWS_NONE = 0,   // leave the field as the engine left it
  VWS_ANG,        // new yaw, wrapped to 0..2047
  VWS_HORIZ,
  VWS_HORIZOFF,   // the player's CURRENT horizoff, read back live
  VWS_ZERO
};

inline ViewWriteSource view_write_source(ViewWriteMode mode, int field) {
  switch (field) {
    case VF_ANG:               return VWS_ANG;
    case VF_HORIZ:             return VWS_HORIZ;
    case VF_ANGVEL:            return VWS_ZERO;
    case VF_RETURN_TO_CENTER:  return VWS_ZERO;
    // The three interpolated partners: the freeze collapses them, the aimbot leaves them to interpolate.
    case VF_OANG:              return mode == VW_FREEZE_COLLAPSE ? VWS_ANG : VWS_NONE;
    case VF_OHORIZ:            return mode == VW_FREEZE_COLLAPSE ? VWS_HORIZ : VWS_NONE;
    case VF_OHORIZOFF:         return mode == VW_FREEZE_COLLAPSE ? VWS_HORIZOFF : VWS_NONE;
    default:                   return VWS_NONE;
  }
}

// Pure, so a mutation writing 0 can't pass: the binding living only in engine.cpp's switch let it.
inline bool view_write_value(ViewWriteMode mode, int field,
                             long ang, long horiz, long horizoff, long* out) {
  if (!out) return false;
  switch (view_write_source(mode, field)) {
    case VWS_ANG:      *out = ang;      return true;
    case VWS_HORIZ:    *out = horiz;    return true;
    case VWS_HORIZOFF: *out = horizoff; return true;
    case VWS_ZERO:     *out = 0;        return true;
    case VWS_NONE:
    default:           return false;
  }
}

// Maps a Windows VK to the SDL scancode compared against SDL_KEYDOWN; a key missing here is one bind_from_sdl_scancode
// (bind.h) refuses. Absent on purpose: ESCAPE, modifiers, lock keys, PRINT SCREEN, numpad ENTER. SDL2: A=4..Z=29.
inline bool vk_to_sdl_scancode(int vk, int* scancodeOut) {
  switch (vk) {
    case 0x2D: *scancodeOut = 73; return true;
    case 0x2E: *scancodeOut = 76; return true;
    case 0x24: *scancodeOut = 74; return true;
    case 0x23: *scancodeOut = 77; return true;
    case 0x21: *scancodeOut = 75; return true;
    case 0x22: *scancodeOut = 78; return true;
    case 0x09: *scancodeOut = 43; return true;
    case 0xC0: *scancodeOut = 53; return true;
    // T is the teleport default, V the toggle default (the old F6 collided with Megaton's Quick_Save); V is unbound in
    // both key tables. Outside the letter run since a switch can't name a case twice.
    case 0x54: *scancodeOut = 23; return true;
    case 0x56: *scancodeOut = 25; return true;
    case 0x70: *scancodeOut = 58; return true;
    case 0x71: *scancodeOut = 59; return true;
    case 0x72: *scancodeOut = 60; return true;
    case 0x73: *scancodeOut = 61; return true;
    case 0x74: *scancodeOut = 62; return true;
    case 0x75: *scancodeOut = 63; return true;
    case 0x76: *scancodeOut = 64; return true;
    case 0x77: *scancodeOut = 65; return true;
    case 0x78: *scancodeOut = 66; return true;
    case 0x79: *scancodeOut = 67; return true;
    case 0x7A: *scancodeOut = 68; return true;
    case 0x7B: *scancodeOut = 69; return true;
    case 0x41: *scancodeOut =  4; return true;
    case 0x42: *scancodeOut =  5; return true;
    case 0x43: *scancodeOut =  6; return true;
    case 0x44: *scancodeOut =  7; return true;
    case 0x45: *scancodeOut =  8; return true;
    case 0x46: *scancodeOut =  9; return true;
    case 0x47: *scancodeOut = 10; return true;
    case 0x48: *scancodeOut = 11; return true;
    case 0x49: *scancodeOut = 12; return true;
    case 0x4A: *scancodeOut = 13; return true;
    case 0x4B: *scancodeOut = 14; return true;
    case 0x4C: *scancodeOut = 15; return true;
    case 0x4D: *scancodeOut = 16; return true;
    case 0x4E: *scancodeOut = 17; return true;
    case 0x4F: *scancodeOut = 18; return true;
    case 0x50: *scancodeOut = 19; return true;
    case 0x51: *scancodeOut = 20; return true;
    case 0x52: *scancodeOut = 21; return true;
    case 0x53: *scancodeOut = 22; return true;
    case 0x55: *scancodeOut = 24; return true;
    case 0x57: *scancodeOut = 26; return true;
    case 0x58: *scancodeOut = 27; return true;
    case 0x59: *scancodeOut = 28; return true;
    case 0x5A: *scancodeOut = 29; return true;
    // THE DIGIT ROW's trap: SDL numbers '1' FIRST and puts '0' after '9' (VK has 0x30 first); '0' last here.
    case 0x31: *scancodeOut = 30; return true;
    case 0x32: *scancodeOut = 31; return true;
    case 0x33: *scancodeOut = 32; return true;
    case 0x34: *scancodeOut = 33; return true;
    case 0x35: *scancodeOut = 34; return true;
    case 0x36: *scancodeOut = 35; return true;
    case 0x37: *scancodeOut = 36; return true;
    case 0x38: *scancodeOut = 37; return true;
    case 0x39: *scancodeOut = 38; return true;
    case 0x30: *scancodeOut = 39; return true;
    case 0x0D: *scancodeOut = 40; return true;
    case 0x08: *scancodeOut = 42; return true;
    case 0x20: *scancodeOut = 44; return true;
    // Punctuation, in scancode order: the US layout's OEM codes, which Windows reports whatever the layout.
    case 0xBD: *scancodeOut = 45; return true;
    case 0xBB: *scancodeOut = 46; return true;
    case 0xDB: *scancodeOut = 47; return true;
    case 0xDD: *scancodeOut = 48; return true;
    case 0xDC: *scancodeOut = 49; return true;
    case 0xBA: *scancodeOut = 51; return true;
    case 0xDE: *scancodeOut = 52; return true;
    case 0xBC: *scancodeOut = 54; return true;
    case 0xBE: *scancodeOut = 55; return true;
    case 0xBF: *scancodeOut = 56; return true;
    case 0x13: *scancodeOut = 72; return true;
    // The arrows are NOT in reading order in SDL: right comes first.
    case 0x27: *scancodeOut = 79; return true;
    case 0x25: *scancodeOut = 80; return true;
    case 0x28: *scancodeOut = 81; return true;
    case 0x26: *scancodeOut = 82; return true;
    // The numpad shares the digit trap (1 first, 0 after 9); its ENTER is absent (Windows reports VK_RETURN).
    case 0x6F: *scancodeOut = 84; return true;
    case 0x6A: *scancodeOut = 85; return true;
    case 0x6D: *scancodeOut = 86; return true;
    case 0x6B: *scancodeOut = 87; return true;
    case 0x61: *scancodeOut = 89; return true;
    case 0x62: *scancodeOut = 90; return true;
    case 0x63: *scancodeOut = 91; return true;
    case 0x64: *scancodeOut = 92; return true;
    case 0x65: *scancodeOut = 93; return true;
    case 0x66: *scancodeOut = 94; return true;
    case 0x67: *scancodeOut = 95; return true;
    case 0x68: *scancodeOut = 96; return true;
    case 0x69: *scancodeOut = 97; return true;
    case 0x60: *scancodeOut = 98; return true;
    case 0x6E: *scancodeOut = 99; return true;
    default: return false;
  }
}

inline bool player_index_valid(long idx, long maxIndex) {
  return idx >= 0 && idx <= maxIndex;
}
inline unsigned player_field_rva(unsigned rvaPs, long idx, unsigned stride, unsigned fieldOffset) {
  return rvaPs + (unsigned)idx * stride + fieldOffset;
}

// Checks the END of the read (offset 42 width 4 overruns the 44-byte struct).
inline bool sprite_field_read_ok(unsigned off, int width) {
  return off + width <= (unsigned)rva::SPRITE_STRIDE;
}

// spawn() stores strength into `sprite.extra` with a `(short)` cast (VA 0x004c23e2), so a larger value is garbage.
const int ACTOR_STRENGTH_MAX = 32767;

// True when `p`, a raw address from actorscrptr, is a usable pointer to compiled CON code: non-null, 4-byte aligned,
inline bool actor_script_ptr_ok(unsigned base, unsigned p) {
  if (p == 0) return false;
  if ((p & 3u) != 0) return false;
  const unsigned lo  = base + rva::SCRIPT;
  const unsigned end = lo + (unsigned)rva::SCRIPT_LONGS * 4u;
  return p >= lo && p <= end - 4u;
}

// Little-endian field of exactly `width` bytes (1, 2 or 4), so a short field only ever has 2 bytes written. Reads
// sign-extend (horiz and relatives go negative).
inline void write_field_bytes(unsigned char* base, unsigned rva, int width, long value) {
  const unsigned v = (unsigned)value;
  for (int i = 0; i < width; i++)
    base[rva + i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
}
inline long read_field_bytes(const unsigned char* base, unsigned rva, int width) {
  unsigned v = 0;
  for (int i = 0; i < width; i++)
    v |= (unsigned)base[rva + i] << (8 * i);
  if (width < 4) {
    const unsigned signBit = 1u << (width * 8 - 1);
    if (v & signBit) v |= ~((signBit << 1) - 1u);
  }
  return (long)v;
}

// Silent aim is a bracket: save five player_struct fields, substitute the solved shot, call the real `shoot` once,
// restore byte for byte within one tic. ang is the yaw every handler fires along (shoot VA 0x004f3640, read at
// 0x004f36e0); auto_aim must be 0 or aim() overwrites the substitution; oang/ohoriz travel with their partners.
enum SilentField {
  SF_ANG = 0, SF_OANG, SF_HORIZ, SF_OHORIZ, SF_AUTO_AIM,
  SF_COUNT
};

// auto_aim is a long (`aim` compares it as a dword).
const int PS_AUTO_AIM_WIDTH = 4;

inline unsigned silent_field_offset(int field) {
  switch (field) {
    case SF_ANG:      return rva::PS_OFFSET_ANG;
    case SF_OANG:     return rva::PS_OFFSET_OANG;
    case SF_HORIZ:    return rva::PS_OFFSET_HORIZ;
    case SF_OHORIZ:   return rva::PS_OFFSET_OHORIZ;
    case SF_AUTO_AIM: return rva::PS_AUTO_AIM;
    default:          return 0;
  }
}

// Declared width in bytes, same for save and restore.
inline int silent_field_width(int field) {
  switch (field) {
    case SF_ANG:      return VIEW_FIELD_WIDTHS[VF_ANG];
    case SF_OANG:     return VIEW_FIELD_WIDTHS[VF_OANG];
    case SF_HORIZ:    return VIEW_FIELD_WIDTHS[VF_HORIZ];
    case SF_OHORIZ:   return VIEW_FIELD_WIDTHS[VF_OHORIZ];
    case SF_AUTO_AIM: return PS_AUTO_AIM_WIDTH;
    default:          return 0;
  }
}

// Out-parameter shape like view_write_value, since a plain `return 0` couldn't tell auto_aim's legitimate 0.
inline bool silent_field_value(int field, long ang, long horiz, long* out) {
  if (!out) return false;
  switch (field) {
    case SF_ANG:
    case SF_OANG:      *out = ang;   return true;
    case SF_HORIZ:
    case SF_OHORIZ:    *out = horiz; return true;
    case SF_AUTO_AIM:  *out = 0;     return true;
    default:           return false;
  }
}

// The bracket as three pure passes over a byte buffer (silent.cpp can't link into test_host). The caller must have
// bounds-checked the player index.
struct SilentSave {
  long v[SF_COUNT];
  bool armed;
};

inline bool silent_save_fields(const unsigned char* base, unsigned psRva, SilentSave* s) {
  if (!base || !s) return false;
  for (int f = 0; f < SF_COUNT; f++)
    s->v[f] = read_field_bytes(base, psRva + silent_field_offset(f), silent_field_width(f));
  s->armed = true;
  return true;
}

inline bool silent_substitute_fields(unsigned char* base, unsigned psRva, long ang, long horiz) {
  if (!base) return false;
  for (int f = 0; f < SF_COUNT; f++) {
    long v = 0;
    if (!silent_field_value(f, ang, horiz, &v)) continue;
    write_field_bytes(base, psRva + silent_field_offset(f), silent_field_width(f), v);
  }
  return true;
}

// Puts all five back and disarms, so a second call is a no-op (the destructor is safe after an explicit restore).
inline bool silent_restore_fields(unsigned char* base, unsigned psRva, SilentSave* s) {
  if (!base || !s || !s->armed) return false;
  s->armed = false;
  for (int f = 0; f < SF_COUNT; f++)
    write_field_bytes(base, psRva + silent_field_offset(f), silent_field_width(f), s->v[f]);
  return true;
}

// The bracket as a type, so its lifetime is testable.
struct SilentBracket;
inline bool silent_bracket_release(SilentBracket* b);

struct SilentBracket {
  unsigned char* base;
  unsigned       psRva;
  SilentSave     save;
  SilentBracket() : base(0), psRva(0) { save.armed = false; }
  ~SilentBracket() { silent_bracket_release(this); }
private:
  // Non-copyable: a copied armed bracket would restore the same fields twice.
  SilentBracket(const SilentBracket&);
  SilentBracket& operator=(const SilentBracket&);
};

inline bool silent_bracket_arm(SilentBracket* b, unsigned char* base, unsigned psRva,
                               long ang, long horiz) {
  if (!b || !base) return false;
  if (b->save.armed) return false;
  SilentSave s;
  s.armed = false;
  if (!silent_save_fields(base, psRva, &s)) return false;
  b->base = base;
  b->psRva = psRva;
  b->save = s;
  silent_substitute_fields(base, psRva, ang, horiz);
  return true;
}

// A disarmed bracket writes nothing.
inline bool silent_bracket_release(SilentBracket* b) {
  if (!b) return false;
  return silent_restore_fields(b->base, b->psRva, &b->save);
}

// View-freeze action and settling window, pure. Zeroing `loc` while the panel is open arms a return_to_center countdown
// that can run after the freeze stops; the settling window (SETTLING_WINDOW_TICS = 2, ~65 ms) forces all six fields.
enum ViewFreezeAction {
  VFA_NONE,
  VFA_SETTLING,
  VFA_FREECAM,
  VFA_FULL,
};

inline ViewFreezeAction view_freeze_action(bool panelOpen, bool freecamActive,
                                           int settlingTicsRemaining) {
  if (panelOpen) return VFA_FULL;
  if (freecamActive) return VFA_FREECAM;
  return settlingTicsRemaining > 0 ? VFA_SETTLING : VFA_NONE;
}

// Who owns the engine-side input lockout (globals cleared every frame, `loc` every tic, four input gates saved, zeroed,
// restored). Keyed on any owner: opening the panel while the camera flies would save already-zeroed gates and
// "restore" zeros forever (mouse dead for the session).
inline bool freeze_wanted(bool panelOpen, bool freecamActive) {
  return panelOpen || freecamActive;
}

enum FreezeTransition {
  FT_NONE,
  FT_ENGAGE,   // nobody had it and somebody wants it: save and zero
  FT_RELEASE,  // held and nobody wants it: restore and settle
};

inline FreezeTransition freeze_transition(bool wanted, bool engaged) {
  if (wanted && !engaged) return FT_ENGAGE;
  if (!wanted && engaged) return FT_RELEASE;
  return FT_NONE;
}

// Deferred init: DllMain runs under the loader lock, so config load, log open, thunk patching and engine reads move to
// the first SDL_PollEvent on the game thread. The caller moves to RUNNING before the body (a guard set after re-enters).
enum { INIT_PENDING = 0, INIT_RUNNING = 1, INIT_DONE = 2 };
inline bool init_should_run(int state) { return state == INIT_PENDING; }
inline int  init_next_state(int state) {
  if (state == INIT_PENDING) return INIT_RUNNING;
  return INIT_DONE;
}

// Which path DllMain takes. If its one IAT write fails, hooked_poll never runs and the mod would vanish silently, so
// failure runs the body inline under the loader lock (the risk, but only where the alternative is total silence).
enum InitEntryOutcome {
  INIT_ENTRY_DEFERRED = 0,
  INIT_ENTRY_INLINE   = 1,
};
inline InitEntryOutcome init_entry_outcome(bool hookOk) {
  return hookOk ? INIT_ENTRY_DEFERRED : INIT_ENTRY_INLINE;
}

// Decrements while the window runs (panel closed), holds at 0, untouched while open.
inline int settling_tics_after_call(bool panelOpen, int settlingTicsRemaining) {
  if (panelOpen) return settlingTicsRemaining;
  return settlingTicsRemaining > 0 ? settlingTicsRemaining - 1 : 0;
}

struct SampleSummary { long minVal; long maxVal; int distinctCount; };

inline SampleSummary summarize_samples(const long* samples, int count) {
  SampleSummary s = { 0, 0, 0 };
  if (count <= 0) return s;
  s.minVal = samples[0];
  s.maxVal = samples[0];
  for (int i = 1; i < count; i++) {
    if (samples[i] < s.minVal) s.minVal = samples[i];
    if (samples[i] > s.maxVal) s.maxVal = samples[i];
  }
  int distinct = 0;
  for (int i = 0; i < count; i++) {
    bool seenBefore = false;
    for (int j = 0; j < i; j++) {
      if (samples[j] == samples[i]) { seenBefore = true; break; }
    }
    if (!seenBefore) distinct++;
  }
  s.distinctCount = distinct;
  return s;
}

// Collapses the nine captured view values into those written while frozen: if the mouse moved at open, horiz/ohoriz
// (and ang/oang) differed and Build interpolated between them forever (the jitter), so the o-fields take the current value.
struct ViewCollapsedValues {
  long horiz, ohoriz, horizoff, ohorizoff, lookAng, returnToCenter, ang, oang, angvel;
};

inline ViewCollapsedValues collapse_view_values(
    long savedHoriz, long savedOhoriz, long savedHorizoff, long savedOhorizoff,
    long savedLookAng, long savedReturnToCenter,
    long savedAng, long savedOang, long savedAngvel) {
  // savedOhoriz, savedOhorizoff and savedOang are for logging only.
  (void)savedOhoriz;
  (void)savedOhorizoff;
  (void)savedOang;
  (void)savedAngvel;
  ViewCollapsedValues v;
  v.horiz          = savedHoriz;
  v.ohoriz         = savedHoriz;
  v.horizoff       = savedHorizoff;
  v.ohorizoff      = savedHorizoff;
  v.lookAng        = savedLookAng;
  v.returnToCenter = savedReturnToCenter;
  v.ang            = savedAng;
  v.oang           = savedAng;
  v.angvel         = 0;
  return v;
}

// The two pure brackets follow SilentBracket's shape, so the suite drives them on synthetic memory.

// The free camera's field set. displayrooms (VA 0x004C0AE0) interpolates each pair as o + mulscale16(cur - o,
// smoothratio), adds look_ang and rolls by the orotscrnang pair: write both members of every pair, zero look_ang.
// cursectnum clips the render.
enum CameraField {
  CF_POSX = 0, CF_OPOSX,
  CF_POSY,     CF_OPOSY,
  CF_POSZ,     CF_OPOSZ,
  CF_ANG,      CF_OANG,
  CF_HORIZ,    CF_OHORIZ,
  CF_HORIZOFF, CF_OHORIZOFF,
  CF_PYOFF,    CF_OPYOFF,
  CF_ROTSCRNANG, CF_OROTSCRNANG,
  CF_CURSECTNUM, CF_POSZV, CF_LOOK_ANG,
  CF_COUNT
};

// oposx/oposy/oposz/oang/ohoriz carry the PREVIOUS tic's pose: writing the same value to both leaves nothing to
// interpolate (the laggy-camera finding).
struct CameraFields {
  int posx, posy, posz, ang, horiz, cursectnum;
  int oposx, oposy, oposz, oang, ohoriz;
};

inline bool camera_preconditions_ok(int newowner, int overShoulderOn) {
  return newowner < 0 && overShoulderOn == 0;
}

inline unsigned camera_field_offset(int field) {
  switch (field) {
    case CF_POSX:        return rva::PS_POSX;
    case CF_OPOSX:       return rva::PS_OPOSX;
    case CF_POSY:        return rva::PS_POSY;
    case CF_OPOSY:       return rva::PS_OPOSY;
    case CF_POSZ:        return rva::PS_POSZ;
    case CF_OPOSZ:       return rva::PS_OPOSZ;
    case CF_ANG:         return rva::PS_OFFSET_ANG;
    case CF_OANG:        return rva::PS_OFFSET_OANG;
    case CF_HORIZ:       return rva::PS_OFFSET_HORIZ;
    case CF_OHORIZ:      return rva::PS_OFFSET_OHORIZ;
    case CF_HORIZOFF:    return rva::PS_HORIZOFF;
    case CF_OHORIZOFF:   return rva::PS_OFFSET_OHORIZOFF;
    case CF_PYOFF:       return rva::PS_PYOFF;
    case CF_OPYOFF:      return rva::PS_OPYOFF;
    case CF_ROTSCRNANG:  return rva::PS_ROTSCRNANG;
    case CF_OROTSCRNANG: return rva::PS_OROTSCRNANG;
    case CF_CURSECTNUM:  return rva::PS_CURSECTNUM;
    case CF_POSZV:       return rva::PS_POSZV;
    case CF_LOOK_ANG:    return rva::PS_OFFSET_LOOK_ANG;
    default:             return 0;
  }
}

inline int camera_field_width(int field) {
  switch (field) {
    case CF_POSX:  case CF_OPOSX:
    case CF_POSY:  case CF_OPOSY:
    case CF_POSZ:  case CF_OPOSZ:
    case CF_PYOFF: case CF_OPYOFF:
    case CF_POSZV:       return 4;
    case CF_ANG:         return VIEW_FIELD_WIDTHS[VF_ANG];
    case CF_OANG:        return VIEW_FIELD_WIDTHS[VF_OANG];
    case CF_HORIZ:       return VIEW_FIELD_WIDTHS[VF_HORIZ];
    case CF_OHORIZ:      return VIEW_FIELD_WIDTHS[VF_OHORIZ];
    case CF_HORIZOFF:    return VIEW_FIELD_WIDTHS[VF_HORIZOFF];
    case CF_OHORIZOFF:   return VIEW_FIELD_WIDTHS[VF_OHORIZOFF];
    case CF_LOOK_ANG:    return VIEW_FIELD_WIDTHS[VF_LOOK_ANG];
    case CF_ROTSCRNANG:
    case CF_OROTSCRNANG:
    case CF_CURSECTNUM:  return 2;
    default:             return 0;
  }
}

// Out-parameter shape like view_write_value.
inline bool camera_field_value(int field, const CameraFields& c, long* out) {
  if (!out) return false;
  switch (field) {
    case CF_POSX:        *out = c.posx;       return true;
    case CF_OPOSX:       *out = c.oposx;      return true;
    case CF_POSY:        *out = c.posy;       return true;
    case CF_OPOSY:       *out = c.oposy;      return true;
    case CF_POSZ:        *out = c.posz;       return true;
    case CF_OPOSZ:       *out = c.oposz;      return true;
    case CF_ANG:         *out = c.ang;        return true;
    case CF_OANG:        *out = c.oang;       return true;
    case CF_HORIZ:       *out = c.horiz;      return true;
    case CF_OHORIZ:      *out = c.ohoriz;     return true;
    case CF_CURSECTNUM:  *out = c.cursectnum; return true;
    // Zeroed, each for its own reason (displayrooms): horizoff/ohorizoff the weapon kick, pyoff/opyoff the view bob,
    // rotscrnang the damage roll, look_ang the peek added onto the yaw, poszv the fall the player must not resume.
    case CF_HORIZOFF:   case CF_OHORIZOFF:
    case CF_PYOFF:      case CF_OPYOFF:
    case CF_ROTSCRNANG: case CF_OROTSCRNANG:
    case CF_POSZV:      case CF_LOOK_ANG:  *out = 0;    return true;
    default: return false;
  }
}

// What the teleport writes (CameraField reused: one table of positions). The three pairs are whole or nothing (posx
// without oposx streaks the player back). The view stays the camera's own.
inline bool teleport_write_value(int field, int x, int y, int z, int sect, long* out) {
  if (!out) return false;
  switch (field) {
    case CF_POSX: case CF_OPOSX:  *out = x;    return true;
    case CF_POSY: case CF_OPOSY:  *out = y;    return true;
    case CF_POSZ: case CF_OPOSZ:  *out = z;    return true;
    case CF_CURSECTNUM:           *out = sect; return true;
    // The fall the camera went up in; left alone they'd drop through the floor (freecam-accuracy.md B).
    case CF_POSZV:                *out = 0;    return true;
    default: return false;
  }
}

// Bounded like player_view_field_rvas (input.h). False for an index outside ps[].
inline bool camera_field_rvas(long idx, unsigned* out) {
  if (!out) return false;
  if (!player_index_valid(idx, MAX_PLAYER_INDEX)) return false;
  for (int f = 0; f < CF_COUNT; f++)
    out[f] = player_field_rva(rva::RVA_PS, idx, rva::PS_STRIDE, camera_field_offset(f));
  return true;
}

// Start of player `idx`'s struct, or false outside ps[]. The bracket can't bound psRva, so one passed to
// camera_bracket_arm must come from this or camera_field_rvas.
inline bool camera_ps_rva(long idx, unsigned* out) {
  if (!out) return false;
  if (!player_index_valid(idx, MAX_PLAYER_INDEX)) return false;
  *out = player_field_rva(rva::RVA_PS, idx, rva::PS_STRIDE, 0);
  return true;
}

struct CameraSave {
  long v[CF_COUNT];
  bool armed;
};

inline bool camera_save_fields(const unsigned char* base, unsigned psRva, CameraSave* s) {
  if (!base || !s) return false;
  for (int f = 0; f < CF_COUNT; f++)
    s->v[f] = read_field_bytes(base, psRva + camera_field_offset(f), camera_field_width(f));
  s->armed = true;
  return true;
}

inline bool camera_substitute_fields(unsigned char* base, unsigned psRva, const CameraFields& c) {
  if (!base) return false;
  for (int f = 0; f < CF_COUNT; f++) {
    long v = 0;
    if (!camera_field_value(f, c, &v)) continue;
    write_field_bytes(base, psRva + camera_field_offset(f), camera_field_width(f), v);
  }
  return true;
}

// Puts every saved value back and disarms. One field goes back "wrong" on purpose: displayrooms writes `orotscrnang =
// rotscrnang` each frame, so the pre-frame value stalls the roll interpolation while armed; it re-converges after release.
inline bool camera_restore_fields(unsigned char* base, unsigned psRva, CameraSave* s) {
  if (!base || !s || !s->armed) return false;
  s->armed = false;
  for (int f = 0; f < CF_COUNT; f++)
    write_field_bytes(base, psRva + camera_field_offset(f), camera_field_width(f), s->v[f]);
  return true;
}

struct CameraBracket;
inline bool camera_bracket_release(CameraBracket* b);

struct CameraBracket {
  unsigned char* base;
  unsigned       psRva;
  CameraSave     save;
  CameraBracket() : base(0), psRva(0) { save.armed = false; }
  ~CameraBracket() { camera_bracket_release(this); }
private:
  CameraBracket(const CameraBracket&);
  CameraBracket& operator=(const CameraBracket&);
};

inline bool camera_bracket_arm(CameraBracket* b, unsigned char* base, unsigned psRva,
                               const CameraFields& c) {
  if (!b || !base) return false;
  if (b->save.armed) return false;
  CameraSave s;
  s.armed = false;
  if (!camera_save_fields(base, psRva, &s)) return false;
  b->base = base;
  b->psRva = psRva;
  b->save = s;
  camera_substitute_fields(base, psRva, c);
  return true;
}

inline bool camera_bracket_release(CameraBracket* b) {
  if (!b) return false;
  return camera_restore_fields(b->base, b->psRva, &b->save);
}

const int ACC_SITE_BYTES = 5;

// Six is every site there is (three weapon pairs); the bound keeps `count` inside the save array.
const int ACC_MAX_SITES = 6;

const unsigned ACC_PROT_RWX = 0x40;

// Protection is page-granular: two sites on a page are one protection. A per-site loop would fault the second write and
// record the first RWX as "previous", leaving .text writable. So: collect distinct pages, change each once, write,
// restore bytes before pages.
const unsigned ACC_PAGE_SIZE = 0x1000u;

// A 5-byte site can straddle a page, so N sites touch up to 2N pages.
const int ACC_MAX_PAGES = ACC_MAX_SITES * 2;

inline unsigned char* acc_page_of(unsigned char* p) {
  if (!p) return 0;
  return p - (size_t)((size_t)p & (size_t)(ACC_PAGE_SIZE - 1u));
}

// Every DISTINCT page the sites' five bytes live on, page-aligned; the count, or -1 on a bad argument.
inline int acc_collect_pages(unsigned char* base, const rva::AccSite* sites, int count,
                             unsigned char** out) {
  if (!base || !sites || !out) return -1;
  if (count < 1 || count > ACC_MAX_SITES) return -1;
  int n = 0;
  for (int i = 0; i < count; i++) {
    unsigned char* first = acc_page_of(base + sites[i].rva);
    unsigned char* last  = acc_page_of(base + sites[i].rva + (unsigned)(ACC_SITE_BYTES - 1));
    for (unsigned char* pg = first; ; pg += ACC_PAGE_SIZE) {
      bool seen = false;
      for (int j = 0; j < n; j++) if (out[j] == pg) { seen = true; break; }
      if (!seen) {
        if (n >= ACC_MAX_PAGES) return -1;
        out[n++] = pg;
      }
      if (pg == last) break;
    }
  }
  return n;
}

inline bool acc_sites_overlap(const rva::AccSite* sites, int count) {
  if (!sites || count < 2) return false;
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      const unsigned a = sites[i].rva, b = sites[j].rva;
      const unsigned lo = a < b ? a : b;
      const unsigned hi = a < b ? b : a;
      if (hi - lo < (unsigned)ACC_SITE_BYTES) return true;   // hi >= lo, no wrap
    }
  }
  return false;
}

typedef void (*AccWriteWatchFn)(void* addr, unsigned size);
inline AccWriteWatchFn& acc_write_watch_fn() { static AccWriteWatchFn fn = 0; return fn; }

inline void acc_write_site(unsigned char* dst, const unsigned char* src) {
  if (!dst || !src) return;
  const AccWriteWatchFn watch = acc_write_watch_fn();
  if (watch) watch(dst, (unsigned)ACC_SITE_BYTES);
  for (int k = 0; k < ACC_SITE_BYTES; k++) dst[k] = src[k];
}

// The page-protection call, injected (a VirtualProtect wrapper in engine.cpp, a stub in the suite).
typedef int (*AccProtectFn)(void* addr, unsigned size, unsigned newProt, unsigned* oldProtOut);

// One program-wide instance, null until installed. Fail closed: patch_bracket_arm refuses while null, since writing a
// page it couldn't make writable would write into read-only code.
inline AccProtectFn& acc_protect_fn() { static AccProtectFn fn = 0; return fn; }

inline void acc_expected_bytes(unsigned siteRva, unsigned char* out) {
  if (!out) return;
  const unsigned rel = thunk_encode_rel32(siteRva, rva::KRAND_THUNK_RVA);
  out[0] = 0xE8;
  out[1] = (unsigned char)( rel        & 0xFFu);
  out[2] = (unsigned char)((rel >>  8) & 0xFFu);
  out[3] = (unsigned char)((rel >> 16) & 0xFFu);
  out[4] = (unsigned char)((rel >> 24) & 0xFFu);
}

inline void acc_patch_bytes(unsigned char centre, unsigned char* out) {
  if (!out) return;
  out[0] = 0xB8;
  out[1] = centre;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = 0x00;
}

inline bool acc_site_matches(const unsigned char* base, unsigned siteRva) {
  if (!base) return false;
  unsigned char want[ACC_SITE_BYTES];
  acc_expected_bytes(siteRva, want);
  for (int i = 0; i < ACC_SITE_BYTES; i++)
    if (base[siteRva + (unsigned)i] != want[i]) return false;
  return true;
}

struct PatchSave {
  unsigned      rva;
  unsigned char orig[ACC_SITE_BYTES];
};

struct PatchPage {
  unsigned char* addr;
  unsigned       oldProt;
};

struct PatchBracket;
inline bool patch_bracket_release(PatchBracket* b);

struct PatchBracket {
  unsigned char* base;
  int            count;
  int            pageCount;
  PatchSave      save[ACC_MAX_SITES];
  PatchPage      pages[ACC_MAX_PAGES];
  bool           armed;
  PatchBracket() : base(0), count(0), pageCount(0), armed(false) {}
  ~PatchBracket() { patch_bracket_release(this); }
private:
  PatchBracket(const PatchBracket&);
  PatchBracket& operator=(const PatchBracket&);
};

// Five passes (order is the contract): 1 refuse a repeated or overlapping site, 2 fingerprint every site, 3 snapshot
// every original, 4 make every distinct page writable, 5 write every patch. All or nothing.
inline bool patch_bracket_arm(PatchBracket* b, unsigned char* base,
                              const rva::AccSite* sites, int count) {
  if (!b || !base || !sites) return false;
  if (count < 1 || count > ACC_MAX_SITES) return false;
  if (b->armed) return false;
  const AccProtectFn protect = acc_protect_fn();
  if (!protect) return false;

  if (acc_sites_overlap(sites, count)) return false;

  for (int i = 0; i < count; i++)
    if (!acc_site_matches(base, sites[i].rva)) return false;

  for (int i = 0; i < count; i++) {
    b->save[i].rva = sites[i].rva;
    for (int k = 0; k < ACC_SITE_BYTES; k++)
      b->save[i].orig[k] = base[sites[i].rva + (unsigned)k];
  }

  unsigned char* pageAddr[ACC_MAX_PAGES];
  const int pages = acc_collect_pages(base, sites, count, pageAddr);
  if (pages < 1) return false;
  for (int pg = 0; pg < pages; pg++) {
    unsigned old = 0;
    if (!protect(pageAddr[pg], ACC_PAGE_SIZE, ACC_PROT_RWX, &old)) {
      for (int q = 0; q < pg; q++) {
        unsigned scratch = 0;
        protect(b->pages[q].addr, ACC_PAGE_SIZE, b->pages[q].oldProt, &scratch);
      }
      b->pageCount = 0;
      return false;
    }
    b->pages[pg].addr = pageAddr[pg];
    b->pages[pg].oldProt = old;
  }
  b->pageCount = pages;

  b->base = base;
  b->count = count;
  for (int i = 0; i < count; i++) {
    unsigned char patch[ACC_SITE_BYTES];
    acc_patch_bytes(sites[i].centre, patch);
    acc_write_site(base + sites[i].rva, patch);
  }
  b->armed = true;
  return true;
}

// Release on every path (the destructor calls it). Every byte first, then every page's protection: interleaved, a
// restore would make a page read-only before the next site on it is restored, faulting mid-shoot(). Disarms first.
inline bool patch_bracket_release(PatchBracket* b) {
  if (!b || !b->armed) return false;
  b->armed = false;
  for (int i = 0; i < b->count; i++)
    acc_write_site(b->base + b->save[i].rva, b->save[i].orig);
  const AccProtectFn protect = acc_protect_fn();
  if (protect) {
    for (int pg = 0; pg < b->pageCount; pg++) {
      unsigned scratch = 0;
      protect(b->pages[pg].addr, ACC_PAGE_SIZE, b->pages[pg].oldProt, &scratch);
    }
  }
  b->pageCount = 0;
  return true;
}


const int CODE_PATCH_MAX_LEN   = 8;
struct CodePatch {
  unsigned      rva;
  int           len;
  unsigned char expected[CODE_PATCH_MAX_LEN];
  unsigned char replacement[CODE_PATCH_MAX_LEN];
};

const int CODE_PATCH_MAX_SITES = 4;
const int CODE_PATCH_MAX_PAGES = CODE_PATCH_MAX_SITES * 2;

const int DISPLAYROOMS_CLAMP_COUNT = 4;
const CodePatch DISPLAYROOMS_CLAMP_SITES[DISPLAYROOMS_CLAMP_COUNT] = {
  // JNZ 0x004c1497 -> EB: skips the gated truecz/truefz bridge clamp.
  { rva::CLAMP_BRIDGE_RVA,   2, { 0x75, 0x40 }, { 0xEB, 0x40 } },
  // JL  0x004c14ef -> EB: skips the unconditional getzsofslope clamp.
  { rva::CLAMP_SECTOR_RVA,   2, { 0x7C, 0x50 }, { 0xEB, 0x50 } },
  // JLE 0x004c1505 -> EB: skips `horiz = 299`.
  { rva::CLAMP_HORIZ_HI_RVA, 2, { 0x7E, 0x0B }, { 0xEB, 0x0B } },
  // JGE 0x004c1517 -> EB: skips `horiz = -99`.
  { rva::CLAMP_HORIZ_LO_RVA, 2, { 0x7D, 0x09 }, { 0xEB, 0x09 } },
};

inline bool code_patch_sites_overlap(const CodePatch* sites, int count) {
  if (!sites || count < 2) return false;
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      const unsigned a = sites[i].rva, b = sites[j].rva;
      const unsigned aEnd = a + (unsigned)sites[i].len;
      const unsigned bEnd = b + (unsigned)sites[j].len;
      if (a < bEnd && b < aEnd) return true;
    }
  }
  return false;
}

inline int code_patch_collect_pages(unsigned char* base, const CodePatch* sites, int count,
                                    unsigned char** out) {
  if (!base || !sites || !out) return -1;
  if (count < 1 || count > CODE_PATCH_MAX_SITES) return -1;
  int n = 0;
  for (int i = 0; i < count; i++) {
    if (sites[i].len < 1 || sites[i].len > CODE_PATCH_MAX_LEN) return -1;
    unsigned char* first = acc_page_of(base + sites[i].rva);
    unsigned char* last  = acc_page_of(base + sites[i].rva + (unsigned)(sites[i].len - 1));
    for (unsigned char* pg = first; ; pg += ACC_PAGE_SIZE) {
      bool seen = false;
      for (int j = 0; j < n; j++) if (out[j] == pg) { seen = true; break; }
      if (!seen) {
        if (n >= CODE_PATCH_MAX_PAGES) return -1;
        out[n++] = pg;
      }
      if (pg == last) break;
    }
  }
  return n;
}

inline bool code_patch_site_matches(const unsigned char* base, const CodePatch& site) {
  if (!base) return false;
  if (site.len < 1 || site.len > CODE_PATCH_MAX_LEN) return false;
  for (int i = 0; i < site.len; i++)
    if (base[site.rva + (unsigned)i] != site.expected[i]) return false;
  return true;
}

inline void code_patch_write_site(unsigned char* dst, const unsigned char* src, int len) {
  if (!dst || !src) return;
  const AccWriteWatchFn watch = acc_write_watch_fn();
  if (watch) watch(dst, (unsigned)len);
  for (int k = 0; k < len; k++) dst[k] = src[k];
}

struct CodePatchSave {
  unsigned      rva;
  int           len;
  unsigned char orig[CODE_PATCH_MAX_LEN];
};
struct CodePatchPage {
  unsigned char* addr;
  unsigned       oldProt;
};

struct CodePatchBracket;
inline bool code_patch_bracket_release(CodePatchBracket* b);

struct CodePatchBracket {
  unsigned char* base;
  int            count;
  int            pageCount;
  CodePatchSave  save[CODE_PATCH_MAX_SITES];
  CodePatchPage  pages[CODE_PATCH_MAX_PAGES];
  bool           armed;
  CodePatchBracket() : base(0), count(0), pageCount(0), armed(false) {}
  ~CodePatchBracket() { code_patch_bracket_release(this); }
private:
  CodePatchBracket(const CodePatchBracket&);
  CodePatchBracket& operator=(const CodePatchBracket&);
};

inline bool code_patch_bracket_arm(CodePatchBracket* b, unsigned char* base,
                                   const CodePatch* sites, int count) {
  if (!b || !base || !sites) return false;
  if (count < 1 || count > CODE_PATCH_MAX_SITES) return false;
  if (b->armed) return false;
  const AccProtectFn protect = acc_protect_fn();
  if (!protect) return false;

  if (code_patch_sites_overlap(sites, count)) return false;

  for (int i = 0; i < count; i++)
    if (!code_patch_site_matches(base, sites[i])) return false;

  for (int i = 0; i < count; i++) {
    b->save[i].rva = sites[i].rva;
    b->save[i].len = sites[i].len;
    for (int k = 0; k < sites[i].len; k++)
      b->save[i].orig[k] = base[sites[i].rva + (unsigned)k];
  }

  unsigned char* pageAddr[CODE_PATCH_MAX_PAGES];
  const int pages = code_patch_collect_pages(base, sites, count, pageAddr);
  if (pages < 1) return false;
  for (int pg = 0; pg < pages; pg++) {
    unsigned old = 0;
    if (!protect(pageAddr[pg], ACC_PAGE_SIZE, ACC_PROT_RWX, &old)) {
      for (int q = 0; q < pg; q++) {
        unsigned scratch = 0;
        protect(b->pages[q].addr, ACC_PAGE_SIZE, b->pages[q].oldProt, &scratch);
      }
      b->pageCount = 0;
      return false;
    }
    b->pages[pg].addr = pageAddr[pg];
    b->pages[pg].oldProt = old;
  }
  b->pageCount = pages;

  b->base = base;
  b->count = count;
  for (int i = 0; i < count; i++)
    code_patch_write_site(base + sites[i].rva, sites[i].replacement, sites[i].len);
  b->armed = true;
  return true;
}

inline bool code_patch_bracket_release(CodePatchBracket* b) {
  if (!b || !b->armed) return false;
  b->armed = false;
  for (int i = 0; i < b->count; i++)
    code_patch_write_site(b->base + b->save[i].rva, b->save[i].orig, b->save[i].len);
  const AccProtectFn protect = acc_protect_fn();
  if (protect) {
    for (int pg = 0; pg < b->pageCount; pg++) {
      unsigned scratch = 0;
      protect(b->pages[pg].addr, ACC_PAGE_SIZE, b->pages[pg].oldProt, &scratch);
    }
  }
  b->pageCount = 0;
  return true;
}
