#include "esp_names.h"
#include "common.h"

// Enemy picnums: badguy()'s switch (VA 0x004b9c50) plus the actortype[]-set CANNON, TANK and NEWBEAST (GAME.CON `useractor
// enemy`). Excludes OCTABRAINSTAYPUT 1821, LIZMANSTAYPUT 2121 (plain `actor`, GAME.CON:5613/5133), MYENEMY 3590 (never
// compiled). Nuclear Winter adds ELFUZI 3800/3801 (50), ELFGUN 4000/4001 (75), SNOWMAN 3841 (75); pig cop is 2002/2003/2047.
struct NameEntry { int picnum; const char* name; };

static const NameEntry NAMES[] = {
  {  675, "Egg" },                  // the Alien Queen's
  { 1267, "Rat" },
  { 1550, "Shark" },
  { 1680, "Assault Trooper" },      // LIZTROOP
  { 1681, "Assault Trooper" },
  { 1682, "Assault Trooper" },
  { 1715, "Assault Trooper" },
  { 1725, "Assault Trooper" },
  { 1741, "Assault Trooper" },
  { 1742, "Assault Trooper" },
  { 1744, "Assault Trooper" },
  { 1810, "Cannon" },               // via actortype
  { 1820, "Octabrain" },
  { 1880, "Sentry Drone" },
  { 1920, "Assault Commander" },
  { 1921, "Assault Commander" },
  { 1960, "Recon Patrol" },
  { 1975, "Pig Cop Tank" },         // via actortype
  { 2000, "Pig Cop" },
  { 2001, "Pig Cop" },
  { 2045, "Pig Cop" },
  { 2002, "Pig Cop" },              // Nuclear Winter's own picnums
  { 2003, "Pig Cop" },
  { 2047, "Pig Cop" },
  { 2120, "Enforcer" },
  { 2150, "Enforcer" },
  { 2160, "Enforcer" },
  { 2165, "Enforcer" },
  { 2360, "Turret" },
  { 2370, "Protozoid Slimer" },     // GREENSLIME's eight states, one range in badguy()
  { 2371, "Protozoid Slimer" },
  { 2372, "Protozoid Slimer" },
  { 2373, "Protozoid Slimer" },
  { 2374, "Protozoid Slimer" },
  { 2375, "Protozoid Slimer" },
  { 2376, "Protozoid Slimer" },
  { 2377, "Protozoid Slimer" },
  { 2420, "Organic turret" },       // ORGANTIC: a turret per its CON strength
  { 2630, "Battlelord" },
  { 2710, "Overlord" },
  { 2760, "Cycloid Emperor" },
  { 3800, "Elf" },                  // Nuclear Winter
  { 3801, "Elf" },
  { 3841, "Snowman" },
  { 4000, "Elf gunner" },
  { 4001, "Elf gunner" },
  { 4610, "Protector Drone" },      // NEWBEAST, via actortype
  { 4611, "Protector Drone" },
  { 4670, "Protector Drone" },
  { 4671, "Protector Drone" },
  { 4690, "Protector Drone" },
  { 4740, "Alien Queen" },
};

static const int NAME_COUNT = (int)(sizeof(NAMES) / sizeof(NAMES[0]));

// Linear scan: under fifty entries, one lookup per visible enemy per frame.
const char* esp_enemy_name(int picnum) {
  for (int i = 0; i < NAME_COUNT; i++)
    if (NAMES[i].picnum == picnum) return NAMES[i].name;
  return nullptr;
}

// Expansion pictures on base-game picnums. Nuclear Winter: SOBBOTWALK 2000 and SOBBOTFLY 2045 stand where pig cops do
// (plain `actor`, so actortype[] misses them, but badguy() accepts both). Duke Caribbean: DRAGON 2120, SEAGULL 2150,
// BEACHBABE1 2165 stand where Enforcers do; SEAGULLSHIT 1638 is its own enemy.
struct AddonName { int addon, picnum; const char* name; };
static const AddonName ADDON_NAMES[] = {
  { ESP_ADDON_NW,        2000, "Bot" },
  { ESP_ADDON_NW,        2045, "Bot" },
  { ESP_ADDON_CARIBBEAN, 1638, "Seagull dropping" },
  { ESP_ADDON_CARIBBEAN, 2120, "Dragon" },
  { ESP_ADDON_CARIBBEAN, 2150, "Seagull" },
  { ESP_ADDON_CARIBBEAN, 2165, "Beach babe" },
};
static const int ADDON_NAME_COUNT = (int)(sizeof(ADDON_NAMES) / sizeof(ADDON_NAMES[0]));

const char* esp_enemy_name_for(int picnum, int addon) {
  for (int i = 0; i < ADDON_NAME_COUNT; i++)
    if (ADDON_NAMES[i].addon == addon && ADDON_NAMES[i].picnum == picnum)
      return ADDON_NAMES[i].name;
  return esp_enemy_name(picnum);
}

