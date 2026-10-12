#pragma once

const int FOV_MIN = 70;
const int FOV_MAX = 140;
const int FOV_DEFAULT = 90;

int fov_to_viewingrange(int degrees);

// Scales the engine's viewingrange by a 16.16 multiplier (the argument carries the aspect
// correction). 64-bit intermediate.
long fov_scale_viewingrange(long viewingrange, int multiplier);
