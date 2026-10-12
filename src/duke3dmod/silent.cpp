#include "silent.h"
#include "common.h"
#include "config.h"
#include "engine.h"
#include "hooks.h"
#include "log.h"
#include "target.h"
#include "aim.h"
#include "actorstate.h"   // actor_state_cached/targetable: the live re-solve's second identity term
#include "accuracy.h"      // header-only (this file cannot link into test_host)

// The hook. Decides nothing: every decision, including SilentBracket's save/substitute/restore lifetime, is a pure function
// or type in target.h or common.h. Step order, write guards and silent_bracket_arm's argument order stay review-only (aim.h).

typedef void (__cdecl* ShootFn)(short, short);
static ShootFn s_origShoot = 0;

static unsigned s_redirectLogCount = 0;
static bool s_loggedIndexInvalid = false;

static unsigned s_accLogCount = 0;
static bool s_loggedShotUnpatched = false;
static bool s_loggedAccNotVerified = false;
// Static: a nested shoot has its own local accBracket starting disarmed, so only a shared flag sees an outer bracket still
// armed (accuracy_may_attempt_arm).
static bool s_accBracketArmed = false;
static bool s_loggedAccReentrant = false;

// First site whose five bytes are not the E8 rel32 call to krand's thunk, or 0; only so the degradation log can name the
// address and bytes found.
static const rva::AccSite* acc_first_mismatch(const rva::AccSite* sites, int count) {
  if (!sites) return 0;
  for (int i = 0; i < count; i++)
    if (!acc_site_matches(g_base, sites[i].rva)) return &sites[i];
  return 0;
}

