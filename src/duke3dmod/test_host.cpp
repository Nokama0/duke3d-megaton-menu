// The Nuke Panel pure-function suite, one translation unit per area. This file is the ORDER: every check lives in a tb_
// function in tests/test_<area>.cpp (declared in tests/test_blocks.h) and main() calls them in the order the old single
// main() ran, over the same globals, so a block that leaves g_cfg or menu state a certain way hands it to the next.

// To add a check: append a tb_ function to its area file, declare it in tests/test_blocks.h and call it below after
// whatever it depends on (normally the end). Fixtures and CHECK: tests/test_common.h. tools/split_tests.py generated
// this layout.

#include "tests/test_common.h"
#include "tests/test_blocks.h"

int passed = 0, failed = 0;

int main() {
  tb_0001_harness_smoke_test_clamp_int_first();
  tb_0002_walkcounter_common_h_pure_walk();
  tb_0003_defaults_when_file_absent_menu();
  tb_0004_phase_5a_task_3_widened();
  tb_0005_remove_tmp();
  tb_0006_overlong_lines_skipped_partially_parsed();
  tb_0007_overlong_line_followed_normal_unknown();
  tb_0008_char_buf_2048_0_fopen_s();
  tb_0009_remove_tmp_2();
  tb_0010_final_line_trailing_newline_should();
  tb_0011_remove_tmp_3();
  tb_0012_non_final_line_whose_content();
  tb_0013_remove_tmp_config_round_trip();
  tb_0014_fov_scale_viewingrange_pure_scaling_arithmetic_behind();
  tb_0015_hit_testing_inclusive_top_left();
  tb_0016_restore();
  tb_0017_vk_to_sdl_scancode_default_menu_key_vk_insert();
  tb_0018_thunk_encode_rel32_thunk_decode_target_pure_rel32_arithmetic();
  tb_0019_player_index_valid_player_field_rva_pure_arithmetic_behind();
  tb_0020_engine_constant_pinning_final_review();
  tb_0021_task_1_targeting_accessors();
  tb_0022_task_2_targeting_core();
  tb_0023_read_field_bytes_write_field_bytes_pure_width_aware();
  tb_0024_collapse_view_values_pure_which_captured_value();
  tb_0025_view_freeze_action_settling_tics_after_call_pure_clock_phase();
  tb_0026_summarize_samples_pure_min_max_distinct();
  tb_0027_build_mod_paths_dllmain_s_path_building();
  tb_0028_over_long_directory_deeply_nested();
  tb_0029_empty_directory_getmodulefilenamea_gave_us();
  tb_0030_canvas_hazard_params_ok_guards_stripe_loop_canvas_hazard();
  tb_0031_phase_5b_task_8_atlas();
  tb_0032_panel_left_anchored_fully_screen();
  tb_0033_offset_0_width_4_reads();
  tb_0034_engine_sourced_projection_project_h();
  tb_0035_enemy_esp_bounding_box_project();
  tb_0036_enemy_esp_names_health_esp_names();
  tb_0037_enemy_esp_health_bar_geometry();
  tb_0038_enemy_esp_marker_layout_esp_layout();
  tb_0039_item_esp_pickup_name_table();
  tb_0040_item_esp_access_card_named();
  tb_0041_item_esp_presence_category_gate();
  tb_0042_enemy_esp_config_fields_config();
  tb_0043_item_esp_config_field_config();
  tb_0044_tile_size_arrays_esp_box();
  tb_0045_every_enemy_picnum_name_table();
  tb_0046_overlay_should_draw_overlay_h_task_3();
  tb_0047_both_opens();
  tb_0048_visuals_tab_draw_distance_cull();
  tb_0049_what_panel_actually_draws();
  tb_0050_task_6_combat_tab_restructured();
  tb_0051_round_2_finding_1_visuals();
  tb_0052_phase_3_task_3_tic();
  tb_0053_phase_3_task_4_aimbot();
  tb_0054_phase_3_task_3_aim();
  tb_0055_phase_3_task_5_silent();
  tb_0056_phase_3_task_5_silent_2();
  tb_0057_phase_4_task_1_aim();
  tb_0058_phase_4_task_2_freecam();
  tb_0059_wave_3_task_a1_general();
  tb_0060_phase_4_task_3_free();
  tb_0061_phase_4_task_3_fix();
  tb_0062_phase_4_task_5_perfect();
  tb_0063_phase_5b_task_4_key();
  tb_0064_capture_state_machine_every_transition();
  tb_0065_config_side_four_rows_conf();
  tb_0066_aimbind_default_round_trip_aimbutton();
  tb_0067_game_s_own_cfg();
  tb_0068_cfg_path_char();
  tb_0069_four_rows_panel_cons();
  tb_0070_view_tab_s_panel_key();
  tb_0071_combat_tab_s_button_row();
  tb_0072_phase_5b_task_5_ui();
  tb_0073_canvas_actually_spends();
  tb_0074_panel_75_100_200_cons();
  tb_0075_phase_5b_task_5_fix();
  tb_0076_c1_b_live_panel();
  tb_0077_phase_5b_task_6_initialisation();
  tb_0078_phase_5b_task_6_fix();
  tb_0079_phase_5b_task_5_fix_2();
  tb_0080_phase_6_config_keys();
  tb_0081_phase_6_rows();
  tb_0082_phase_6_light_rules();
  tb_0083_phase_6_screenshot_rules();
  tb_0084_phase_7_mouse_aim_rules();
  tb_0085_phase_7_config();
  tb_0086_phase_7_rows();
  tb_0087_phase_7_fix1_pending_caps();
  tb_0088_phase_7_task4_view_doubles_match_polymost();
  tb_0090_phase_7_fixb_viewscreen_gate();
  tb_0091_phase_7_fixc_frame_gates_and_frustum();
  tb_0092_phase_7_fixd_pause_lurch_and_locavel_clamp();
  tb_0093_phase_7_fixe_int_trig_and_pitch_match();
  tb_0094_phase_7_fixf_globalang_of_view();
  tb_0095_phase_7_fixg_mirror_view();
  tb_0096_phase_8_task1_damage_numbers();
  tb_0097_phase_8_task2_trigger_resolver();
  tb_0098_phase_8_task3_fire_rate();
  tb_0099_phase_8_task4_anim_core_and_settings_tab();
  tb_0100_phase_8_task5_glass_primitives_and_geometry();
  tb_0101_phase_8_fixb_panel_transform();
  tb_0102_phase_9_task1_world_layer();
  tb_0103_phase_9_task2_combination_tint();
  tb_0104_phase_9_task3_weapon_switch_speed();
  tb_0105_phase_9_task4_secret_volume();
  tb_0106_aim_ahead();
  tb_0107_triggerbot_core();
  tb_0108_triggerbot_config();
  tb_0109_triggerbot_rows();
  tb_0110_actorstate_core();
  tb_0111_shot_geometry();
  tb_0112_shot_hitscan_halfw();
  tb_0113_enemy_states();
  tb_0114_hold_view_rule();
  tb_0115_camera_follows_mouse_aim();
  tb_0116_ui_polish_rank9();
  tb_0117_hotkeys_group();
  tb_0118_bind_field_hover_colours();
  tb_0119_widened_key_table();
  tb_0120_tooltip_rule();
  tb_0121_tooltips_drawn();
  tb_0122_panel_rules();
  tb_0123_panel_drawn();
  tb_0124_entry_rules();
  tb_0125_slider_entry_drawn();
  tb_0126_entry_selection();
  tb_0127_secret_exit_esp();
  tb_0128_keycards_and_locks();
  tb_0129_breakables_split();
  tb_0130_autosave_rule();
  tb_0131_dollhouse_rule();
  tb_0132_dollhouse_depth();
  tb_0133_dollhouse_wall_records();
  tb_0134_freecam_transport();
  tb_0135_secret_path();
  tb_0136_teleport_marker();
  tb_0137_ini_nul_keeps_next_line();


  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
