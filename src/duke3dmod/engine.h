#pragma once
#include "common.h"

// spritetype field offsets, from duke3d.pdb. Widths differ: x/y/z long; xrepeat/yrepeat/pal uchar; xoffset/yoffset
// schar; the rest short. A wrong width reads the neighbouring field.
const unsigned SPR_X       = 0x00;
const unsigned SPR_Y       = 0x04;
const unsigned SPR_Z       = 0x08;
const unsigned SPR_CSTAT   = 0x0C;
const unsigned SPR_PICNUM  = 0x0E;
// pal is not cosmetic: CON keys two health overrides off it (pal 21 Assault Trooper doubles to TROOPSTRENGTH; pal != 0
// BOSS1 is SET to BOSS1PALSTRENGTH), so max health is a function of (picnum, pal).
const unsigned SPR_PAL     = 0x11;
const unsigned SPR_XREPEAT = 0x14;
const unsigned SPR_YREPEAT = 0x15;
const unsigned SPR_SECTNUM = 0x18;
const unsigned SPR_STATNUM = 0x1A;
// owner: on a sector effector 7 sprite (statnum 9) it is the paired effector's index; owner == self means unpaired.
const unsigned SPR_OWNER   = 0x1E;
const unsigned SPR_LOTAG   = 0x26;
const unsigned SPR_HITAG   = 0x28;
const unsigned SPR_EXTRA   = 0x2A;
const unsigned SPR_ANG     = 0x1C;
// The caller sign-extends xoffset/yoffset: engine_sprite_field_u8 is unsigned.
const unsigned SPR_XOFFSET = 0x16;
const unsigned SPR_YOFFSET = 0x17;

// STAT_ACTOR holds sprites that noticed the player, STAT_ZOMBIEACTOR those still asleep (movefta() promotes 2 to 1).
// Walks of these must never nest or interleave: the cycle counter is per-walk.
const int STAT_ACTOR       = 1;
const int STAT_ZOMBIEACTOR = 2;

// cstat bit 15, Build's "invisible" flag. With ud.respawn_items on, a taken pickup is hidden by this bit (GAME.CON
// spawns RESPAWNMARKERRED cstat 32768), not deleted, so an item ESP must check it.
const int SPR_CSTAT_INVISIBLE = 0x8000;

// sectortype field offsets, from duke3d.pdb (40 bytes, rva::SECTOR_STRIDE): ceilingz/floorz long, the rest short.
const unsigned SEC_WALLPTR  = 0x00;
const unsigned SEC_WALLNUM  = 0x02;
const unsigned SEC_CEILINGZ = 0x04;
const unsigned SEC_FLOORZ   = 0x08;
const unsigned SEC_LOTAG    = 0x22;
// A wall sprite's top clamps to the ceiling and its bottom to the floor only when that surface's parallax bit is clear
// (docs/re/esp-sprites.md C).
const unsigned SEC_CEILINGSTAT = 0x0C;
const unsigned SEC_FLOORSTAT   = 0x0E;
// Low bit of either stat: the surface is parallaxed (sky or void).
const int SEC_STAT_PARALLAX = 1;

// walltype field offsets (32 bytes, rva::WALL_STRIDE); only the two vertex longs are read.
const unsigned WAL_X = 0x00;
const unsigned WAL_Y = 0x04;

// player_struct position fields engine_player_aim reads directly (stride 940, rva::PS_STRIDE); aliases of common.h's
// offsets so there is no second copy to drift.
const unsigned PS_POSX = rva::PS_POSX;
const unsigned PS_POSY = rva::PS_POSY;
const unsigned PS_POSZ = rva::PS_POSZ;  // the eye

// Build marks a secret area with this sector lotag (checksectors, VA 0x00514180). No ordinary sector carries 32767, and
// the engine writes 0 over a found secret, so 0 would mark nearly the whole map.
const int SECRET_SECTOR_LOTAG = 32767;

struct CamState {
  int posx, posy, posz;
  int ang;
  // globalhoriz, not Build's 0..200 `horiz`: already viewport pixels (drawrooms VA 0x00429689-0x004296a9).
  int horiz;
  int halfxdimen, xdimenscale, windowx1, windowy1;
  // The 3D viewport height (rva::YDIMEN), not engine_ydim() (the full screen, which differs when the status bar shrinks
  // the view). Polymost's projection origin is ydimen >> 1 (last write VA 0x0047a638).
  int ydimen;
  // 16.16 fixed point, 65536 at the neutral 90-degree FOV (fov.h).
  int viewingrange;
  bool valid;
};

