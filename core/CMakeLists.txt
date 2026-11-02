# core: everything that is the same on the Tab5, the simulator and in tests.
# Includes are written relative to core/, e.g. #include "kernel/journal.h".

# The kernel, services and file formats: plain C++, no LVGL.
add_library(meshdeck_core STATIC
    kernel/clock.cpp
    kernel/event_bus.cpp
    kernel/journal.cpp
    kernel/settings.cpp
    kernel/sha256.cpp
    kernel/storage.cpp
    services/contacts.cpp
    services/gazetteer.cpp
    services/inventory.cpp
    services/mesh.cpp
    services/position.cpp
    services/power_monitor.cpp
    services/security.cpp
    services/terminal.cpp
    games/pong.cpp
    games/sea_battle.cpp
    games/klondike.cpp
    services/high_scores.cpp
    formats/map_pack.cpp
    formats/osgrid.cpp
    formats/png_writer.cpp
    formats/report.cpp
)
target_include_directories(meshdeck_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The user interface and the apps, drawn with LVGL.
add_library(meshdeck_ui STATIC
    ui/boot_splash.cpp
    ui/clock_setter.cpp
    ui/drip_rain.cpp
    ui/credits/credits_logo.cpp
    ui/credits/credits_text.cpp
    ui/fonts.cpp
    ui/fonts/vt323_ttf.cpp
    ui/lock_screen.cpp
    ui/pin_pad.cpp
    ui/screenshot.cpp
    ui/shell.cpp
    ui/theme.cpp
    ui/widgets.cpp
    apps/apps.cpp
    apps/diagnostics_app.cpp
    apps/files_app.cpp
    formats/book_pack.cpp
    formats/epub_import.cpp
    formats/image_decode.cpp
    formats/zip_archive.cpp
    apps/library_app.cpp
    apps/logbook_app.cpp
    apps/map_app.cpp
    apps/messages_app.cpp
    apps/placeholder_app.cpp
    apps/security_app.cpp
    apps/settings_app.cpp
    apps/setup_wizard.cpp
    apps/terminal_app.cpp
    apps/games_app.cpp
    apps/tools_app.cpp
)
target_link_libraries(meshdeck_ui PUBLIC meshdeck_core lvgl)
