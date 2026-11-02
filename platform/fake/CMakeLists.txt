# Pretend hardware shared by the simulator and the tests.
add_library(meshdeck_fake STATIC fake_radio.cpp)
target_include_directories(meshdeck_fake PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meshdeck_fake PUBLIC meshdeck_core)
