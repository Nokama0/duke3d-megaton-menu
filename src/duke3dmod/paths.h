#pragma once
#include <stdio.h>

// Builds the ini and log paths under <dir> (ends in a separator or is empty). _TRUNCATE, not sprintf_s: a long dir
// must not trip the CRT invalid-parameter handler. False if truncated.
inline bool build_mod_paths(const char* dir, char* iniOut, size_t iniCap, char* logOut, size_t logCap) {
  int iniRet = _snprintf_s(iniOut, iniCap, _TRUNCATE, "%sduke3dmod.ini", dir);
  int logRet = _snprintf_s(logOut, logCap, _TRUNCATE, "%sduke3dmod.log", dir);
  return iniRet >= 0 && logRet >= 0;
}

// The game's cfg: install is <GameDir>\bin\duke3d.exe, cfg at <GameDir>\gameroot\duke3d.cfg. Truncate, never abort.
inline bool build_gamecfg_path(const char* exeDir, char* out, size_t cap) {
  const int ret = _snprintf_s(out, cap, _TRUNCATE, "%s..\\gameroot\\duke3d.cfg", exeDir);
  return ret >= 0;
}
