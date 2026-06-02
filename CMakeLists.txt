cmake_minimum_required(VERSION 4.2.2)
project(LOL_overlay)

set(CMAKE_CXX_STANDARD 20)

set(COMMON_SOURCES
        src/app_paths.cpp
        src/app_settings.cpp
        src/api/api_caller.cpp
        src/api/json_parser.cpp
        src/item_catalog.cpp
        src/champion_catalog.cpp
        src/lcu_client.cpp
        src/champ_select.cpp
        src/performance_scorer.cpp
        src/performance_tracker.cpp
)

set(COMMON_HEADERS
        include/app_paths.h
        include/app_settings.h
        include/api/api_caller.h
        include/api/json_parser.h
        include/item_catalog.h
        include/champion_catalog.h
        include/lcu_client.h
        include/champ_select.h
        include/performance_scorer.h
        include/performance_tracker.h
)

add_executable(LOL_overlay
        src/main.cpp
        src/game_tui.cpp
        include/game_tui.h
        ${COMMON_SOURCES}
        ${COMMON_HEADERS}
)

add_executable(LOL_overlay_gui WIN32
        src/overlay_main.cpp
        src/overlay_data.cpp
        src/match_history.cpp
        include/overlay_data.h
        include/match_history.h
        ${COMMON_SOURCES}
        ${COMMON_HEADERS}
)

target_include_directories(LOL_overlay PRIVATE include)
target_include_directories(LOL_overlay_gui PRIVATE include)

find_package(CURL CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
target_link_libraries(LOL_overlay PRIVATE CURL::libcurl jsoncpp_lib advapi32)
target_link_libraries(LOL_overlay_gui PRIVATE CURL::libcurl jsoncpp_lib user32 gdi32 advapi32)

if (WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLLS
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll
    )

    function(copy_mingw_runtime target_name)
        foreach (runtime_dll IN LISTS MINGW_RUNTIME_DLLS)
            add_custom_command(TARGET ${target_name} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${MINGW_BIN_DIR}/${runtime_dll}"
                    "$<TARGET_FILE_DIR:${target_name}>/${runtime_dll}"
            )
        endforeach ()
    endfunction()

    copy_mingw_runtime(LOL_overlay)
    copy_mingw_runtime(LOL_overlay_gui)

    foreach (runtime_dll IN LISTS MINGW_RUNTIME_DLLS)
        install(FILES "${MINGW_BIN_DIR}/${runtime_dll}" DESTINATION .)
    endforeach ()
endif ()

install(TARGETS LOL_overlay RUNTIME DESTINATION .)
install(TARGETS LOL_overlay_gui RUNTIME DESTINATION .)
