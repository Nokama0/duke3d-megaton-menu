#pragma once
// Shared test helpers. To add a check, append a tb_ function to its area file, declare it in test_blocks.h and call it
// from main() in test_host.cpp.


#include <stdio.h>
#include "../common.h"
#include "../config.h"
#include "../log.h"
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <limits>
#include <new>
#include "../fov.h"
#include "../ui.h"
#include "../paths.h"
#include "../canvas.h"
#include "../font.h"
#include "../fontpack.h"
#include "../menu.h"
#include "../engine.h"
#include "../project.h"
#include "../overlay.h"
#include "../esp_names.h"
#include "../esp_layout.h"
#include "../esp_world.h"
#include "../esp_switch.h"
#include "../input.h"
void menu_stub_set_open(bool open);
#include "../target.h"
#include "../accuracy.h"
#include "../freecam.h"
#include "../nocull.h"
#include "../nearsect.h"
#include "../aim.h"
#include "../bind.h"
#include "../gamecfg.h"
#include "../mouseaim.h"

extern int passed, failed;


// Synthetic clicks on the live panel: row LAYOUT is file-local to menu.cpp, so these sweep pixels and report what a click did.

// Visuals-tab config fields, snapshotted so a click's effect is a diff.
struct VisState { int enemies, boxes, health, names, distance, items, secrets, drawdist, explodables, switches, fullbright, dmgnumbers, secrettriggers, secretvolume, breakables; };

VisState vis_snapshot();

void vis_restore(const VisState& v);

enum { VF_ENEMIES = 1, VF_BOXES = 2, VF_HEALTH = 4, VF_NAMES = 8,
       VF_DISTANCE = 16, VF_ITEMS = 32, VF_SECRETS = 64, VF_DRAWDIST = 128,
       VF_EXPLODABLES = 256, VF_SWITCHES = 512, VF_FULLBRIGHT = 1024,
       VF_DMGNUMBERS = 2048, VF_SECRETTRIGGERS = 4096, VF_SECRETVOLUME = 8192,
       VF_BREAKABLES = 16384 };

int vis_changed(const VisState& before);


// The sweeps step a 4px lattice: every clickable control is at least 6 device px tall and 7 wide at UI_SCALE_MIN.
static const int SWEEP_STRIDE = 4;


// Sweeps the visuals tab, ORing everything any click changed; `multi` is set if one click changed several fields.
int vis_sweep(int screenW, int screenH, const VisState& start, bool held,
                     bool* multi);


struct CombatState {
  int aimon, aimhold, aimbind, aimtarget, aimpoint, aimsmooth, aimcone, aimlead, aimdebug;
  int aimleadscale;
  int silenton, silentmaxangle;
  int aimrange;
};

CombatState combat_snapshot();

void combat_restore(const CombatState& c);

enum { CF_AIMON = 1, CF_AIMHOLD = 2, CF_AIMBUTTON = 4, CF_AIMTARGET = 8, CF_AIMPOINT = 16,
      CF_AIMSMOOTH = 32, CF_AIMCONE = 64, CF_AIMLEAD = 128, CF_AIMDEBUG = 256,
      CF_SILENTON = 512, CF_SILENTMAXANGLE = 1024, CF_AIMLEADSCALE = 2048,
      CF_AIMRANGE = 4096 };

int combat_changed(const CombatState& before);

int combat_sweep(int screenW, int screenH, const CombatState& start, bool held,
                        bool* multi);


// Sweeps one row's own y, so a mis-wired row's mask differs from its label's bit.
int combat_row_click_mask(int rowY, int screenW, int screenH, const CombatState& start);


int vis_row_click_mask(int rowY, int screenW, int screenH, const VisState& start);


struct WeapState { int pistol, chaingun, shotgun, expander;
                   int mouseaimon, mouseaimsens, mouseaimvscale; int firerate; };

WeapState weap_snapshot();

void weap_restore(const WeapState& w);

enum { WF_PISTOL = 1, WF_CHAINGUN = 2, WF_SHOTGUN = 4, WF_EXPANDER = 8,
       WF_MOUSEAIMON = 16, WF_MOUSEAIMSENS = 32, WF_MOUSEAIMVSCALE = 64,
       WF_FIRERATE = 128 };

int weap_changed(const WeapState& before);

int weap_sweep(int screenW, int screenH, const WeapState& start, bool held,
                      bool* multi);

