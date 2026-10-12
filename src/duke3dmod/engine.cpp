#include "engine.h"
#include "shot.h"
#include "common.h"
#include "hooks.h"
#include "log.h"
#include "target.h"
#include "input.h"
#include "freecam.h"
#include "nearsect.h"
#include <windows.h>

typedef short (__cdecl *BadguyFn)(const void*);
static BadguyFn s_badguy = nullptr;

// Per-walk cycle-protection counter (WalkCounter, common.h): a list holds at most MAX_SPRITES sprites, so more means
// nextspritestat is corrupt. Valid for ONE walk at a time: esp_draw and aim_tic walk the lists sequentially, never nested.
static WalkCounter s_walk = { 0, false };
static bool s_walkCorruptionLogged = false;

// Resolves badguy's address once, from mod_deferred_init (called, not hooked). Its own step so dropping one in a
// refactor cannot silently disable the other.
void engine_install_badguy() {
  s_badguy = (BadguyFn)(g_base + rva::BADGUY);
  LOGI("engine: badguy resolved at %p", (void*)s_badguy);
}

static bool sprite_index_ok(int i) { return i >= 0 && i < rva::MAX_SPRITES; }

int engine_sprite_field_s(int i, unsigned off) {
  if (!engine_verified() || !sprite_index_ok(i) || !sprite_field_read_ok(off, 2)) return 0;
  return *(short*)(g_base + rva::SPRITE + (unsigned)i * rva::SPRITE_STRIDE + off);
}

int engine_sprite_field_u8(int i, unsigned off) {
  if (!engine_verified() || !sprite_index_ok(i) || !sprite_field_read_ok(off, 1)) return 0;
  return *(unsigned char*)(g_base + rva::SPRITE + (unsigned)i * rva::SPRITE_STRIDE + off);
}

int engine_sprite_field_l(int i, unsigned off) {
  if (!engine_verified() || !sprite_index_ok(i) || !sprite_field_read_ok(off, 4)) return 0;
  return *(int*)(g_base + rva::SPRITE + (unsigned)i * rva::SPRITE_STRIDE + off);
}

static bool tile_index_ok(int picnum) { return picnum >= 0 && picnum < rva::MAX_TILES; }

int engine_tilesizx(int picnum) {
  if (!engine_verified() || !tile_index_ok(picnum)) return 0;
  return *(short*)(g_base + rva::TILESIZX + (unsigned)picnum * 2);
}

int engine_tilesizy(int picnum) {
  if (!engine_verified() || !tile_index_ok(picnum)) return 0;
  return *(short*)(g_base + rva::TILESIZY + (unsigned)picnum * 2);
}