// Polymost's per-frame projection constants (polymost_drawrooms, 0x0047a2d0), read from the engine's globals: the
// recomputed projection was proven wrong at every FOV (project.cpp). Each is the double g<name>; `valid` is false when
// the fingerprint failed.
struct GProj {
  double cosang;
  double sinang;
  double cosang2;
  double sinang2;
  double halfx;
  double horiz;
  double shang;
  double chang;
  double yxscale;
  bool valid;
};

struct PlayerAim {
  int  index;                // myconnectindex, or -1 when invalid
  int  spriteIndex;          // ps[].i, or -1
  int  eyex, eyey, eyez;
  // Previous tic's position (processinput, VA 0x004fb020-0x004fb03e); pos - opos is the latency the aimbot's solve pays.
  int  oposx, oposy, oposz;
  int  ang, horiz, horizoff;
  int  pyoff;
  int  cursectnum;
  int  currWeapon;
  int  autoAim;
  bool dead;
  // newowner >= 0: looking through a viewscreen/camera sprite, the one state the engine does not interpolate.
  bool cameraActive;
  bool valid;
};

int  engine_xdim();
int  engine_ydim();
int  engine_rendmode();

int  engine_viewingrange();

int  engine_viewingrangerecip();

// Fingerprint gate: the twelve jmp-thunk hooks each verify their thunk and call this once in mod_deferred_init. It
// latches false on the first failure, making every hardcoded RVA suspect, and gates blind engine-memory access.
void engine_mark_hook_result(bool installed);
bool engine_verified();

CamState engine_camera();

GProj engine_gproj();
int  engine_sprite_field_s(int i, unsigned off);   // sign-extended
int  engine_sprite_field_u8(int i, unsigned off);
int  engine_sprite_field_l(int i, unsigned off);
int  engine_tilesizx(int picnum);
int  engine_tilesizy(int picnum);
// An actor's `strength` from the compiled CON script (its maximum health), or 0 when unreadable. The one read that
// follows a pointer out of an engine array: picnum bounded, pointer checked (actor_script_ptr_ok), SEH-guarded.
int  engine_actor_strength(int picnum);
int  engine_first_sprite_of_stat(int stat);
int  engine_next_sprite(int i);
int  engine_sector_lotag(int sectnum);
int  engine_numsectors();
bool engine_is_badguy(int spriteIndex);
void engine_install_badguy();

int  engine_numwalls();

bool engine_sector_is_secret(int sectnum);

// The loaded expansion (rva::ADDON): 0 base, 1 D.C., 2 Nuclear Winter, 3 Caribbean; 0 when unverified.
int engine_addon_index();

// Whether the local player is in a level being played: the game mode byte reads exactly 4. False when unverified,
// keeping overlays off.
bool engine_player_in_play();

// Sector centre: the wall-vertex average for x/y and the floor z. Inside any convex sector (nearly every secret) but not
// guaranteed for a concave one; point-in-polygon is not worth it for a marker.
bool engine_sector_centre(int sectnum, int* cx, int* cy, int* cz);

PlayerAim engine_player_aim();

// The engine's own line-of-sight test (cansee, VA 0x0043EEA0); false if unverified or a sector index is out of range.
// Safe on the tic or render clock (docs/re/targeting.md A).
bool engine_cansee(int x1, int y1, int z1, int sect1, int x2, int y2, int z2, int sect2);

int  engine_getangle(int dx, int dy);

// Per-tic velocity of sprite i: its position minus hittype[i]'s previous-tic position, read from the engine's actor
// mover. False (nothing touched) if unverified or out of range.
bool engine_sprite_velocity(int i, int* vx, int* vy, int* vz);

// The floor the actor mover measures sprite i against (hittype[i].floorz, written by makeitfall, VA 0x004dd120); the
// sector floor differs on slopes and platforms. False means "cannot tell", never "grounded".
bool engine_sprite_floorz(int i, int* floorz);

// Death-state diagnostic reads of hittype[] (PDB `weaponhit`), logged not acted on. False means "cannot tell".
bool engine_actor_dispicnum(int i, int* dispicnum);
bool engine_actor_temp_data(int i, int slot, int* value);

// Hands temp_data[4] to action_read (common.h), which owns the VirtualQuery discipline; its loads run under this
// function's __try/__except. On a fault it writes a zeroed ActorAction, returns false and counts it.
bool engine_actor_action(int i, ActorAction* out);
int  engine_actor_action_fault_count();
// action_read at a raw address under the same SEH frame and counter. The play-dead question (actorstate.h) needs the
// records twenty bytes either side of temp_data[4]: the only raw CON-heap dereference.
bool engine_action_read_guarded(unsigned addr, ActorAction* out);
// actorscrptr[picnum], undereferenced: bounds-checked and SEH-read, but not actor_script_ptr_ok-checked (the heap buffer
// has no static array to contain it). 0 means "no entry could be read".
int  engine_actor_script_ptr(int picnum);

