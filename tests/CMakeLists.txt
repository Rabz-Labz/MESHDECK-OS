# Unit tests (doctest). One executable; CTest runs it and reports each case.
add_executable(meshdeck_tests
    test_main.cpp
    test_clock.cpp
    test_event_bus.cpp
    test_books.cpp
    test_epub.cpp
    test_drip_rain.cpp
    test_games.cpp
    test_images.cpp
    test_journal.cpp
    test_logbook.cpp
    test_map.cpp
    test_mesh.cpp
    test_png_writer.cpp
    test_power_monitor.cpp
    test_security.cpp
    test_settings.cpp
    test_shell.cpp
    test_storage.cpp
    test_terminal.cpp
)
target_link_libraries(meshdeck_tests PRIVATE meshdeck_ui meshdeck_fake)
target_include_directories(meshdeck_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME meshdeck_tests COMMAND meshdeck_tests)
