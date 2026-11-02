# LVGL 9.2.2, unmodified (third_party/lvgl/src), configured by config/lv_conf.h.
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lvgl/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lvgl ${PROJECT_SOURCE_DIR}/config)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w)   # third-party code: its warnings aren't ours to fix

# With SDL2 available, LVGL also gets its SDL window driver for the simulator.
if(MESHDECK_CR_HAVE_SDL)
    target_compile_definitions(lvgl PUBLIC MESHDECK_CR_SDL SDL_MAIN_HANDLED)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
endif()