// Reentrant: shoot calls checkhitsprite, which calls shoot back at thirteen sites (0x00511883, 0x00511e61..0x00511f87,
// 0x00512488..0x005124be) with a blood sprite (picnum 0x8f8..0x8fb) the atwith filter refuses; each level restores only what
// it saved. No early returns, so destructors run (SEH unwind runs none: a crash in progress). s_accBracketArmed guards nesting.
static void __cdecl hooked_shoot(short i, short atwith) {
  SilentBracket bracket;

  PatchBracket accBracket;

  // --- read --- Both reads unconditional so g_cfg.silenton is one term of one decision. engine_player_aim is safe on any
  // binary (valid=false when unverified).
  const PlayerAim p = engine_player_aim();

  AimSnapshot snap;
  snap.targetIndex = -1;
  snap.solvedAng = 0;
  snap.solvedHoriz = 0;
  snap.leadScale = 0;
  snap.choice = 0;
  snap.los = false;
  snap.targetPicnum = 0;
  snap.targetRung = 0;
  const bool haveSnap = aim_snapshot(&snap);

  const bool isLocalPlayerShot = p.valid && p.spriteIndex >= 0 && (int)i == p.spriteIndex;
  const bool atwithIsPlayerShot = silent_atwith_is_player_shot((int)atwith);
  const bool redirect = silent_decide(
      g_cfg.silenton != 0,
      isLocalPlayerShot,
      atwithIsPlayerShot,
      haveSnap && silent_snapshot_usable(snap.targetIndex, snap.solvedAng, snap.solvedHoriz),
      haveSnap && snap.los,
      p.ang,
      snap.solvedAng,
      target_degrees_to_units(silent_maxangle_clamped(g_cfg.silentmaxangle)));

  // --- measure --- The one place the live miss exists: facing yaw vs the yaw that hits the published aim point at the instant
  // shoot runs. A redirect logs as via=silent. Bounded, writes nothing (aim.h).
  if (isLocalPlayerShot && atwithIsPlayerShot) aim_shot_diag(p, (int)atwith, redirect);

  if (redirect) {
    if (engine_verified() && player_index_valid(p.index, MAX_PLAYER_INDEX)) {
      const unsigned psRva = player_field_rva(rva::RVA_PS, p.index, rva::PS_STRIDE, 0);

      // Re-solve from where the target is now: this hook runs inside processinput before aim_tic publishes, so the published
      // pair is a tic stale. Any mismatch below falls back to it.
      int rsAng = snap.solvedAng;
      int rsHoriz = snap.solvedHoriz;
      bool rsLive = false;
      bool rsConeRejected = false;

      // Identity first: a Build slot is recycled the instant its actor dies, and even the same picnum can be a corpse, so
      // actor_state_targetable is checked via the per-tic cache.
      const int livePicnum = engine_sprite_field_s(snap.targetIndex, SPR_PICNUM);
      if (livePicnum == snap.targetPicnum &&
          actor_state_targetable(actor_state_cached(snap.targetIndex))) {
        const int lx = engine_sprite_field_l(snap.targetIndex, SPR_X);
        const int ly = engine_sprite_field_l(snap.targetIndex, SPR_Y);
        const int lz = engine_sprite_field_l(snap.targetIndex, SPR_Z);
        const int lyrepeat = engine_sprite_field_u8(snap.targetIndex, SPR_YREPEAT);
        // The rung aim_tic chose, not rung 0: rung 0 may have been rejected by cansee, and substituting it would aim into cover.
        Point3 liveRaw;
        liveRaw.x = lx;
        liveRaw.y = ly;
        liveRaw.z = target_probe_z(lz, lyrepeat, engine_tilesizy(livePicnum),
                                   aim_point_from_config(g_cfg.aimpoint),
                                   snap.targetRung);

        // Last tic's estimate (this hook runs before aim_tic); zeroes on refusal make the lead a no-op.
        int vx = 0, vy = 0, vz = 0;
        aim_smoothed_velocity(snap.targetIndex, snap.targetPicnum, &vx, &vy, &vz);

        Point3 eye;
        eye.x = p.eyex;
        eye.y = p.eyey;
        eye.z = p.eyez;
        const SilentResolve rs = silent_resolve(
            eye, p.horizoff, target_weapon_info(p.currWeapon),
            snap.solvedAng, snap.solvedHoriz,
            liveRaw, vx, vy, vz,
            aim_lead_vel(target_weapon_info(p.currWeapon).vel, g_cfg.aimlead),
            aim_leadscale_clamped(g_cfg.aimleadscale),
            p.ang, target_degrees_to_units(silent_maxangle_clamped(g_cfg.silentmaxangle)));
        rsAng = rs.ang;
        rsHoriz = rs.horiz;
        rsLive = rs.live;
        rsConeRejected = rs.coneRejected;
      }

      if (silent_bracket_arm(&bracket, g_base, psRva, rsAng, rsHoriz)
          && g_cfg.debuglog != 0 && s_redirectLogCount < 10) {
        s_redirectLogCount++;
        // choice: 1 = led solve, 2 = raw point. live: 1 = this tic's re-solve, 0 = published fallback; " cone" marks a live=0
        // refused for exceeding "Max redirect".
        LOGI("silent: redirect #%u atwith=0x%X target=%d live=%d%s ang %ld->%d (pub %d) "
             "horiz %ld->%d (pub %d) leadscale=%d choice=%d auto_aim was %ld",
             s_redirectLogCount, (unsigned)(unsigned short)atwith, snap.targetIndex,
             rsLive ? 1 : 0, rsConeRejected ? " cone" : "",
             bracket.save.v[SF_ANG], rsAng, snap.solvedAng,
             bracket.save.v[SF_HORIZ], rsHoriz, snap.solvedHoriz,
             snap.leadScale, snap.choice,
             bracket.save.v[SF_AUTO_AIM]);
      }
    } else if (!s_loggedIndexInvalid) {
      s_loggedIndexInvalid = true;
      LOGE("silent: cannot substitute, engine verified=%d myconnectindex=%d -- "
           "silent aim will keep passing shots straight through",
           engine_verified() ? 1 : 0, p.index);
    }
  }

  // --- accuracy patch, this shot only --- Immediately before the one original call, so the window is exactly the call. One
  // pair, never a union. Success is logged after an early release: log_write blocks on file I/O and .text would sit RWX.
  bool accPatched = false;
  int accPatchedPair = -1;
  const rva::AccSite* accPatchedSites = 0;
  {
    // The shooter test: accuracy_pair_for cannot tell an enemy hitscan from the player's (LIZTROOP/PIGCOP/ENFORCER share the
    // atwith values), and every enemy shot would false-latch a weapon off.
    const bool isPlayerShot = isLocalPlayerShot;
    const int accPair = accuracy_pair_for(atwith,
                                          g_cfg.accpistol   != 0, g_cfg.accchaingun != 0,
                                          g_cfg.accshotgun  != 0, g_cfg.accexpander != 0);
    const rva::AccSite* accSites = accuracy_pair_sites(accPair);
    const int accCount = accSites ? ACC_PAIR_SITES : 0;
    // engine_verified() guards a read too: the fingerprint pass reads five bytes at each RVA before any write.
    const bool engineOk = engine_verified();
    if (accCount > 0 && accSites && accuracy_should_arm(accPair, isPlayerShot) && engineOk) {
      // Checked first, so a nested call makes no fingerprint read and cannot see the outer bracket's patch bytes.
      if (!accuracy_may_attempt_arm(s_accBracketArmed)) {
        if (!s_loggedAccReentrant) {
          s_loggedAccReentrant = true;
          LOGI("accuracy: shoot re-entered while pair %d was already armed by "
               "the outer call; the nested shot fires with the outer bracket's "
               "own patch already in place and no new arm was attempted",
               accPair);
        }
      } else {
        const bool armed = accuracy_note_arm_result(
            accPair, patch_bracket_arm(&accBracket, g_base, accSites, accCount));
        if (armed) {
          s_accBracketArmed = true;
          accPatched = true;
          accPatchedPair = accPair;
          accPatchedSites = accSites;
        } else {
          // Once per pair; accuracy_note_arm_result has latched this pair off, the others keep working.
          const rva::AccSite* bad = acc_first_mismatch(accSites, accCount);
          if (bad) {
            const unsigned char* seen = g_base + bad->rva;
            LOGE("accuracy: site rva=%08X holds %02X %02X %02X %02X %02X, not the "
                 "call to krand this DLL expects -- pair %d is off for this session, "
                 "the other weapons are unaffected and this shot keeps its spread",
                 bad->rva, seen[0], seen[1], seen[2], seen[3], seen[4], accPair);
          } else {
            // Every fingerprint matched: page protection refused (no hook installed, or VirtualProtect said no).
            LOGE("accuracy: could not make the spread sites writable for pair %d "
                 "(rva=%08X) -- that pair is off for this session, the other "
                 "weapons are unaffected and this shot keeps its spread",
                 accPair, accSites[0].rva);
          }
        }
      }
    } else if (isPlayerShot && accCount > 0 && !engineOk && !s_loggedAccNotVerified) {
      // An unverified binary would otherwise skip silently. One-shot; latches no pair, the binary is the problem.
      s_loggedAccNotVerified = true;
      LOGE("accuracy: engine fingerprint check failed, perfect accuracy is off "
           "for every weapon this session; the switches will show as on and do "
           "nothing until this DLL recognises the binary again");
    }
  }

  // --- call, then unwind --- s_origShoot is null only if the hook never installed.
  if (s_origShoot) {
    s_origShoot(i, atwith);
  } else if (accBracket.armed && !s_loggedShotUnpatched) {
    s_loggedShotUnpatched = true;
    LOGE("accuracy: the spread sites were patched but the original shoot is not "
         "installed, so this shot was not fired at all; the bytes are restored "
         "on the way out");
  }

  // Early release before the blocking log call: disarms first, so .text is not writable during log_write's I/O.
  if (accPatched) {
    patch_bracket_release(&accBracket);
    s_accBracketArmed = false;
    if (g_cfg.debuglog != 0 && s_accLogCount < 10) {
      s_accLogCount++;
      LOGI("accuracy: patched shot #%u atwith=0x%X pair=%d sites %08X,%08X "
           "centres %02X,%02X verified=1",
           s_accLogCount, (unsigned)(unsigned short)atwith, accPatchedPair,
           accPatchedSites[0].rva, accPatchedSites[1].rva,
           (unsigned)accPatchedSites[0].centre, (unsigned)accPatchedSites[1].centre);
    }
  }

}

bool silent_install() {
  void* origTarget = 0;
  const bool ok = hook_jmp_thunk(rva::THUNK_SHOOT_RVA, rva::THUNK_SHOOT_BYTES,
                                 (void*)&hooked_shoot, &origTarget);
  // Fourth of twelve thunk fingerprint checks (engine.h): if these bytes differ every hardcoded RVA is suspect.
  engine_mark_hook_result(ok);
  if (ok) {
    s_origShoot = (ShootFn)origTarget;
    const unsigned origRva = (unsigned)((unsigned char*)origTarget - g_base);
    LOGI("silent: shoot thunk hook installed; recovered original target rva=%08X "
         "(expect %08X)%s",
         origRva, rva::SHOOT_BODY_RVA,
         origRva == rva::SHOOT_BODY_RVA ? "" : " -- MISMATCH, this may not be the right choke point");
  } else {
    LOGE("silent: shoot thunk hook failed, silent aim is off for this session; "
         "everything else keeps running and shots go exactly where the engine aims them");
  }
  return ok;
}
