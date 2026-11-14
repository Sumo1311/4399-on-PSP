cmake_minimum_required(VERSION 3.16)
project(BleachVsNaruto5DPlayPSP VERSION 0.1.0 LANGUAGES C CXX)

option(BVN_RESOURCE_DIAGNOSTICS "Record physical PSP resource failures (local diagnostic build)" OFF)
if(BVN_RESOURCE_DIAGNOSTICS)
    add_compile_definitions(BVN_RESOURCE_DIAGNOSTICS=1)
endif()

add_library(bvn_stage STATIC src/stage_asset.cpp src/stage_motion.cpp src/map_optical_asset.cpp src/transition_asset.cpp)
target_include_directories(bvn_stage PUBLIC src)
target_compile_features(bvn_stage PUBLIC cxx_std_20)
target_link_libraries(bvn_stage PUBLIC z)
target_compile_options(bvn_stage PRIVATE -Wall -Wextra -ffp-contract=off)

add_library(bvn_fighter STATIC src/fighter_asset.cpp src/fighter_motion.cpp src/fighter_source_reaction.cpp src/fighter_source_hit.cpp src/fight_effects.cpp
    src/versus_round_source.cpp src/fight_source_scene.cpp
    src/combat_effects.cpp
    src/fight_camera.cpp src/fight_match.cpp src/fight_projectile.cpp src/fight_attacker.cpp src/versus_round.cpp
    src/fighter_cpu.cpp src/fighter_ai_policy.cpp src/arcade_run.cpp src/roster_asset.cpp src/musou_campaign.cpp src/musou_battle.cpp
    src/musou_party.cpp src/game_settings.cpp src/musou_source_control.cpp src/musou_source_scene.cpp)
