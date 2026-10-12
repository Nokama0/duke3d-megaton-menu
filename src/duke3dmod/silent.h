#pragma once

// Silent aim: a jump-thunk hook on `shoot` (thunk RVA 0x0000316B -> body 0x000F3640) swaps the solved angle/pitch into
// the five player_struct fields it reads, for the local player's shots only, and restores them. It does NOT remove the
// one-tic lag: hooked_shoot runs before aim_tic publishes this tic's snapshot (~33 ms, up to ~50 xy units).

// Installs the hook, once from mod_deferred_init; false means off this session. The fourth of five thunk fingerprint checks.

bool silent_install();
