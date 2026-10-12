# Triggerbot engine facts: the held fire bit, the hitscan ray, the input record

What the triggerbot needs from `duke3d.exe`: how a held fire bit behaves, the ray `shoot`
casts, and where the input record is written.
Method: Ghidra decompile of `processinput` (dump `processinput.c` in the Ghidra project's
`dumps` folder); the line numbers below are that dump's.

## A. The weapon section: holding the fire bit re-fires every gun

`processinput` weapon section, decompile `processinput.c` lines 1101-1420. The fire bit
is `sync.bits & 4` (`local_30 & 4` in the dump). A shot STARTS only when
`(bits & 4) != 0 && kickback_pic == 0 && fist_incs == 0 && last_weapon == -1 &&
(weapon_pos == 0 || holster_weapon == 1)` (line 1112).

While `kickback_pic != 0` the per-weapon cycle advances regardless of the bit and
returns `kickback_pic` to 0 at its end:

- pistol, case 1, lines 1146-1161
- shotgun, case 2, line 1197
- chaingun, case 3, lines 1221/1227 (stops early when the bit drops)
- RPG, case 4, line 1245
- shrinker/expander, cases 6 and 11, lines 1301/1316
- devastator, case 7, line 1337
- tripbomb, case 8, line 1350
- freezer, case 9, lines 1371/1374
- remote, case 10, line 1388

With the bit still held on the tic the cycle returns to 0, the start condition is true
again and the next shot begins: A HELD BIT RE-FIRES EVERY GUN, NO RELEASE EDGE.

The one exception is `rapid_fire_hold`, set only by the pipebomb (case 5, line 1250, at
`kickback_pic == 6` with the bit held) and cleared only when the bit drops (lines
1105-1110): the pipebomb's hold-to-throw, a WC_NONE weapon the triggerbot's decision
already refuses (`tbot_decide`'s `weaponUsable` term). The knee (case 0, line 1123)
repeats while held too but is WC_MELEE, likewise refused.

## B. `shoot`'s hitscan ray and origin

`shoot`'s player hitscan, no autoaim target (VA 0x004f4390-0x004f447b): `zvel`
(`[EBP-0x2c]`, built from `(100 - horiz - horizoff) << 5` per targeting.md,
section D) gets the spread `+= 0x80 - (krand & 0xff)` at 0x004f43a9-0x004f43bd, the yaw `sa` gets
`+= 0x40 - (krand & 0x7f)` at 0x004f43f5-0x004f43ff (both are what the accuracy feature
zeroes), then

```
hitscan(sx, sy, sz, sect, sintable[(sa + 512) & 2047], sintable[sa & 2047], zvel << 6,
        &hitsect, &hitwall, &hitsprite, &hitx, &hity, &hitz, 0x01000040)
```

at 0x004f4417 (the CLIPMASK1 push) to 0x004f4476 (the call through thunk 0x00403d05).
The autoaim branch's call at 0x004f3a1e has the same shape with
`zvel = ((z - sz - dal) << 8) / (dist + 1)`.

The origin `sz = posz + pyoff + 0x400` is at 0x004f36cc (`LEA EAX, [ECX + EDX + 0x400]`).

`tbot_ray` (tbot.h) is exactly the no-autoaim, no-spread shape: `vx = sintable[(ang +
512) & 2047]`, `vy = sintable[ang & 2047]`, `vz = ((100 - horiz - horizoff) << 5) << 6`.

## C. The input record: `loc`, `bits`, the write site

`loc` lives at `rva::RVA_LOC`, the 12-byte `input` struct `getinput` fills each tic and
`processinput` consumes as `sync[snum]`. Its `bits` field is the unsigned long at
offset +8, and the fire button is bit 4 (`sync.bits & 4`, section A above). It is an
INPUT record, not player state.

The triggerbot's write (`engine_input_set_fire`, `tbot_tic`) goes inside `hooked_getinput`,
after the original call, in exactly the spot `mouseaim_tic_apply` writes `avel`/`horz`,
i.e. after `mouseaim_tic_apply(player);` and before the input lockout's `loc` memset.
Never cleared by the triggerbot itself: the player's own key may be holding the bit
too, and the lockout's memset already clears the whole record when the panel is open.

## D. The tic rate

30 game tics a second (mouse.md): input is sampled once per tic, which is what
`tbot_delay_tics` (tbot.h) converts the delay slider's milliseconds against.