// An actor's `strength` (max health) from the compiled CON script, the only place it exists; mirrors spawn()'s read (VA
// 0x004c23d4). Picnum bounded by 9500 entries, pointer checked (actor_script_ptr_ok), SEH, value bounded to a short.
int engine_actor_strength(int picnum) {
  if (!engine_verified() || !tile_index_ok(picnum)) return 0;
  int value = 0;
  __try {
    const unsigned entry =
        *(const unsigned*)(g_base + rva::ACTORSCRPTR + (unsigned)picnum * 4u);
    if (!actor_script_ptr_ok((unsigned)(UINT_PTR)g_base, entry)) return 0;
    value = *(const int*)(UINT_PTR)entry;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
  if (value <= 0 || value > ACTOR_STRENGTH_MAX) return 0;
  return value;
}

int engine_first_sprite_of_stat(int stat) {
  if (!engine_verified() || stat < 0 || stat > rva::MAX_STATNUM) return -1;
  walk_counter_begin(s_walk);
  s_walkCorruptionLogged = false;
  const short v = *(short*)(g_base + rva::HEADSPRITESTAT + (unsigned)stat * 2);
  return sprite_index_ok(v) ? v : -1;
}

int engine_next_sprite(int i) {
  if (!engine_verified() || !sprite_index_ok(i)) return -1;
  if (!walk_counter_advance(s_walk, rva::MAX_SPRITES)) {
    // nextspritestat forms a cycle or is corrupt: end the walk to avoid a render-thread hang; logged once per corruption.
    if (!s_walkCorruptionLogged) {
      LOGI("engine: nextspritestat corruption detected at step %d", s_walk.steps);
      s_walkCorruptionLogged = true;
    }
    return -1;
  }
  const short v = *(short*)(g_base + rva::NEXTSPRITESTAT + (unsigned)i * 2);
  return sprite_index_ok(v) ? v : -1;
}

int engine_numsectors() {
  if (!engine_verified()) return 0;
  const short n = *(short*)(g_base + rva::NUMSECTORS);
  return (n < 0 || n > rva::MAX_SECTORS) ? 0 : n;
}

int engine_numwalls() {
  if (!engine_verified()) return 0;
  const short n = *(short*)(g_base + rva::NUMWALLS);
  return (n < 0 || n > rva::MAX_WALLS) ? 0 : n;
}

int engine_sector_lotag(int sectnum) {
  if (!engine_verified() || !sector_index_ok(sectnum, engine_numsectors())) return -1;
  return *(short*)(g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE + SEC_LOTAG);
}

// An undiscovered secret area. -1 never equals SECRET_SECTOR_LOTAG, so an unreadable sector is not a secret.
bool engine_player_in_play() {
  if (!engine_verified()) return false;
  unsigned psRva = 0;
  if (!camera_ps_rva(engine_myconnectindex(), &psRva)) return false;
  return read_field_bytes(g_base, psRva + rva::PS_GM, 1) == 4;
}

int engine_addon_index() {
  if (!engine_verified()) return 0;
  const int a = *(const int*)(g_base + rva::ADDON);
  return (a >= 0 && a <= 3) ? a : 0;
}

bool engine_sector_is_secret(int sectnum) {
  return engine_sector_lotag(sectnum) == SECRET_SECTOR_LOTAG;
}

// The average of a sector's wall vertices plus its floor z. The one loop with a live trip count, so wallptr and wallnum
// are checked as a RANGE (wall_range_ok). No SEH: every address is a bounds-checked index into a static array.
bool engine_sector_centre(int sectnum, int* cx, int* cy, int* cz) {
  if (!cx || !cy || !cz) return false;
  if (!engine_verified() || !sector_index_ok(sectnum, engine_numsectors())) return false;
  const unsigned char* sec = g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE;
  const int wallptr = *(short*)(sec + SEC_WALLPTR);
  const int wallnum = *(short*)(sec + SEC_WALLNUM);
  if (!wall_range_ok(wallptr, wallnum, engine_numwalls())) return false;
  long long sx = 0, sy = 0;
  for (int w = 0; w < wallnum; w++) {
    const unsigned char* wl =
        g_base + rva::WALL + (unsigned)(wallptr + w) * rva::WALL_STRIDE;
    sx += *(int*)(wl + WAL_X);
    sy += *(int*)(wl + WAL_Y);
  }
  *cx = clamp_ll_to_int(sx / wallnum);
  *cy = clamp_ll_to_int(sy / wallnum);
  *cz = *(int*)(sec + SEC_FLOORZ);
  return true;
}

bool engine_is_badguy(int i) {
  if (!engine_verified() || !s_badguy || !sprite_index_ok(i)) return false;
  const void* spr = g_base + rva::SPRITE + (unsigned)i * rva::SPRITE_STRIDE;
  return s_badguy(spr) != 0;
}

CamState engine_camera() {
  CamState c = {};
  if (!engine_verified()) { c.valid = false; return c; }
  c.posx        = *(int*)  (g_base + rva::GLOBALPOSX);
  c.posy        = *(int*)  (g_base + rva::GLOBALPOSY);
  c.posz        = *(int*)  (g_base + rva::GLOBALPOSZ);
  c.ang         = *(short*)(g_base + rva::GLOBALANG) & 2047;
  c.horiz       = *(int*)  (g_base + rva::GLOBALHORIZ);
  c.halfxdimen  = *(int*)  (g_base + rva::HALFXDIMEN);
  c.xdimenscale = *(int*)  (g_base + rva::XDIMENSCALE);
  c.viewingrange = *(int*) (g_base + rva::VIEWINGRANGE);
  c.windowx1    = *(int*)  (g_base + rva::WINDOWX1);
  c.windowy1    = *(int*)  (g_base + rva::WINDOWY1);
  c.ydimen      = *(int*)  (g_base + rva::YDIMEN);
  // Both must be positive: project_world divides by radii built from them, zero before the engine's first setview call.
  c.valid       = c.halfxdimen > 0 && c.ydimen > 0;
  return c;
}

// Polymost's nine per-frame projection constants (GProj, engine.h), all doubles per the PDB: reading one as a float
// gives half a mantissa that looks plausible. Bounds live in proj_g_usable; `valid` only means the DLL matches the binary.
GProj engine_gproj() {
  GProj g = {};
  if (!engine_verified()) { g.valid = false; return g; }
  g.cosang  = *(double*)(g_base + rva::GCOSANG);
  g.sinang  = *(double*)(g_base + rva::GSINANG);
  g.cosang2 = *(double*)(g_base + rva::GCOSANG2);
  g.sinang2 = *(double*)(g_base + rva::GSINANG2);
  g.halfx   = *(double*)(g_base + rva::GHALFX);
  g.horiz   = *(double*)(g_base + rva::GHORIZ);
  g.shang   = *(double*)(g_base + rva::GSHANG);
  g.chang   = *(double*)(g_base + rva::GCHANG);
  g.yxscale = *(double*)(g_base + rva::GYXSCALE);
  g.valid   = true;
  return g;
}

int engine_xdim()               { return *(int*)(g_base + rva::XDIM); }
int engine_ydim()                { return *(int*)(g_base + rva::YDIM); }
int engine_rendmode()            { return *(int*)(g_base + rva::RENDMODE); }
int engine_viewingrange()        { return *(int*)(g_base + rva::VIEWINGRANGE); }
int engine_viewingrangerecip()   { return *(int*)(g_base + rva::VIEWINGRANGERECIP); }

// Starts true, latches false on the first jmp-thunk fingerprint failure.
static bool s_engineVerified = true;

void engine_mark_hook_result(bool installed) {
  if (!installed) s_engineVerified = false;
}

bool engine_verified() { return s_engineVerified; }

// One bounds-checked read of the local player; a bad myconnectindex or ps[].i leaves index/spriteIndex -1, valid false.
PlayerAim engine_player_aim() {
  PlayerAim p = {};
  p.index = -1;
  p.spriteIndex = -1;
  p.valid = false;
  if (!engine_verified()) return p;
  const int idx = *(int*)(g_base + rva::RVA_MYCONNECTINDEX);
  if (idx < 0 || idx > MAX_PLAYER_INDEX) return p;
  const unsigned char* ps = g_base + rva::RVA_PS + (unsigned)idx * rva::PS_STRIDE;
  p.index       = idx;
  p.spriteIndex = *(short*)(ps + rva::PS_I);
  if (p.spriteIndex < 0 || p.spriteIndex >= rva::MAX_SPRITES) p.spriteIndex = -1;
  p.eyex = *(int*)(ps + PS_POSX);
  p.eyey = *(int*)(ps + PS_POSY);
  p.eyez = *(int*)(ps + PS_POSZ);
  p.oposx = *(int*)(ps + rva::PS_OPOSX);
  p.oposy = *(int*)(ps + rva::PS_OPOSY);
  p.oposz = *(int*)(ps + rva::PS_OPOSZ);
  p.ang       = *(short*)(ps + rva::PS_OFFSET_ANG) & 2047;
  p.horiz     = *(int*)  (ps + rva::PS_OFFSET_HORIZ);
  p.horizoff  = *(short*)(ps + rva::PS_HORIZOFF);
  p.pyoff     = *(int*)  (ps + rva::PS_PYOFF);
  p.cursectnum = *(short*)(ps + rva::PS_CURSECTNUM);
  p.currWeapon = *(short*)(ps + rva::PS_CURR_WEAPON);
  p.autoAim    = *(int*)  (ps + rva::PS_AUTO_AIM);
  p.dead       = *(short*)(ps + rva::PS_DEAD_FLAG) != 0;
  // newowner >= 0 means the player looks through a camera sprite; only its sign is used.
  p.cameraActive = *(short*)(ps + rva::PS_NEWOWNER) >= 0;
  p.valid = (p.spriteIndex >= 0) && (p.cursectnum >= 0) && (p.cursectnum < engine_numsectors());
  return p;
}

typedef long (__cdecl* CanseeFn)(long, long, long, short, long, long, long, short);
typedef long (__cdecl* GetangleFn)(long, long);

// cansee, VA 0x0043EEA0: called directly (a real body, not a thunk), gated by engine_verified().
bool engine_cansee(int x1, int y1, int z1, int sect1, int x2, int y2, int z2, int sect2) {
  if (!engine_verified()) return false;
  const int ns = engine_numsectors();
  if (sect1 < 0 || sect1 >= ns || sect2 < 0 || sect2 >= ns) return false;
  const CanseeFn fn = (CanseeFn)(g_base + rva::CANSEE_BODY_RVA);
  return fn(x1, y1, z1, (short)sect1, x2, y2, z2, (short)sect2) != 0;
}

// getangle, VA 0x0043E550. The failure default 0 is a valid angle, so a forgotten engine_verified() check yields
// "facing east", not an out-of-range value.
int engine_getangle(int dx, int dy) {
  if (!engine_verified()) return 0;
  const GetangleFn fn = (GetangleFn)(g_base + rva::GETANGLE_BODY_RVA);
  return (int)fn(dx, dy) & 2047;
}

// Sprite i's position minus hittype[i]'s previous-tic position, bounds-checked against MAX_SPRITES.
bool engine_sprite_velocity(int i, int* vx, int* vy, int* vz) {
  if (!engine_verified() || i < 0 || i >= rva::MAX_SPRITES) return false;
  const unsigned char* h = g_base + rva::RVA_HITTYPE + (unsigned)i * rva::HITTYPE_STRIDE;
  return target_velocity_from(
      engine_sprite_field_l(i, SPR_X), engine_sprite_field_l(i, SPR_Y), engine_sprite_field_l(i, SPR_Z),
      *(int*)(h + rva::HITTYPE_BPOSX), *(int*)(h + rva::HITTYPE_BPOSY), *(int*)(h + rva::HITTYPE_BPOSZ),
      vx, vy, vz);
}

bool engine_sprite_floorz(int i, int* floorz) {
  if (!floorz) return false;
  if (!engine_verified() || i < 0 || i >= rva::MAX_SPRITES) return false;
  const unsigned char* h = g_base + rva::RVA_HITTYPE + (unsigned)i * rva::HITTYPE_STRIDE;
  *floorz = *(int*)(h + rva::HITTYPE_FLOORZ);
  return true;
}

// Death-state diagnostic read, logged not acted on (target.h). False means cannot tell.
bool engine_actor_dispicnum(int i, int* dispicnum) {
  if (!dispicnum) return false;
  if (!engine_verified() || i < 0 || i >= rva::MAX_SPRITES) return false;
  const unsigned char* h = g_base + rva::RVA_HITTYPE + (unsigned)i * rva::HITTYPE_STRIDE;
  // A short: dispicnum sits between actorstayput and timetosleep, so a four-byte read would pull timetosleep in.
  *dispicnum = *(short*)(h + rva::HITTYPE_DISPICNUM);
  return true;
}

// The slot bound is not optional: temp_data[] is the LAST member of the 72-byte struct, so a slot past its end reads the
// NEXT actor's cgg/picnum/ang, plausible memory that never looks like a fault (hittype_temp_data_slot_ok).
bool engine_actor_temp_data(int i, int slot, int* value) {
  if (!value) return false;
  if (!hittype_temp_data_slot_ok(slot)) return false;
  if (!engine_verified() || i < 0 || i >= rva::MAX_SPRITES) return false;
  const unsigned char* h = g_base + rva::RVA_HITTYPE + (unsigned)i * rva::HITTYPE_STRIDE;
  *value = *(int*)(h + hittype_temp_data_offset(slot));
  return true;
}

// A running total rather than a bool out-parameter, because engine_actor_action already returns false for the ordinary
// unreadable case; comparing before/after tells a caught fault apart.
static int s_actionFaultCount = 0;
int engine_actor_action_fault_count() { return s_actionFaultCount; }

// temp_data[4] -> action_read, which owns the VirtualQuery discipline. __try/__except wraps the call, not action_read,
// which stays Windows-free; ActorAction is POD, so a zeroed one out of __except is safe.
bool engine_actor_action(int i, ActorAction* out) {
  if (!out) return false;
  int t4 = 0;
  if (!engine_actor_temp_data(i, 4, &t4)) return false;
  __try {
    *out = action_read((unsigned)t4);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = ActorAction();
    s_actionFaultCount++;
    return false;
  }
  return true;
}

// Same guard at a raw address: the play-dead question's neighbour records are addresses this DLL computes.
bool engine_action_read_guarded(unsigned addr, ActorAction* out) {
  if (!out) return false;
  __try {
    *out = action_read(addr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *out = ActorAction();
    s_actionFaultCount++;
    return false;
  }
  return true;
}

// Undereferenced: picnum bounds-checked and read under SEH, no value guard, since the point is to log the raw pointer
// and see if it lands in the same heap region as t4.
int engine_actor_script_ptr(int picnum) {
  if (!engine_verified() || !tile_index_ok(picnum)) return 0;
  unsigned entry = 0;
  __try {
    entry = *(const unsigned*)(g_base + rva::ACTORSCRPTR + (unsigned)picnum * 4u);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
  return (int)entry;
}

// Writes ang/horiz on the TIC CLOCK ONLY, collapsing all three pairs (collapse_view_values): an open ohorizoff wobbles pitch
// during weapon kick. Zeroing return_to_center cancels a countdown armed by look keys and forceplayerangle (VA 0x004f2b28)
// that would fight the write; cost: an explosion's pitch kick does not ease back on its own.
static bool write_view_fields(ViewWriteMode mode, int ang, int horiz) {
  if (!engine_verified()) return false;
  unsigned fieldRvas[VF_COUNT];
  if (!player_view_field_rvas(fieldRvas)) return false;
  const long a = ang & 2047;
  const long horizoff = read_field_bytes(g_base, fieldRvas[VF_HORIZOFF], VIEW_FIELD_WIDTHS[VF_HORIZOFF]);
  for (int f = 0; f < VF_COUNT; f++) {
    long v = 0;
    if (!view_write_value(mode, f, a, horiz, horizoff, &v)) continue;
    write_field_bytes(g_base, fieldRvas[f], VIEW_FIELD_WIDTHS[f], v);
  }
  return true;
}

bool engine_write_player_view(int ang, int horiz) { return write_view_fields(VW_FREEZE_COLLAPSE, ang, horiz); }

// The aimbot's writer: a tracked view must not collapse the pairs, a held one must.
bool engine_write_player_aim(int ang, int horiz) { return write_view_fields(VW_AIM_TRACK, ang, horiz); }

// Each is a cdecl call to the real body by RVA behind engine_verified() and a sector bound from the live numsectors.
typedef void (__cdecl* UpdatesectorzFn)(long, long, long, short*);
typedef long (__cdecl* SlopezFn)(short, long, long);

// updatesectorz takes `sectnum` as a short* IN/OUT (-1 inside solid geometry, which the teleport refuses on). A LOCAL
// SHORT, not a cast of the caller's int, or the other two bytes would turn -1 into an out-of-bounds index next call.
bool engine_updatesectorz(int x, int y, int z, int* sectInOut) {
  if (!sectInOut) return false;
  if (!engine_verified()) return false;
  // -1 is Build's "no starting sector, full scan", needed by the camera's level-change recovery. The output is not
  // checked: -1 is a legitimate answer.
  if (*sectInOut != -1 && !sector_index_ok(*sectInOut, engine_numsectors())) return false;
  const UpdatesectorzFn fn = (UpdatesectorzFn)(g_base + rva::UPDATESECTORZ_BODY_RVA);
  short s = (short)*sectInOut;
  fn(x, y, z, &s);
  *sectInOut = s;
  return true;
}

// Two entry points; the suite pins their RVAs apart because swapping them would put the player through the ground.
bool engine_florz(int sect, int x, int y, int* out) {
  if (!out) return false;
  if (!engine_verified() || !sector_index_ok(sect, engine_numsectors())) return false;
  const SlopezFn fn = (SlopezFn)(g_base + rva::GETFLORZ_BODY_RVA);
  *out = (int)fn((short)sect, x, y);
  return true;
}

bool engine_ceilz(int sect, int x, int y, int* out) {
  if (!out) return false;
  if (!engine_verified() || !sector_index_ok(sect, engine_numsectors())) return false;
  const SlopezFn fn = (SlopezFn)(g_base + rva::GETCEILZ_BODY_RVA);
  *out = (int)fn((short)sect, x, y);
  return true;
}

// The teleport's ray cast. The starting sectnum must be readable; hit indices and point are NOT bounds-checked: hitsect
// < 0 (nothing hit) is the teleport's own refusal. The fail-closed seeding lives in hitscan_call (common.h).
bool engine_hitscan(int xs, int ys, int zs, int sectnum,
                    int vx, int vy, int vz,
                    int* hitsect, int* hitwall, int* hitsprite,
                    int* hitx, int* hity, int* hitz) {
  if (!engine_verified()) return false;
  if (!sector_index_ok(sectnum, engine_numsectors())) return false;
  const HitscanRawFn fn = (HitscanRawFn)(g_base + rva::HITSCAN_BODY_RVA);
  return hitscan_call(fn, (long)xs, (long)ys, (long)zs, (short)sectnum,
                      (long)vx, (long)vy, (long)vz,
                      (unsigned long)rva::HITSCAN_CLIPTYPE,
                      hitsect, hitwall, hitsprite, hitx, hity, hitz);
}

// The triggerbot's one write: OR the fire bit into this tic's 12-byte input record (rva::RVA_LOC); `bits` is its
// unsigned long at +8, fire is bit 4. Never cleared here: the player's own key may hold it.
bool engine_input_set_fire() {
  if (!engine_verified()) return false;
  unsigned char* loc = g_base + rva::RVA_LOC;
  *(unsigned long*)(loc + rva::LOC_BITS_OFFSET) |= rva::INPUT_BIT_FIRE;
  return true;
}

// Hold view: the aimbot's one write into the input record, a CLEAR. avel (+0) and horz (+1) are signed chars zeroed one
// byte each (wider would hit fvel at +2); the rest is untouched so holding view never stops walking or shooting.
bool engine_input_clear_turn() {
  if (!engine_verified()) return false;
  unsigned char* loc = g_base + rva::RVA_LOC;
  *(signed char*)(loc + rva::LOC_AVEL_OFFSET) = 0;
  *(signed char*)(loc + rva::LOC_HORZ_OFFSET) = 0;
  return true;
}

// ud.pause_on is 16 bits, read at the width the engine writes (`MOVSX ECX, word ptr [...]`).
bool engine_paused() {
  if (!engine_verified()) return false;
  return *(short*)(g_base + rva::PAUSE_ON) != 0;
}

// An int read would fold in the neighbouring global; range-checked so a pre-game read returns -1.
int engine_screenpeek() {
  if (!engine_verified()) return -1;
  const short v = *(short*)(g_base + rva::RVA_SCREENPEEK);
  if (!player_index_valid((long)v, MAX_PLAYER_INDEX)) return -1;
  return (int)v;
}

int engine_myconnectindex() {
  if (!engine_verified()) return -1;
  const long idx = *(long*)(g_base + rva::RVA_MYCONNECTINDEX);
  if (!player_index_valid(idx, MAX_PLAYER_INDEX)) return -1;
  return (int)idx;
}

// Position and previous-tic partner in one pass, so the camera and teleport see one consistent pair.
PlayerPos engine_player_pos() {
  PlayerPos p = {};
  p.cursectnum = -1;
  p.valid = false;
  if (!engine_verified()) return p;
  const int idx = *(int*)(g_base + rva::RVA_MYCONNECTINDEX);
  if (!player_index_valid((long)idx, MAX_PLAYER_INDEX)) return p;
  const unsigned char* ps = g_base + rva::RVA_PS + (unsigned)idx * rva::PS_STRIDE;
  p.posx  = *(int*)(ps + rva::PS_POSX);
  p.posy  = *(int*)(ps + rva::PS_POSY);
  p.posz  = *(int*)(ps + rva::PS_POSZ);
  p.oposx = *(int*)(ps + rva::PS_OPOSX);
  p.oposy = *(int*)(ps + rva::PS_OPOSY);
  p.oposz = *(int*)(ps + rva::PS_OPOSZ);
  p.cursectnum = *(short*)(ps + rva::PS_CURSECTNUM);
  p.valid = sector_index_ok(p.cursectnum, engine_numsectors());
  return p;
}

bool engine_teleport_player(const TeleportPlan& plan) {
  // A REFUSED PLAN IS THE FIRST TEST, above every engine read.
  if (!plan.ok) return false;
  if (!engine_verified()) return false;
  const int idx = *(int*)(g_base + rva::RVA_MYCONNECTINDEX);
  unsigned psRva = 0;
  // THE ONLY SANCTIONED SOURCE of psRva (common.h): a raw myconnectindex times PS_STRIDE is an eight-field write into
  // arbitrary memory once the index is garbage.
  if (!camera_ps_rva(idx, &psRva)) return false;
  // The destination sector passed the planner's `>= 0` but lands in a field the renderer indexes sector[] with every
  // frame, so it is bounded again against the LIVE numsectors.
  if (!sector_index_ok(plan.sect, engine_numsectors())) return false;

  // One loop over the pure table, deciding nothing: teleport_write_value is pinned field by field, offsets/widths are
  // shared with the render bracket. A loop since engine.cpp cannot link into test_host.
  for (int f = 0; f < CF_COUNT; f++) {
    long v = 0;
    if (!teleport_write_value(f, plan.x, plan.y, plan.z, plan.sect, &v)) continue;
    write_field_bytes(g_base, psRva + camera_field_offset(f), camera_field_width(f), v);
  }
  return true;
}

// The real page-protection call behind AccProtectFn, so PatchBracket's byte logic stays pure; keeps <windows.h> out of common.h.
static int acc_virtualprotect(void* addr, unsigned size, unsigned newProt, unsigned* oldProtOut) {
  DWORD old = 0;
  const BOOL ok = VirtualProtect(addr, (SIZE_T)size, (DWORD)newProt, &old);
  if (oldProtOut) *oldProtOut = (unsigned)old;
  return ok ? 1 : 0;
}

void engine_install_accuracy_protect() {
  acc_protect_fn() = acc_virtualprotect;
  LOGI("engine: accuracy patch protection hook installed");
}

// The real page query behind action_query_fn: the compiled CON script is in a heap buffer, so VirtualQuery is the only
// guard. Needs MEM_COMMIT, a readable non-guard protection and `bytes` left in the ONE region reported.
static int engine_action_query(const void* addr, unsigned bytes) {
  if (!addr || bytes == 0) return 0;
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
  if (mbi.State != MEM_COMMIT) return 0;
  if (mbi.Protect & PAGE_GUARD) return 0;
  const DWORD prot = mbi.Protect & 0xFFu;
  const bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE ||
                         prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
                         prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
  if (!readable) return 0;
  const unsigned char* base = (const unsigned char*)mbi.BaseAddress;
  const unsigned char* p = (const unsigned char*)addr;
  if (p < base) return 0;
  const size_t offset = (size_t)(p - base);
  if (offset >= mbi.RegionSize) return 0;
  const size_t left = mbi.RegionSize - offset;
  return left >= (size_t)bytes ? 1 : 0;
}

void engine_install_action_query() {
  action_query_fn() = engine_action_query;
  LOGI("engine: action query hook installed");
}

// Signed shorts, the engine's declared width; a wrapped-negative read must NOT be clamped back.
int engine_numscans() {
  if (!engine_verified()) return -1;
  return *(short*)(g_base + rva::NUMSCANS);
}
int engine_numbunches() {
  if (!engine_verified()) return -1;
  return *(short*)(g_base + rva::NUMBUNCHES);
}

int engine_sector_wallnum(int sectnum) {
  if (!engine_verified() || !sector_index_ok(sectnum, engine_numsectors())) return -1;
  return *(short*)(g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE + SEC_WALLNUM);
}

bool engine_in_prepare_mirror() {
  if (!engine_verified()) return false;
  return *(unsigned char*)(g_base + rva::INPREPAREMIRROR) != 0;
}

// wall_index_ok is bounded against both the live engine_numwalls() and rva::MAX_WALLS.
static bool wall_index_ok(int w) {
  return w >= 0 && w < engine_numwalls() && w < (int)rva::MAX_WALLS;
}

int engine_wall_field_s(int w, unsigned off) {
  if (!engine_verified() || !wall_index_ok(w) || off + 2u > rva::WALL_STRIDE) return 0;
  return *(short*)(g_base + rva::WALL + (unsigned)w * rva::WALL_STRIDE + off);
}

int engine_wall_field_l(int w, unsigned off) {
  if (!engine_verified() || !wall_index_ok(w) || off + 4u > rva::WALL_STRIDE) return 0;
  return *(int*)(g_base + rva::WALL + (unsigned)w * rva::WALL_STRIDE + off);
}

int engine_wall_field_b(int w, unsigned off) {
  if (!engine_verified() || !wall_index_ok(w) || off + 1u > rva::WALL_STRIDE) return 0;
  return *(unsigned char*)(g_base + rva::WALL + (unsigned)w * rva::WALL_STRIDE + off);
}

bool engine_sector_wall_range(int sectnum, int* wallptr, int* wallnum) {
  if (!wallptr || !wallnum) return false;
  if (!engine_verified() || !sector_index_ok(sectnum, engine_numsectors())) return false;
  const unsigned char* sec = g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE;
  const int wp = *(short*)(sec + SEC_WALLPTR);
  const int wn = *(short*)(sec + SEC_WALLNUM);
  if (!wall_range_ok(wp, wn, engine_numwalls())) return false;
  *wallptr = wp;
  *wallnum = wn;
  return true;
}

// One sectortype long field, bounds-checked; 0 for a distrusted index, which callers treat as data, so the bound
// protects them, not the value.
int engine_sector_field_l(int sectnum, unsigned off) {
  if (!engine_verified()) return 0;
  if (!sector_index_ok(sectnum, engine_numsectors())) return 0;
  if (off + 4u > rva::SECTOR_STRIDE) return 0;
  return *(int*)(g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE + off);
}

// The same one width down, sign-extended like engine_wall_field_s: an unsigned read would turn a corrupt negative into a
// large positive index.
int engine_sector_field_s(int sectnum, unsigned off) {
  if (!engine_verified()) return 0;
  if (!sector_index_ok(sectnum, engine_numsectors())) return 0;
  if (off + 2u > rva::SECTOR_STRIDE) return 0;
  return *(short*)(g_base + rva::SECTOR + (unsigned)sectnum * rva::SECTOR_STRIDE + off);
}

// Static buffers (16384*12 + 4096*16 bytes of BSS), not allocation: this runs on the tic clock and must not touch the
// heap. Called only when updatesectorz failed and the player asked (camera.cpp).
static NearSectWall   s_nsWalls[rva::MAX_WALLS];
static NearSectSector s_nsSectors[rva::MAX_SECTORS];

// Every index is bounded against nw/ns ONCE per staging pass rather than re-running engine_verified() per element.
int engine_nearest_sector(int x, int y, int z) {
  if (!engine_verified()) return -1;
  const int nw = engine_numwalls();
  const int ns = engine_numsectors();
  if (nw <= 0 || ns <= 0) return -1;
  for (int w = 0; w < nw; w++) {
    const unsigned char* wl = g_base + rva::WALL + (unsigned)w * rva::WALL_STRIDE;
    s_nsWalls[w].x      = *(int*)(wl + WAL_X);
    s_nsWalls[w].y      = *(int*)(wl + WAL_Y);
    s_nsWalls[w].point2 = *(short*)(wl + rva::WAL_POINT2);
  }
  for (int s = 0; s < ns; s++) {
    int wallptr = 0, wallnum = 0, ceilingz = 0, floorz = 0;
    if (sector_index_ok(s, ns)) {
      const unsigned char* sec = g_base + rva::SECTOR + (unsigned)s * rva::SECTOR_STRIDE;
      const int wp = *(short*)(sec + SEC_WALLPTR);
      const int wn = *(short*)(sec + SEC_WALLNUM);
      if (wall_range_ok(wp, wn, nw)) { wallptr = wp; wallnum = wn; }
      ceilingz = *(int*)(sec + SEC_CEILINGZ);
      floorz   = *(int*)(sec + SEC_FLOORZ);
    }
    s_nsSectors[s].wallptr  = wallptr;
    s_nsSectors[s].wallnum  = wallnum;
    s_nsSectors[s].ceilingz = ceilingz;
    s_nsSectors[s].floorz   = floorz;
  }
  return nearest_sector(x, y, z, s_nsWalls, s_nsSectors, nw, ns);
}

// Got_access bits (rva::PS_GOT_ACCESS). 0 for an untrusted read means "holds nothing", so switch_classify shows a
// needed-card line rather than hiding one.
int engine_player_got_access() {
  if (!engine_verified()) return 0;
  const int idx = *(int*)(g_base + rva::RVA_MYCONNECTINDEX);
  if (!player_index_valid((long)idx, MAX_PLAYER_INDEX)) return 0;
  const unsigned rva = player_field_rva(rva::RVA_PS, idx, rva::PS_STRIDE, rva::PS_GOT_ACCESS);
  return *(short*)(g_base + rva);
}
