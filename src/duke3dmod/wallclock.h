#pragma once

// Free-running ms counter; UNSIGNED AND WRAPS by design: take an age as `now - t0`, exact across the wrap.
unsigned wallclock_ms();
