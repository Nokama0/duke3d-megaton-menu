#pragma once
void overlay_frame();
void overlay_install();

// True when anything is drawn (panel, ESP category, damage numbers, secret triggers); canvas_begin/end touch no GL state
// otherwise. dmgNumbersOn and secretTriggersOn are own terms, not in esp_any_on(), which is also esp_draw's gate.
inline bool overlay_should_draw(bool menuOpen, bool anyEspOn, bool dmgNumbersOn,
                                bool secretTriggersOn) {
  return menuOpen || anyEspOn || dmgNumbersOn || secretTriggersOn;
}

// World overlays belong to a level being played; the panel is not gated on this, it turns them off.

inline bool overlay_world_should_draw(bool inPlay) { return inPlay; }
