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
option(DPTDOOM_NETWORK "Build experimental direct two-player coop" ON)
option(DPTDOOM_MASTER "Build experimental native session directory" ON)
option(DPTDOOM_LAUNCHER "Build the Windows HTML playtest launcher (MSVC)" ${MSVC})
set(DPTDOOM_WEBVIEW2_SDK "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/webview2" CACHE PATH "WebView2 NuGet SDK (tools/fetch-webview2.ps1)")
option(DPTDOOM_STATIC_CRT "Link the MSVC runtime statically for portable test bundles" OFF)
if(MSVC AND DPTDOOM_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(DPTDOOM_SANITIZER "none" CACHE STRING "Diagnostic sanitizer: none, address, undefined")
set_property(CACHE DPTDOOM_SANITIZER PROPERTY STRINGS none address undefined)

if(NOT DPTDOOM_SANITIZER STREQUAL "none")
    if(NOT DPTDOOM_SANITIZER MATCHES "^(address|undefined)$")
        message(FATAL_ERROR "Unknown DPTDOOM_SANITIZER: ${DPTDOOM_SANITIZER}")
    endif()
    if(DPTDOOM_LTO)
        message(STATUS "Sanitizer build: disabling LTO")
        set(DPTDOOM_LTO OFF)
    endif()
    if(MSVC)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm")
            message(FATAL_ERROR "This MSVC sanitizer configuration currently supports x64 only")
        endif()
        if(NOT DPTDOOM_SANITIZER STREQUAL "address")
            message(FATAL_ERROR "MSVC supports address sanitizer only; use Clang/GCC for undefined")
        endif()
        # /RTC and incremental linking are incompatible with MSVC ASan.
        string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO /DEBUG)
    else()
        include(CheckCSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-fsanitize=${DPTDOOM_SANITIZER}")
        unset(sanitizer_supported CACHE)
        check_c_source_compiles("int main(void) { return 0; }" sanitizer_supported)
        unset(CMAKE_REQUIRED_FLAGS)
        if(NOT sanitizer_supported)
            message(FATAL_ERROR "Compiler/runtime does not support ${DPTDOOM_SANITIZER} sanitizer")
        endif()
        add_compile_options(-fsanitize=${DPTDOOM_SANITIZER} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${DPTDOOM_SANITIZER})
    endif()
    if(DPTDOOM_SANITIZER STREQUAL "address")
        add_compile_definitions(DPTDOOM_ASAN)
    endif()
endif()

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
if(DPTDOOM_NETWORK)
    # Fingerprint all shipped engine code/headers, not a stale dev version
    # string or absolute build path. Different local patches must not connect.
    file(GLOB net_fingerprint_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/doomgeneric/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/doomgeneric/*.h")
    list(SORT net_fingerprint_files)
    set(net_fingerprint "")
    foreach(source IN LISTS net_fingerprint_files)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(READ "${source}" source_text)
        string(REPLACE "\r\n" "\n" source_text "${source_text}")
        string(SHA256 source_hash "${source_text}")
        string(APPEND net_fingerprint "${source_hash}")
    endforeach()
    string(SHA256 net_build_id "${net_fingerprint}")
    target_sources(dptdoom PRIVATE doomgeneric/dpt_net.c)
    target_compile_definitions(dptdoom PRIVATE DPTDOOM_NETWORK DPT_NET_BUILD_ID="${net_build_id}")
    if(WIN32)
        target_link_libraries(dptdoom PRIVATE ws2_32)
    endif()
endif()
if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
    get_filename_component(compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    find_file(asan_runtime NAMES clang_rt.asan_dynamic-x86_64.dll
        HINTS "${compiler_dir}" NO_DEFAULT_PATH REQUIRED)
    add_custom_command(TARGET dptdoom POST_BUILD COMMAND ${CMAKE_COMMAND} -E
        copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:dptdoom>)
endif()

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
    find_package(SDL2 2.0.7 CONFIG REQUIRED)
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_sdl.c)
    target_compile_definitions(dptdoom PRIVATE SDL_MAIN_HANDLED DPTDOOM_SDL)
    target_link_libraries(dptdoom PRIVATE SDL2::SDL2)
    if(WIN32 AND TARGET SDL2::SDL2)
        add_custom_command(TARGET dptdoom POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:dptdoom>)
    endif()
elseif(DPTDOOM_BACKEND STREQUAL "headless")
    target_sources(dptdoom PRIVATE doomgeneric/doomgeneric_headless.c doomgeneric/d_diagnostics.c)
    target_compile_definitions(dptdoom PRIVATE DPTDOOM_DIAGNOSTICS)
    set_target_properties(dptdoom PROPERTIES OUTPUT_NAME dptdoom-headless)
else()
    message(FATAL_ERROR "Unknown DPTDOOM_BACKEND: ${DPTDOOM_BACKEND}")
endif()

if(WIN32)
    enable_language(RC)
    if(NOT DPTDOOM_BACKEND STREQUAL "headless")
        target_sources(dptdoom PRIVATE "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/doomgeneric/dpt_debug_console.c>")
        target_compile_definitions(dptdoom PRIVATE "$<$<CONFIG:Debug>:DPTDOOM_DEBUG_CONSOLE>")
    endif()
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
    find_package(SDL2_mixer 2.6 CONFIG REQUIRED)
    target_compile_definitions(dptdoom PRIVATE FEATURE_SOUND)
    target_sources(dptdoom PRIVATE doomgeneric/mus2mid.c
        doomgeneric/i_sdlmusic.c doomgeneric/i_sdlsound.c)
    target_link_libraries(dptdoom PRIVATE SDL2_mixer::SDL2_mixer)
    if(WIN32)
        add_custom_command(TARGET dptdoom POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different $<TARGET_FILE:SDL2_mixer::SDL2_mixer> $<TARGET_FILE_DIR:dptdoom>)
    endif()
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
if(DPTDOOM_MASTER)
    add_executable(dptdoom-master master/main.c master/directory.c master/log.c)
    target_include_directories(dptdoom-master PRIVATE master)
    if(WIN32)
        target_link_libraries(dptdoom-master PRIVATE ws2_32 bcrypt)
    endif()
    if(MSVC)
        target_compile_options(dptdoom-master PRIVATE /W4)
        target_compile_definitions(dptdoom-master PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(dptdoom-master PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
        add_custom_command(TARGET dptdoom-master POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:dptdoom-master>)
    endif()
    install(TARGETS dptdoom-master RUNTIME DESTINATION bin)
endif()
if(WIN32 AND DPTDOOM_LAUNCHER AND NOT DPTDOOM_BACKEND STREQUAL "headless")
    # The engine/model stay C99. Only the WebView2 COM host uses C++/WRL.
    enable_language(CXX)
    if(NOT MSVC)
        message(FATAL_ERROR "HTML launcher currently needs MSVC/WRL; use -DDPTDOOM_LAUNCHER=OFF for MinGW engine builds")
    endif()
    if(NOT EXISTS "${DPTDOOM_WEBVIEW2_SDK}/build/native/include/WebView2.h")
        message(FATAL_ERROR "Run powershell -NoProfile -File tools/fetch-webview2.ps1 or set DPTDOOM_WEBVIEW2_SDK; engine-only: DPTDOOM_LAUNCHER=OFF")
    endif()
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
        set(webview_arch arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(webview_arch x64)
    else()
        set(webview_arch x86)
    endif()
    set(webview_loader "${DPTDOOM_WEBVIEW2_SDK}/build/native/${webview_arch}/WebView2Loader.dll")
    add_executable(dptdoom-launcher WIN32 launcher/launcher_win.cpp launcher/launch_model.c launcher/storage.c resources/launcher.rc)
    set_target_properties(dptdoom-launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(dptdoom-launcher PRIVATE resources launcher "${DPTDOOM_WEBVIEW2_SDK}/build/native/include")
    target_compile_definitions(dptdoom-launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(dptdoom-launcher PRIVATE /utf-8 /W4)
    target_link_libraries(dptdoom-launcher PRIVATE user32 gdi32 shell32 shlwapi comdlg32 ole32)
    add_custom_command(TARGET dptdoom-launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E
        copy_if_different "${webview_loader}" $<TARGET_FILE_DIR:dptdoom-launcher>)
    if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
        add_custom_command(TARGET dptdoom-launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:dptdoom-launcher>)
    endif()
    install(TARGETS dptdoom-launcher RUNTIME DESTINATION bin)
    install(FILES "${webview_loader}" DESTINATION bin)
    install(FILES "${DPTDOOM_WEBVIEW2_SDK}/LICENSE.txt" DESTINATION share/dptdoom RENAME WebView2-LICENSE.txt)
    install(FILES "${DPTDOOM_WEBVIEW2_SDK}/NOTICE.txt" DESTINATION share/dptdoom RENAME WebView2-NOTICE.txt)
endif()
if(BUILD_TESTING)
    if(DPTDOOM_MASTER)
        add_executable(master-log-test tests/master-log.c master/log.c)
        target_include_directories(master-log-test PRIVATE master)
        target_compile_definitions(master-log-test PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
        add_executable(master-model-test tests/master-model.c master/directory.c)
        target_include_directories(master-model-test PRIVATE master)
        add_test(NAME master-model COMMAND master-model-test)
        if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
            add_custom_command(TARGET master-log-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:master-log-test>)
            add_custom_command(TARGET master-model-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:master-model-test>)
        endif()
    endif()
    if(WIN32)
        add_executable(debug-console-test tests/debug-console.c doomgeneric/dpt_debug_console.c)
        target_include_directories(debug-console-test PRIVATE doomgeneric)
        target_compile_definitions(debug-console-test PRIVATE DPTDOOM_DEBUG_CONSOLE _CRT_SECURE_NO_WARNINGS)
        add_executable(release-console-test tests/debug-console.c)
        target_include_directories(release-console-test PRIVATE doomgeneric)
        if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
            foreach(probe IN ITEMS debug-console-test release-console-test)
                add_custom_command(TARGET ${probe} POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                    copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:${probe}>)
            endforeach()
        endif()
        add_executable(launcher-model-test tests/launcher-model.c launcher/launch_model.c launcher/storage.c)
        target_include_directories(launcher-model-test PRIVATE launcher)
        target_compile_definitions(launcher-model-test PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(launcher-model-test PRIVATE shell32)
        if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
            add_custom_command(TARGET launcher-model-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:launcher-model-test>)
        endif()
    endif()
    add_executable(match-rules-test tests/match-rules.c)
    target_include_directories(match-rules-test PRIVATE doomgeneric)
    add_test(NAME match-rules COMMAND match-rules-test)
    if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
        add_custom_command(TARGET match-rules-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:match-rules-test>)
    endif()
    if(DPTDOOM_NETWORK)
        add_executable(net-protocol-test tests/net-protocol.c)
        target_include_directories(net-protocol-test PRIVATE doomgeneric)
        add_test(NAME net-protocol COMMAND net-protocol-test)
        if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
            add_custom_command(TARGET net-protocol-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:net-protocol-test>)
        endif()
    endif()
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        if(TARGET master-log-test)
            add_test(NAME master-log COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-isolated.py" $<TARGET_FILE:master-log-test>)
        endif()
        if(TARGET debug-console-test)
            add_test(NAME debug-console COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/debug-console.py" $<TARGET_FILE:debug-console-test> debug)
            add_test(NAME release-console COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/debug-console.py" $<TARGET_FILE:release-console-test> release)
            set_tests_properties(debug-console PROPERTIES LABELS interactive RUN_SERIAL TRUE TIMEOUT 45)
            set_tests_properties(release-console PROPERTIES TIMEOUT 45)
        endif()
        if(TARGET dptdoom-master)
            add_test(NAME master-loopback COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/master-loopback.py" $<TARGET_FILE:dptdoom-master>)
            set_tests_properties(master-loopback PROPERTIES TIMEOUT 40)
        endif()
        if(TARGET launcher-model-test)
            add_test(NAME launcher-model COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-isolated.py" $<TARGET_FILE:launcher-model-test>)
            set_tests_properties(launcher-model PROPERTIES TIMEOUT 15)
        endif()
        find_package(Git QUIET)
        if(GIT_FOUND)
            add_test(NAME packaging COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/packaging.py")
            set_tests_properties(packaging PROPERTIES TIMEOUT 30)
        endif()
        add_test(NAME startup-smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup-smoke.py"
            $<TARGET_FILE:dptdoom> "${DPTDOOM_BACKEND}")
        set_tests_properties(startup-smoke PROPERTIES TIMEOUT 30)
    endif()
    add_executable(timer-test tests/timer.c doomgeneric/i_timer.c)
    target_include_directories(timer-test PRIVATE doomgeneric)
    add_test(NAME timer COMMAND timer-test)
    add_test(NAME timer-wrap COMMAND timer-test wrap)
    add_executable(zone-memory-test tests/zone-memory.c doomgeneric/z_zone.c)
    target_include_directories(zone-memory-test PRIVATE doomgeneric)
    add_test(NAME zone-memory COMMAND zone-memory-test)
    if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
        add_custom_command(TARGET timer-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:timer-test>)
        add_custom_command(TARGET zone-memory-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
            copy_if_different "${asan_runtime}" $<TARGET_FILE_DIR:zone-memory-test>)
    endif()
    if(DPTDOOM_SANITIZER STREQUAL "address")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(probe IN ITEMS uaf overflow)
            add_test(NAME asan-zone-${probe} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/sanitizer-probe.py"
                $<TARGET_FILE:zone-memory-test> ${probe})
            set_tests_properties(asan-zone-${probe} PROPERTIES TIMEOUT 30)
        endforeach()
    endif()
    add_test(NAME version COMMAND dptdoom --version)
    set_tests_properties(version PROPERTIES
        PASS_REGULAR_EXPRESSION "DPT\\.DOOM 0\\.1\\.0-dev" TIMEOUT 10)
    set(DPTDOOM_TEST_IWAD "" CACHE FILEPATH "Optional local IWAD for engine smoke tests")
    set(DPTDOOM_TEST_GAMEVERSION "" CACHE STRING "Optional Doom gameversion for the test IWAD")
    if(WIN32 AND NOT DPTDOOM_BACKEND STREQUAL "headless" AND DPTDOOM_TEST_IWAD)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        if(TARGET dptdoom-launcher)
            add_test(NAME launcher-windows COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher-windows.py"
                --exe $<TARGET_FILE:dptdoom-launcher> --engine $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(launcher-windows PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE LABELS interactive)
        endif()
        set(smoke_args --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
        if(DPTDOOM_TEST_GAMEVERSION)
            list(APPEND smoke_args --gameversion "${DPTDOOM_TEST_GAMEVERSION}")
        endif()
        add_test(NAME windows-smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows-smoke.py" ${smoke_args})
        set_tests_properties(windows-smoke PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS interactive)
        if(DPTDOOM_BACKEND STREQUAL "sdl")
            add_test(NAME demo-snapshot COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/demo-snapshot.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(demo-snapshot PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS interactive)
            if(DPTDOOM_NETWORK)
                add_test(NAME net-windows COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-windows.py"
                    --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
                set_tests_properties(net-windows PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS interactive)
                add_test(NAME net-windows-local-end COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-windows.py"
                    --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}" --dmflags 3 --local-end --fast)
                set_tests_properties(net-windows-local-end PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS interactive)
                add_test(NAME net-windows-guest-end COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-windows.py"
                    --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}" --dmflags 3 --local-end --leaver 1)
                set_tests_properties(net-windows-guest-end PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS interactive)
            endif()
        endif()
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
        add_test(NAME state-smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/state-smoke.py"
            --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
        set_tests_properties(state-smoke PROPERTIES TIMEOUT 180)
        if(DPTDOOM_NETWORK)
            add_test(NAME net-loopback COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-loopback.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(net-loopback PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
            add_test(NAME net-errors COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-errors.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(net-errors PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
            add_test(NAME net-recovery COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-errors.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}" --recover)
            set_tests_properties(net-recovery PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
            add_test(NAME net-survivor COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-survivor.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(net-survivor PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
            add_test(NAME net-rules COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-rules.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
            set_tests_properties(net-rules PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
            add_test(NAME net-loopback-dmflags COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-loopback.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}" --dmflags 3)
            set_tests_properties(net-loopback-dmflags PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
            add_test(NAME net-chat COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/net-loopback.py"
                --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}" --chat)
            set_tests_properties(net-chat PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        endif()
    endif()
    if(DPTDOOM_BACKEND STREQUAL "sdl")
        add_executable(sdl-input-test tests/sdl-input.c doomgeneric/doomgeneric_sdl.c
            doomgeneric/i_input.c doomgeneric/d_event.c)
        target_include_directories(sdl-input-test PRIVATE doomgeneric)
        target_compile_definitions(sdl-input-test PRIVATE SDL_MAIN_HANDLED DPTDOOM_BACKEND_TEST)
        target_link_libraries(sdl-input-test PRIVATE SDL2::SDL2)
        if(WIN32)
            add_custom_command(TARGET sdl-input-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E
                copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:sdl-input-test>)
        endif()
        if(WIN32)
            target_link_libraries(sdl-input-test PRIVATE user32)
        endif()
        add_test(NAME sdl-input COMMAND sdl-input-test)
        set_tests_properties(sdl-input PROPERTIES ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 10)
        if(DPTDOOM_SOUND)
            add_executable(sdl-audio-test tests/sdl-audio.c doomgeneric/i_sound.c
                doomgeneric/i_sdlsound.c doomgeneric/i_sdlmusic.c
                doomgeneric/mus2mid.c doomgeneric/memio.c doomgeneric/sha1.c
                doomgeneric/m_misc.c doomgeneric/m_argv.c)
            target_include_directories(sdl-audio-test PRIVATE doomgeneric)
            target_compile_definitions(sdl-audio-test PRIVATE SDL_MAIN_HANDLED FEATURE_SOUND DPTDOOM_SDL
                $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
            target_link_libraries(sdl-audio-test PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer)
            if(WIN32)
                add_custom_command(TARGET sdl-audio-test POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:sdl-audio-test>
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2_mixer::SDL2_mixer> $<TARGET_FILE_DIR:sdl-audio-test>)
            endif()
            if(NOT WIN32)
                target_link_libraries(sdl-audio-test PRIVATE m)
            endif()
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            foreach(mode IN ITEMS normal nosound nosfx nomusic bad-driver)
                add_test(NAME sdl-audio-${mode} COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-isolated.py"
                    $<TARGET_FILE:sdl-audio-test> -${mode})
                set_tests_properties(sdl-audio-${mode} PROPERTIES
                    ENVIRONMENT "SDL_AUDIODRIVER=dummy" TIMEOUT 15)
            endforeach()
            if(DPTDOOM_TEST_IWAD)
                add_test(NAME sdl-engine COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdl-smoke.py"
                    --exe $<TARGET_FILE:dptdoom> --iwad "${DPTDOOM_TEST_IWAD}")
                set_tests_properties(sdl-engine PROPERTIES TIMEOUT 120)
            endif()
        endif()
    endif()
endif()
install(TARGETS dptdoom RUNTIME DESTINATION bin)
if(MSVC AND DPTDOOM_SANITIZER STREQUAL "address")
    install(FILES "${asan_runtime}" DESTINATION bin)
endif()
if(WIN32 AND DPTDOOM_BACKEND STREQUAL "sdl")
    install(FILES $<TARGET_FILE:SDL2::SDL2> DESTINATION bin)
    if(DPTDOOM_SOUND)
        install(FILES $<TARGET_FILE:SDL2_mixer::SDL2_mixer> DESTINATION bin)
    endif()
endif()
install(FILES LICENSE docs/SDL-LICENSES.txt DESTINATION share/dptdoom)