// Writes the local player's ang/horiz on the tic clock only (inside the processinput hook). Collapses all three
// interpolated pairs and zeroes angvel/return_to_center; a stale ohorizoff wobbles pitch on weapon kick.
bool engine_write_player_view(int ang, int horiz);

// The aimbot's writer: it rewrites every tic, and the engine copies oang <- ang at the top of processinput (VA
// 0x004fb057/0x004fb633), so collapsing the interpolated pairs would draw a step per tic.
bool engine_write_player_aim(int ang, int horiz);

// Build's "which sector is this point in", disambiguating stacked sectors by z. *sectInOut is guess in, answer out; -1
// (solid space) passes through, the camera's only signal that it is inside geometry.
bool engine_updatesectorz(int x, int y, int z, int* sectInOut);

// getflorzofslope / getceilzofslope: the z of a sloped surface at (x, y), two entry points 352 bytes apart. False if
// unverified, null out-parameter, or sector outside the live array.
bool engine_florz(int sect, int x, int y, int* out);
bool engine_ceilz(int sect, int x, int y, int* out);

// The engine's ray cast, by RVA (a real body, not a thunk); formula in docs/re/freecam-accuracy.md F. *hitsect < 0
// (nothing hit) is the teleport's refusal signal.
bool engine_hitscan(int xs, int ys, int zs, int sectnum,
                    int vx, int vy, int vz,
                    int* hitsect, int* hitwall, int* hitsprite,
                    int* hitx, int* hity, int* hitz);

// The triggerbot's one write: ORs the fire bit into this tic's input record. Only from hooked_getinput after the
// original; never clears the bit, as the player's key may hold it.
bool engine_input_set_fire();

// Zeroes the record's turn half (avel, horz). The engine adds mouse turn at the top of processinput, between the
// aimbot's solve and `shoot`; getinput is the only early enough point.
bool engine_input_clear_turn();

// True while Duke's pause is active; read from hooked_getinput, the one hook that still runs every frame paused.
bool engine_paused();

int engine_screenpeek();

// The local player's own index; differs from engine_screenpeek() in a demo or coop spectate. -1 if untrusted.
int engine_myconnectindex();

struct PlayerPos {
  int posx, posy, posz;
  int oposx, oposy, oposz;
  int cursectnum;
  bool valid;
};
PlayerPos engine_player_pos();

// Moves the local player to freecam_teleport_plan's destination. Writes both members of each pair, since Build
// interpolates from the o member, plus cursectnum and poszv = 0. Tic clock only, from camera_tic.
struct TeleportPlan;
bool engine_teleport_player(const TeleportPlan& plan);

// Installs the real VirtualProtect behind common.h's acc_protect_fn; the accuracy PatchBracket refuses to write
// without it, and acc_protect_fn() fails closed, so losing the call silently turns the feature off.
void engine_install_accuracy_protect();

// Installs the real VirtualQuery behind common.h's action_query_fn; without it every action read is unreadable.
void engine_install_action_query();

// numscans/numbunches: counters polymost_scansector appends to with no bound. Signed shorts: wrapped-negative means
// the budget is gone. -1 if unverified.
int engine_numscans();
int engine_numbunches();

int engine_sector_wallnum(int sectnum);

// ud's mirror-in-progress flag: true while displayrooms renders the mirror pass, where polymost_drawrooms runs twice.
bool engine_in_prepare_mirror();

// Bounds-checked walltype field readers: 0 for an out-of-range wall, unverified engine or overrun, never a promise about
// the offset.
int  engine_wall_field_s(int w, unsigned off);   // sign-extended
int  engine_wall_field_l(int w, unsigned off);
int  engine_wall_field_b(int w, unsigned off);   // unsigned byte

bool engine_sector_wall_range(int sectnum, int* wallptr, int* wallnum);

int engine_sector_field_l(int sectnum, unsigned off);

// The short twin, for SEC_WALLPTR/SEC_WALLNUM. Not engine_sector_wall_range: the resolver wants the raw pair so its own
// refusal is what the suite pins. An unreadable pair comes back 0/0.
int engine_sector_field_s(int sectnum, unsigned off);

int engine_nearest_sector(int x, int y, int z);

// The local player's got_access bits (docs/re/switches-breakables.md G): 1 blue, 2 red, 4 yellow. 0 for an untrusted
// read, which switch_classify (esp_switch.h) treats as "card not held".
int engine_player_got_access();