int esp_addon_name_count() { return ADDON_NAME_COUNT; }
int esp_addon_name_addon(int i) { return (i >= 0 && i < ADDON_NAME_COUNT) ? ADDON_NAMES[i].addon : -1; }
int esp_addon_name_picnum(int i) { return (i >= 0 && i < ADDON_NAME_COUNT) ? ADDON_NAMES[i].picnum : -1; }

int esp_enemy_name_count() { return NAME_COUNT; }

int esp_enemy_name_picnum(int i) {
  if (i < 0 || i >= NAME_COUNT) return -1;
  return NAMES[i].picnum;
}

// Pickup picnums: spawn() VA 0x004c1e00's PICKUP cases (0x15-0x3d, 100, into 0x3c ACCESSCARD; HOLODUKE 0x544 joins).
// Names follow NAMES.H and the GAME.CON pickup quote ("GOT SHRINKER!" -> "Shrinker"). Omitted: HEAVYHBOMB (26) is also
// every live thrown pipebomb (docs/re/esp-sprites.md); HBOMBAMMO (47) is the unambiguous pickup.
struct ItemEntry { int picnum; const char* name; };

static const ItemEntry ITEMS[] = {
  {   21, "Pistol" },
  {   22, "Chaingun cannon" },
  {   23, "RPG" },
  {   24, "Freezethrower" },
  {   25, "Shrinker" },
  {   27, "Laser tripbomb" },
  {   28, "Shotgun" },
  {   29, "Devastator" },
  {   37, "Freezethrower ammo" },
  {   40, "Pistol ammo" },
  {   41, "Chaingun ammo" },
  {   42, "Devastator ammo" },
  {   44, "RPG ammo" },
  {   45, "Expander ammo" },
  {   46, "Shrinker ammo" },
  {   47, "Pipebombs" },
  {   48, "Pistol ammo" },           // AMMOLOTS, the big box
  {   49, "Shotgun ammo" },
  {   51, "Small medkit" },
  {   52, "Large medkit" },
  {   53, "Portable medkit" },
  {   54, "Armour" },
  {   55, "Steroids" },
  {   56, "Scuba gear" },
  {   57, "Jetpack" },
  {   59, "Night vision goggles" },
  {   60, "Keycard" },               // ACCESSCARD; coloured below
  {   61, "Boots" },
  {  100, "Atomic health" },
  { 1348, "Holoduke" },
};

static const int ITEM_COUNT = (int)(sizeof(ITEMS) / sizeof(ITEMS[0]));

const char* esp_item_name(int picnum) {
  for (int i = 0; i < ITEM_COUNT; i++)
    if (ITEMS[i].picnum == picnum) return ITEMS[i].name;
  return nullptr;
}

int esp_item_name_count() { return ITEM_COUNT; }

int esp_item_name_picnum(int i) {
  if (i < 0 || i >= ITEM_COUNT) return -1;
  return ITEMS[i].picnum;
}

// Which lock a card opens is its pal: checkhitswitch (VA 0x0050ec7c-0x0050ed6d) compares against 0, 21, 23. Other pals
// get the bare name; no other pickup reaches the ESP with a non-zero pal (spawn() assigns pal=0).
const char* esp_item_name_pal(int picnum, int pal) {
  const char* nm = esp_item_name(picnum);
  if (nm == nullptr || picnum != ESP_PIC_ACCESSCARD) return nm;
  if (pal == ESP_PAL_KEY_BLUE)   return "Blue keycard";
  if (pal == ESP_PAL_KEY_RED)    return "Red keycard";
  if (pal == ESP_PAL_KEY_YELLOW) return "Yellow keycard";
  return nm;
}

// Max health is a function of (picnum, pal): pal 21 Assault Troopers have 60 HP (GAME.CON:4721-4781 TROOPSTRENGTH 30), so
// base + 30; a coloured BOSS1 ASSIGNS 1000 (unreadable here): no maximum; BOSS2/3/4 pals change AI only. Residual: a
// respawned pal-21 trooper resets to 30 (GAME.CON:4598-4605); single-player leaves respawn off.

bool esp_is_trooper(int picnum) {
  // The eight picnums whose actors invoke `state checktrooppalette` (GAME.CON:4721-4781); 1683..1714 are animation frames.
  return picnum == ESP_PIC_LIZTROOP
      || picnum == 1681 || picnum == 1682 || picnum == 1715
      || picnum == 1725 || picnum == 1741 || picnum == 1742 || picnum == 1744;
}