target_link_libraries(bvn_fighter PUBLIC bvn_stage z)
target_compile_options(bvn_fighter PRIVATE -Wall -Wextra -ffp-contract=off)
if(PSP)
    add_executable(bvn_allocation_pressure_qa tests/allocation_pressure_psp.cpp)
    target_compile_options(bvn_allocation_pressure_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_allocation_pressure_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/allocation_pressure.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_allocation_pressure_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_allocation_pressure_qa TITLE "BVN Allocation Pressure QA" VERSION 00.68 OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/allocation-pressure")
    add_executable(bvn_emission_qa tests/emission_psp.cpp)
    target_compile_options(bvn_emission_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_emission_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/emission.map")
    target_link_libraries(bvn_emission_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_emission_qa TITLE "BVN Emission QA" VERSION 00.67
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/emission")
    add_executable(bvn_paint_qa tests/paint_psp.cpp)
    target_compile_options(bvn_paint_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_paint_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/paint.map")
    target_link_libraries(bvn_paint_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_paint_qa TITLE "BVN Paint QA" VERSION 00.66
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/paint")
    add_executable(bvn_paint_gpu_qa tests/paint_gpu_psp.cpp)
    target_compile_options(bvn_paint_gpu_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_paint_gpu_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/paint_gpu.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_paint_gpu_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_paint_gpu_qa TITLE "BVN Paint GPU QA" VERSION 00.66
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/paint-gpu")
    add_executable(bvn_viewport_qa tests/viewport_psp.cpp)
    target_compile_options(bvn_viewport_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_viewport_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/viewport.map")
    target_link_libraries(bvn_viewport_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_viewport_qa TITLE "BVN Camera QA" VERSION 00.65
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/viewport")
    add_executable(bvn_lifecycle_qa tests/lifecycle_psp.cpp)
    target_compile_options(bvn_lifecycle_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_lifecycle_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/lifecycle.map")
    target_link_libraries(bvn_lifecycle_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_lifecycle_qa TITLE "BVN Lifecycle QA" VERSION 00.64
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/lifecycle")
    add_executable(bvn_scene_order_qa tests/scene_order_psp.cpp)
    target_compile_options(bvn_scene_order_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_scene_order_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/scene_order.map")
    target_link_libraries(bvn_scene_order_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_scene_order_qa TITLE "BVN Scene Order QA" VERSION 00.63
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/scene-order")
    add_executable(bvn_combo_gpu_qa tests/combo_render_psp.cpp)
    target_compile_options(bvn_combo_gpu_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combo_gpu_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combo_gpu.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_combo_gpu_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_combo_gpu_qa TITLE "BVN Combo GPU QA" VERSION 00.62
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combo-gpu")
    add_executable(bvn_combo_qa tests/combo_psp.cpp)
    target_compile_options(bvn_combo_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combo_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combo.map")
    target_link_libraries(bvn_combo_qa PRIVATE bvn_fighter bvn_renderer pspdisplay psppower)
    create_pbp_file(TARGET bvn_combo_qa TITLE "BVN Combo QA" VERSION 00.62
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combo")
    add_executable(bvn_fighter_events_qa tests/fighter_events_psp.cpp)
    target_compile_options(bvn_fighter_events_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_fighter_events_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/fighter_events.map")
    target_link_libraries(bvn_fighter_events_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_fighter_events_qa TITLE "BVN Fighter Events QA" VERSION 00.61
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fighter-events")
    add_executable(bvn_hit_controller_qa tests/hit_controller_psp.cpp)
    target_compile_options(bvn_hit_controller_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_hit_controller_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/hit_controller.map")
    target_link_libraries(bvn_hit_controller_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_hit_controller_qa TITLE "BVN Hit Controller QA" VERSION 00.60
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/hit-controller")
    add_executable(bvn_reaction_lifecycle_qa tests/reaction_lifecycle_psp.cpp)
    target_compile_options(bvn_reaction_lifecycle_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_reaction_lifecycle_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/reaction_lifecycle.map")
    target_link_libraries(bvn_reaction_lifecycle_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_reaction_lifecycle_qa TITLE "BVN Reaction QA" VERSION 00.59
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reaction-lifecycle")
    add_executable(bvn_hud_gpu_qa tests/hud_render_psp.cpp)
    target_compile_options(bvn_hud_gpu_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_hud_gpu_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/hud_gpu.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_hud_gpu_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_hud_gpu_qa TITLE "BVN HUD GPU QA" VERSION 00.58
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/hud-gpu")

    add_executable(bvn_hud_session_qa tests/hud_session_psp.cpp)
    target_compile_options(bvn_hud_session_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_hud_session_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/hud_session.map")
    target_link_libraries(bvn_hud_session_qa PRIVATE bvn_fighter bvn_renderer pspdisplay psppower)
    create_pbp_file(TARGET bvn_hud_session_qa TITLE "BVN HUD Session QA" VERSION 00.58
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/hud-session")
    add_executable(bvn_dispatch_transition_qa tests/dispatch_transition_psp.cpp)
    target_compile_options(bvn_dispatch_transition_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_dispatch_transition_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/dispatch_transition.map")
    target_link_libraries(bvn_dispatch_transition_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_dispatch_transition_qa TITLE "BVN Dispatch QA" VERSION 00.57
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/dispatch-transition")

    add_executable(bvn_start_end_qa tests/start_end_psp.cpp)
    target_compile_options(bvn_start_end_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_start_end_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/start_end.map")
    target_link_libraries(bvn_start_end_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_start_end_qa TITLE "BVN Start End QA" VERSION 00.56
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/start-end")

    add_executable(bvn_round_order_qa tests/round_order_psp.cpp)
    target_compile_options(bvn_round_order_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_round_order_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/round_order.map")
    target_link_libraries(bvn_round_order_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_round_order_qa TITLE "BVN Round Order QA" VERSION 00.55
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/round-order")

    add_executable(bvn_controller_session_qa tests/controller_session_psp.cpp)
    target_compile_options(bvn_controller_session_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_controller_session_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/controller_session.map")
    target_link_libraries(bvn_controller_session_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_controller_session_qa TITLE "BVN Controller Session QA" VERSION 00.54
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/controller-session")

    add_executable(bvn_controller_order_qa tests/controller_order_psp.cpp)
    target_compile_options(bvn_controller_order_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_controller_order_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/controller_order.map")
    target_link_libraries(bvn_controller_order_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_controller_order_qa TITLE "BVN Controller Order QA" VERSION 00.53
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/controller-order")

    add_executable(bvn_source_delivery_qa tests/source_delivery_psp.cpp)
    target_compile_options(bvn_source_delivery_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_source_delivery_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/source_delivery.map")
    target_link_libraries(bvn_source_delivery_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_source_delivery_qa TITLE "BVN Source Delivery QA" VERSION 00.52
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/source-delivery")

    add_executable(bvn_controller_clock_qa tests/controller_clock_psp.cpp)
    target_compile_options(bvn_controller_clock_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_controller_clock_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/controller_clock.map")
    target_link_libraries(bvn_controller_clock_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_controller_clock_qa TITLE "BVN Controller Clock QA" VERSION 00.51
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/controller-clock")

    add_executable(bvn_game_clock_qa tests/game_clock_psp.cpp)
    target_include_directories(bvn_game_clock_qa PRIVATE src)
    target_compile_features(bvn_game_clock_qa PRIVATE cxx_std_20)
    target_compile_options(bvn_game_clock_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_game_clock_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/game_clock.map")
    target_link_libraries(bvn_game_clock_qa PRIVATE pspdisplay)
    create_pbp_file(TARGET bvn_game_clock_qa TITLE "BVN Game Clock QA" VERSION 00.51
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/game-clock")

    add_executable(bvn_map_optical_qa tests/map_optical_psp.cpp)
    target_compile_options(bvn_map_optical_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_map_optical_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/map_optical.map")
    target_link_libraries(bvn_map_optical_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_map_optical_qa TITLE "BVN Map Optical QA" VERSION 00.50
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/map-optical")

    add_executable(bvn_map_blur_qa tests/map_blur_psp.cpp)
    target_compile_options(bvn_map_blur_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_map_blur_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/map_blur.map")
    target_link_libraries(bvn_map_blur_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_map_blur_qa TITLE "BVN Map Blur QA" VERSION 00.49
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/map-blur")

    add_executable(bvn_ko_background_qa tests/ko_background_psp.cpp)
    target_compile_options(bvn_ko_background_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_ko_background_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/ko_background.map")
    target_link_libraries(bvn_ko_background_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_ko_background_qa TITLE "BVN KO Background QA" VERSION 00.48
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ko-background")

    add_executable(bvn_combat_floor_qa tests/combat_floor_psp.cpp)
    target_compile_options(bvn_combat_floor_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_floor_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_floor.map")
    target_link_libraries(bvn_combat_floor_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_combat_floor_qa TITLE "BVN Combat Floor QA" VERSION 00.47
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-floor")

    add_executable(bvn_combat_buff_qa tests/combat_buff_psp.cpp)
    target_compile_options(bvn_combat_buff_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_buff_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_buff.map")
    target_link_libraries(bvn_combat_buff_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_combat_buff_qa TITLE "BVN Combat Buff QA" VERSION 00.46
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-buff")

    add_executable(bvn_musou_combat_qa tests/musou_combat_psp.cpp)
    target_compile_options(bvn_musou_combat_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_musou_combat_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/musou_combat.map")
    target_link_libraries(bvn_musou_combat_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_musou_combat_qa TITLE "BVN Musou Combat QA" VERSION 00.45
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/musou-combat")

    add_executable(bvn_combat_status_qa tests/combat_status_psp.cpp)
    target_compile_options(bvn_combat_status_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_status_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_status.map")
    target_link_libraries(bvn_combat_status_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_combat_status_qa TITLE "BVN Combat Status QA" VERSION 00.44
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-status")

    add_executable(bvn_combat_playback_qa tests/combat_playback_psp.cpp)
    target_compile_options(bvn_combat_playback_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_playback_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_playback.map")
    target_link_libraries(bvn_combat_playback_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_combat_playback_qa TITLE "BVN Combat Playback QA" VERSION 00.43
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-playback")

    add_executable(bvn_combat_effects_qa tests/combat_effects_psp.cpp)
    target_compile_options(bvn_combat_effects_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_effects_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_effects.map")
    target_link_libraries(bvn_combat_effects_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_combat_effects_qa TITLE "BVN Combat Source QA" VERSION 00.44
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-effects")

    add_executable(bvn_shine_queue_qa tests/shine_queue_psp.cpp)
    target_compile_options(bvn_shine_queue_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_shine_queue_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/shine_queue.map")
    target_link_libraries(bvn_shine_queue_qa PRIVATE bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_shine_queue_qa TITLE "BVN Shine Queue QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shine-queue")

    add_executable(bvn_settings_qa tests/settings_psp.cpp)
    target_compile_options(bvn_settings_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_settings_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/settings.map")
    target_link_libraries(bvn_settings_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_settings_qa TITLE "BVN Settings QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/settings")

    add_executable(bvn_frame_effects_qa tests/frame_effects_psp.cpp)
    target_compile_options(bvn_frame_effects_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_frame_effects_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/frame_effects.map")
    target_link_libraries(bvn_frame_effects_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_frame_effects_qa TITLE "BVN Frame Effects QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/frame-effects")

    add_executable(bvn_ghost_controls_qa tests/ghost_controls_psp.cpp)
    target_compile_options(bvn_ghost_controls_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_ghost_controls_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/ghost_controls.map")
    target_link_libraries(bvn_ghost_controls_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_ghost_controls_qa TITLE "BVN Ghost Controls QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/ghost-controls")

    add_executable(bvn_counter_controls_qa tests/counter_controls_psp.cpp)
    target_compile_options(bvn_counter_controls_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_counter_controls_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/counter_controls.map")
    target_link_libraries(bvn_counter_controls_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_counter_controls_qa TITLE "BVN Counter Controls QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/counter-controls")

    add_executable(bvn_recovery_controls_qa tests/recovery_controls_psp.cpp)
    target_compile_options(bvn_recovery_controls_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_recovery_controls_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/recovery_controls.map")
    target_link_libraries(bvn_recovery_controls_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_recovery_controls_qa TITLE "BVN Recovery Controls QA" VERSION 00.36
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/recovery-controls")

    add_executable(bvn_recovery_assets_qa tests/recovery_assets_psp.cpp)
    target_compile_options(bvn_recovery_assets_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_recovery_assets_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/recovery_assets.map")
    target_link_libraries(bvn_recovery_assets_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_recovery_assets_qa TITLE "BVN Recovery Assets QA" VERSION 00.36
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/recovery-assets")

    add_executable(bvn_cpu_runtime_qa tests/cpu_runtime_psp.cpp)
    target_compile_options(bvn_cpu_runtime_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_cpu_runtime_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/cpu_runtime.map")
    target_link_libraries(bvn_cpu_runtime_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_cpu_runtime_qa TITLE "BVN Original Hit Query QA" VERSION 00.36
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cpu-runtime")

    target_sources(bvn_fighter PRIVATE src/texture_stream.cpp)
    target_link_libraries(bvn_fighter PUBLIC lz4)
else()
    find_path(BVN_LZ4_INCLUDE lz4.h REQUIRED)
    find_library(BVN_LZ4_LIBRARY lz4 REQUIRED)
    target_include_directories(bvn_fighter PRIVATE "${BVN_LZ4_INCLUDE}")
    target_link_libraries(bvn_fighter PUBLIC "${BVN_LZ4_LIBRARY}")
endif()

if(PSP)
    add_executable(bvn_cpu_policy_qa tests/cpu_policy_psp.cpp)
    target_compile_options(bvn_cpu_policy_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_cpu_policy_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/cpu_policy.map")
    target_link_libraries(bvn_cpu_policy_qa PRIVATE bvn_fighter pspctrl pspdebug pspge pspdisplay psppower)
    create_pbp_file(TARGET bvn_cpu_policy_qa TITLE "BVN Original CPU Policy QA" VERSION 00.36
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cpu-policy")

    add_library(bvn_renderer STATIC src/psp_stage_renderer.cpp src/psp_allocation_recovery.cpp src/ui_asset.cpp)
    add_executable(bvn_map_optical_render_qa tests/map_optical_render_psp.cpp)
    target_compile_options(bvn_map_optical_render_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_map_optical_render_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/map_optical_render.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_map_optical_render_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_map_optical_render_qa TITLE "BVN Map Optical GPU QA" VERSION 00.50
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/map-optical-render")
    add_executable(bvn_map_blur_render_qa tests/map_blur_render_psp.cpp)
    target_compile_options(bvn_map_blur_render_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_map_blur_render_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/map_blur_render.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_map_blur_render_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_map_blur_render_qa TITLE "BVN Map Blur GPU QA" VERSION 00.49
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/map-blur-render")
    target_compile_options(bvn_renderer PRIVATE -Wall -Wextra)
    target_link_libraries(bvn_renderer PUBLIC bvn_fighter pspgu pspge pspdisplay)
    if(BVN_RESOURCE_DIAGNOSTICS)
        target_link_libraries(bvn_renderer PUBLIC pspdebug)
    endif()

    add_executable(bvn_status_tint_qa tests/status_tint_psp.cpp)
    target_compile_options(bvn_status_tint_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_status_tint_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/status_tint.map")
    target_link_libraries(bvn_status_tint_qa PRIVATE bvn_renderer pspctrl psppower)
    create_pbp_file(TARGET bvn_status_tint_qa TITLE "BVN Actor Tint QA" VERSION 00.44
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/status-tint")

    add_executable(bvn_combat_blend_qa tests/combat_blend_psp.cpp)
    target_compile_options(bvn_combat_blend_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_combat_blend_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/combat_blend.map")
    target_link_libraries(bvn_combat_blend_qa PRIVATE bvn_renderer pspctrl psppower)
    create_pbp_file(TARGET bvn_combat_blend_qa TITLE "BVN Combat Blend QA" VERSION 00.43
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/combat-blend")

    add_executable(bvn_shine_render_qa tests/shine_render_psp.cpp)
    target_compile_options(bvn_shine_render_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_shine_render_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/shine_render.map")
    target_link_libraries(bvn_shine_render_qa PRIVATE bvn_renderer pspctrl psppower)
    create_pbp_file(TARGET bvn_shine_render_qa TITLE "BVN Shine GPU QA" VERSION 00.42
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shine-render")

    add_executable(bvn_stage_qa tests/stage_psp.cpp)
    target_compile_options(bvn_stage_qa PRIVATE -Wall -Wextra)
    target_link_options(bvn_stage_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/stage_qa.map")
    target_link_libraries(bvn_stage_qa PRIVATE bvn_renderer pspctrl pspdebug psppower)
    create_pbp_file(TARGET bvn_stage_qa TITLE "BVN 3.7 Stage QA" VERSION 00.01)

    add_executable(bvn_fighter_qa tests/fighter_psp.cpp)
    target_compile_options(bvn_fighter_qa PRIVATE -Wall -Wextra)
    target_link_options(bvn_fighter_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/fighter_qa.map")
    target_link_libraries(bvn_fighter_qa PRIVATE bvn_fighter bvn_renderer pspctrl pspdebug psppower)
    create_pbp_file(TARGET bvn_fighter_qa TITLE "BVN 3.7 Action QA" VERSION 00.07
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fighter")

    add_executable(bvn_transition_gpu_qa tests/transition_render_psp.cpp)
    target_compile_options(bvn_transition_gpu_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_transition_gpu_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/transition_gpu.map")
    target_link_libraries(bvn_transition_gpu_qa PRIVATE bvn_renderer bvn_fighter pspdisplay psppower)
    create_pbp_file(TARGET bvn_transition_gpu_qa TITLE "BVN Transition GPU QA" VERSION 00.57
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/transition-gpu")

    add_executable(bvn_versus src/psp_versus.cpp src/psp_audio.cpp src/wlan_link.cpp)
    target_compile_options(bvn_versus PRIVATE -Wall -Wextra)
    # Newlib also imports this function. Keep all sceUtility stubs together.
    target_link_options(bvn_versus PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/versus.map"
        "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_versus PRIVATE bvn_fighter bvn_renderer pspctrl pspdebug psppower pspaudio
        pspnet pspnet_adhoc pspnet_adhocctl pspwlan psputility)
    set(BVN_GAME_TITLE "Bleach vs Naruto 3.7 Alpha")
    create_pbp_file(TARGET bvn_versus TITLE "${BVN_GAME_TITLE}" VERSION 01.05
                    ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/artwork/xmb/ICON0.PNG"
                    BACKGROUND_PATH "${CMAKE_CURRENT_SOURCE_DIR}/artwork/xmb/PIC1.PNG"
                    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/versus")
    add_executable(bvn_live_render_qa src/psp_versus.cpp src/psp_audio.cpp src/wlan_link.cpp)
    target_compile_definitions(bvn_live_render_qa PRIVATE BVN_LIVE_RENDER_QA=1)
    target_compile_options(bvn_live_render_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_live_render_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/live_render.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_live_render_qa PRIVATE bvn_fighter bvn_renderer pspctrl pspdebug psppower pspaudio pspnet pspnet_adhoc pspnet_adhocctl pspwlan psputility)
    create_pbp_file(TARGET bvn_live_render_qa TITLE "BVN37 Live Render Check" VERSION 00.02 OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/live-render")
    add_executable(bvn_blend_layers_qa tests/blend_layers_psp.cpp)
    target_compile_options(bvn_blend_layers_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_blend_layers_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/blend_layers.map")
    target_link_libraries(bvn_blend_layers_qa PRIVATE bvn_renderer bvn_fighter pspctrl psppower)
    create_pbp_file(TARGET bvn_blend_layers_qa TITLE "BVN Authored Blend Audit" VERSION 00.01 OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/blend-layers")

endif()

if(PSP)
    add_executable(bvn_asset_coverage_qa tests/asset_coverage.cpp)
    target_compile_options(bvn_asset_coverage_qa PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_options(bvn_asset_coverage_qa PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/asset_coverage.map" "-Wl,-u,sceUtilityGetSystemParamInt")
    target_link_libraries(bvn_asset_coverage_qa PRIVATE bvn_renderer bvn_fighter psppower psputility)
    create_pbp_file(TARGET bvn_asset_coverage_qa TITLE "BVN Full Asset QA" VERSION 01.00 OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/asset-coverage")
else()
    add_executable(bvn_asset_coverage tests/asset_coverage.cpp)
    target_compile_options(bvn_asset_coverage PRIVATE -Wall -Wextra -ffp-contract=off)
    target_link_libraries(bvn_asset_coverage PRIVATE bvn_fighter)

endif()