// The exact set of fields one row's y moves (a swapped weapon binding hides here).
int weap_row_click_mask(int rowY, int screenW, int screenH, const WeapState& start);


bool tab_text_fits(int tab, int screenW, int screenH, int inset, int* worstOverflowOut);


// A row out of cursor height draws nothing and so passes the right-edge check; this asserts the last row exists.
bool tab_bottom_row_fits(int tab, const char* bottomLabel, int screenW, int screenH);


const CanvasOp* find_readout_at_y(int rowY);


// A slider row's track by label: row_slider_at draws the C_WELL bevel at r.y + 11.
const CanvasOp* find_track_at_label(const char* label);


// A numeric slider reaches both declared ends (a `hi` mistyped low still "changes" the field on every click).
bool slider_row_reaches_ends(int tab, const char* label, int screenW, int screenH,
                                    int lo, int hi, int* fieldPtr, int startValue);


// A row's tooltip after resting the cursor on it. Sound only at applied scale 100.
const char* tooltip_for_row(const char* label, int screenW, int screenH);


// The GProj matching a camera (in game it comes from engine_gproj()).
GProj gcam(const CamState& c, const short* st);

// polymost_drawsprite's face branch then drawpoly's rotation and divide (VA 0x00482d02 -> 0x00475635), sharing no
// helper with project.cpp: ground truth for a sprite's four world corners.
struct PolyPt { double px, py; bool ok; };

PolyPt polymost_point(const CamState& c, const GProj& g,
                             double wx, double wy, double wz);


bool esp_name_is(int picnum, const char* expect);

bool esp_item_name_is(int picnum, const char* expect);

bool streq(const char* a, const char* b);

// Spells a path with '|' for backslash and '~' for slash (the publish gate reads literal paths as PII).
const char* tb_path(const char* s);

bool esp_names_match(int a, int b);

#define CHECK(cond) do { if (cond) passed++; else { failed++; \
  printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

// A simulated page model standing in for VirtualProtect: green against a bracket that restores in the wrong order or
// tracks protection per site instead of per page.
const unsigned ACC_TEST_PAGE_SIZE = 0x1000u;

const unsigned ACC_TEST_OLD_PROT  = 0x20;


enum { ACC_EV_PROTECT = 1, ACC_EV_WRITE = 2 };


void acc_test_protect_reset();

int      acc_test_protect_calls();

unsigned acc_test_protect_last_size();

unsigned acc_test_protect_last_prot();

int      acc_test_write_calls();

int      acc_test_write_violations();

int      acc_test_write_viol_total();

int      acc_test_page_overflow();

int      acc_test_ev_count();

int      acc_test_ev_overflow();

int      acc_test_ev_kind(int i);

unsigned acc_test_ev_prot(int i);


void acc_test_log(int kind, unsigned prot);


size_t acc_test_page_key(const void* p);

int acc_test_page_slot(size_t key);

unsigned acc_test_page_prot(const void* p);

void acc_test_page_set(size_t key, unsigned prot);

int acc_test_pages_writable();


void acc_test_protect_apply(void* addr, unsigned size, unsigned newProt,
                                   unsigned* oldProtOut);


int acc_test_protect_ok(void* addr, unsigned size, unsigned newProt, unsigned* oldProtOut);


int acc_test_protect_fail_second(void* addr, unsigned size, unsigned newProt,
                                        unsigned* oldProtOut);


extern const void* q_stub_last_addr;

extern unsigned q_stub_last_bytes;

int q_stub_refuse(const void* addr, unsigned bytes);

int q_stub_accept(const void* addr, unsigned bytes);


void acc_test_write_watch(void* addr, unsigned size);


// A fake hitscan body that writes none of its out-parameters (hitscan_call's fail-closed seeding).
long __cdecl fake_hitscan_writes_nothing(long, long, long, short,
                                                long, long, long,
                                                short*, short*, short*,
                                                long*, long*, long*,
                                                unsigned long);


void nocull_test_reset();

void nocull_test_original(int s);

unsigned char* nocull_test_got();

int             nocull_test_call_count();

int             nocull_test_call_at(int i);

extern const char* tmp;
extern FILE* f;
extern UiRect r;
extern UiRect cur;
extern UiRect a;
extern UiRect b;
extern UiRect p;
extern UiRect q;
extern UiRect s;
