# The desktop simulator: the real shell and apps in a 1280x720 SDL window.
add_executable(meshdeck-sim main.cpp)
target_link_libraries(meshdeck-sim PRIVATE meshdeck_ui meshdeck_fake)
if(WIN32)
    # Keep a console for the log, and put SDL2.dll next to the program so it runs from anywhere.
    target_link_options(meshdeck-sim PRIVATE -mconsole)
    add_custom_command(TARGET meshdeck-sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:meshdeck-sim>)
endif()
