cmake_minimum_required(VERSION 4.2.2)
project(LOL_overlay)

set(CMAKE_CXX_STANDARD 20)

add_executable(LOL_overlay main.cpp
        app_paths.cpp
        app_paths.h
        api/api_caller.cpp
        api/api_caller.h
        api/json_parser.cpp
        api/json_parser.h
        item_catalog.cpp
        item_catalog.h
        champion_catalog.cpp
        champion_catalog.h
        lcu_client.cpp
        lcu_client.h
        champ_select.cpp
        champ_select.h
        performance_scorer.cpp
        performance_scorer.h
        performance_tracker.cpp
        performance_tracker.h
        game_tui.cpp
        game_tui.h
)

add_executable(LOL_overlay_gui WIN32
        overlay_main.cpp
        app_paths.cpp
        app_paths.h
        app_settings.cpp
        app_settings.h
        overlay_data.cpp
        overlay_data.h
        api/api_caller.cpp
        api/api_caller.h
        api/json_parser.cpp
        api/json_parser.h
        item_catalog.cpp
        item_catalog.h
        champion_catalog.cpp
        champion_catalog.h
        lcu_client.cpp
        lcu_client.h
        champ_select.cpp
        champ_select.h
        performance_scorer.cpp
        performance_scorer.h
        performance_tracker.cpp
        performance_tracker.h
        match_history.cpp
        match_history.h
)

find_package(CURL CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
target_link_libraries(LOL_overlay PRIVATE CURL::libcurl)
target_link_libraries(LOL_overlay PRIVATE jsoncpp_lib)
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
