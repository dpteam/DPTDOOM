cmake_minimum_required(VERSION 3.21)
project(DPTDOOM VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
    set(default_backend win32)
elseif(APPLE)
    set(default_backend sdl)
else()
    set(default_backend x11)
endif()
set(DPTDOOM_BACKEND "${default_backend}" CACHE STRING "Platform backend: win32, x11, sdl, headless")
set_property(CACHE DPTDOOM_BACKEND PROPERTY STRINGS win32 x11 sdl headless)
option(DPTDOOM_LTO "Enable link-time optimization for optimized builds" ON)
option(DPTDOOM_SOUND "Enable sound/music (SDL backend only)" OFF)

# Explicit list: other platform files and future tools must not enter the engine.
set(engine_sources
    dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c
    d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c
    g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c
    i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c
    m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c
    p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c
    p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c
    r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c
    sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c
    wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c z_zone.c
    w_file_stdc.c i_input.c i_video.c doomgeneric.c
)
list(TRANSFORM engine_sources PREPEND "doomgeneric/")
add_executable(dptdoom ${engine_sources})
target_include_directories(dptdoom PRIVATE doomgeneric)

if(DPTDOOM_BACKEND STREQUAL "win32")
    if(NOT WIN32)
        message(FATAL_ERROR "The win32 backend requires a Windows target")
    endif()
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_win.c)
    target_link_libraries(dptdoom PRIVATE user32 gdi32)
elseif(DPTDOOM_BACKEND STREQUAL "x11")
    find_package(X11 REQUIRED)
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_xlib.c)
    target_link_libraries(dptdoom PRIVATE X11::X11)
elseif(DPTDOOM_BACKEND STREQUAL "sdl")
    find_package(SDL2 CONFIG REQUIRED)
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_sdl.c)
    target_compile_definitions(dptdoom PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(dptdoom PRIVATE SDL2::SDL2)
elseif(DPTDOOM_BACKEND STREQUAL "headless")
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_headless.c)
    set_target_properties(dptdoom PROPERTIES OUTPUT_NAME dptdoom-headless)
else()
    message(FATAL_ERROR "Unknown DPTDOOM_BACKEND: ${DPTDOOM_BACKEND}")
endif()

if(WIN32)
    enable_language(RC)
    target_sources(dptdoom PRIVATE resources/dptdoom.rc)
    target_include_directories(dptdoom PRIVATE resources)
else()
    target_link_libraries(dptdoom PRIVATE m)
    target_compile_definitions(dptdoom PRIVATE _DEFAULT_SOURCE)
endif()

if(DPTDOOM_SOUND)
    if(NOT DPTDOOM_BACKEND STREQUAL "sdl")
        message(FATAL_ERROR "Sound requires -DDPTDOOM_BACKEND=sdl")
    endif()
    find_package(SDL2_mixer CONFIG REQUIRED)
    target_compile_definitions(dptdoom PRIVATE FEATURE_SOUND)
    target_sources(dptdoom PRIVATE doomgeneric/mus2mid.c
        doomgeneric/i_sdlmusic.c doomgeneric/i_sdlsound.c)
    target_link_libraries(dptdoom PRIVATE SDL2_mixer::SDL2_mixer)
endif()

if(MSVC)
    target_compile_definitions(dptdoom PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(dptdoom PRIVATE /W3 /wd4146
        "$<$<NOT:$<CONFIG:Debug>>:/Gy;/Gw>")
    target_link_options(dptdoom PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>")
else()
    # Vanilla Doom relies on integer wraparound and legacy aliasing patterns.
    # Never enable fast-math: demo sync and future netgame determinism matter.
    target_compile_options(dptdoom PRIVATE -Wall -fwrapv -fno-strict-aliasing
        -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(dptdoom PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(dptdoom PRIVATE -Wl,--gc-sections)
    endif()
endif()

if(DPTDOOM_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES C)
    if(ipo_supported)
        set_target_properties(dptdoom PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
            INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(WARNING "LTO unavailable: ${ipo_error}")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME version COMMAND dptdoom --version)
    set_tests_properties(version PROPERTIES
        PASS_REGULAR_EXPRESSION "DPT\\.DOOM 0\\.1\\.0-dev" TIMEOUT 10)
    set(DPTDOOM_TEST_IWAD "" CACHE FILEPATH "Optional local IWAD for engine smoke tests")
    set(DPTDOOM_TEST_GAMEVERSION "" CACHE STRING "Optional Doom gameversion for the test IWAD")
    if(WIN32 AND DPTDOOM_BACKEND STREQUAL "win32" AND DPTDOOM_TEST_IWAD)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(smoke_args --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
        if(DPTDOOM_TEST_GAMEVERSION)
            list(APPEND smoke_args --gameversion "${DPTDOOM_TEST_GAMEVERSION}")
        endif()
        add_test(NAME windows-smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows-smoke.py" ${smoke_args})
        set_tests_properties(windows-smoke PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
    if(DPTDOOM_BACKEND STREQUAL "headless" AND DPTDOOM_TEST_IWAD)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(smoke_args --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
        if(DPTDOOM_TEST_GAMEVERSION)
            list(APPEND smoke_args --gameversion "${DPTDOOM_TEST_GAMEVERSION}")
        endif()
        add_test(NAME headless-smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless-smoke.py" ${smoke_args})
        set_tests_properties(headless-smoke PROPERTIES TIMEOUT 120)
    endif()
endif()
install(TARGETS dptdoom RUNTIME DESTINATION bin)
