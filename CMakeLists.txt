cmake_minimum_required (VERSION 3.2.2)
project (detect_hid_unplugging)
add_executable(detect_hid_unplugging WIN32 main.cpp)
target_link_libraries(detect_hid_unplugging SetupAPI.lib)