int esp_rule_max_health(int picnum, int pal, int base, int troopBase) {
  // Ceiling is what sprite.extra can hold (spawn() casts CON strength into that short, VA 0x004c23e2).
  if (base <= 0 || base > ACTOR_STRENGTH_MAX) return 0;

  // Coloured Battlelord: the operand has no readable address, so the stock value is used only when the spawn strength
  // proves a stock CON.
  if (picnum == ESP_PIC_BOSS1 && pal != 0) return (base == ESP_BOSS1_STRENGTH) ? ESP_BOSS1_PAL_STRENGTH : 0;

  if (pal == ESP_TROOP_BOOST_PAL && esp_is_trooper(picnum)) {
    if (troopBase <= 0 || troopBase > ACTOR_STRENGTH_MAX) return 0;
    // Widened: the sum of two bounded operands is not bounded.
    const long long boosted = (long long)base + (long long)troopBase;
    if (boosted > ACTOR_STRENGTH_MAX) return 0;
    return (int)boosted;
  }

  return base;
}

// Last-resort learned ceiling, only when esp_rule_max_health has no answer. Flat table, linear scan: a level holds far
// fewer than ESP_MAX_HEALTH_TYPES enemy tiles.
static int s_pic[ESP_MAX_HEALTH_TYPES];
static int s_pal[ESP_MAX_HEALTH_TYPES];
static int s_max[ESP_MAX_HEALTH_TYPES];
static int s_count = 0;
static int s_overflow = 0;

int esp_track_max_health(int picnum, int pal, int current) {
  // Non-positive health records nothing (divide-by-zero, drawing past full).
  if (current <= 0) return 0;

  for (int i = 0; i < s_count; i++) {
    if (s_pic[i] == picnum && s_pal[i] == pal) {
      if (current > s_max[i]) s_max[i] = current;
      return s_max[i];
    }
  }

  if (s_count < ESP_MAX_HEALTH_TYPES) {
    s_pic[s_count] = picnum;
    s_pal[s_count] = pal;
    s_max[s_count] = current;
    s_count++;
    return current;
  }

  // Past capacity: no maximum (a permanently full bar was the old wrong answer); count the refusal.
  if (s_overflow < 2147483647) s_overflow++;
  return 0;
}

int esp_max_health(int picnum, int pal, int current, int base, int troopBase) {
  // `< 0`, not `<= 0`: Duke keeps actors shootable at zero health (empty bar); negative is garbage.
  if (current < 0) return 0;
  const int ruled = esp_rule_max_health(picnum, pal, base, troopBase);
  if (ruled > 0) {
    // Never below current, so canvas_bar_fill never draws past full.
    return ruled > current ? ruled : current;
  }
  return esp_track_max_health(picnum, pal, current);
}

int esp_max_health_overflow() { return s_overflow; }

void esp_max_health_reset() {
  s_count = 0;
  s_overflow = 0;
}

// CRACK1..CRACK4 = 546..549 (0x222..0x225; docs/re/switches-breakables.md B): wall-aligned statnum-6 sprites that
// movestandables reacts to only with a nonzero hitag (esp_crack_visible, esp_world.h). One name for all four.
static const int EXPLODABLE_PICS[] = { 546, 547, 548, 549 };

const char* esp_explodable_name(int picnum) {
  for (int i = 0; i < (int)(sizeof(EXPLODABLE_PICS) / sizeof(EXPLODABLE_PICS[0])); i++)
    if (EXPLODABLE_PICS[i] == picnum) return "Explodable";
  return nullptr;
}
int esp_explodable_name_count() {
  return (int)(sizeof(EXPLODABLE_PICS) / sizeof(EXPLODABLE_PICS[0]));
}
int esp_explodable_name_picnum(int i) {
  return (i >= 0 && i < esp_explodable_name_count()) ? EXPLODABLE_PICS[i] : -1;
}

// Destructible panels (docs/re/switches-breakables.md C, checkhitwall VA 0x0050fcd0's breakwall cases). Broken, the
// engine rewrites the picnum (360..363, 4144..4147, 4203 or +1/+2), so being in this set means "not yet broken".
static const int BREAKABLE_PICS[] = {
  293, 297, 299, 301, 305, 306, 307,
  1212, 1215,
  4130, 4131, 4132, 4133, 4134, 4136, 4138, 4140, 4142, 4181
};

const char* esp_breakable_name(int picnum) {
  for (int i = 0; i < (int)(sizeof(BREAKABLE_PICS) / sizeof(BREAKABLE_PICS[0])); i++)
    if (BREAKABLE_PICS[i] == picnum) return "Breakable";
  return nullptr;
}
int esp_breakable_name_count() {
  return (int)(sizeof(BREAKABLE_PICS) / sizeof(BREAKABLE_PICS[0]));
}
int esp_breakable_name_picnum(int i) {
  return (i >= 0 && i < esp_breakable_name_count()) ? BREAKABLE_PICS[i] : -1;
}
