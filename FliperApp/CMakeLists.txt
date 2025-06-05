cmake_minimum_required(VERSION 3.13)
project(scan_app C)

add_library(scan_app STATIC scan_app.c)

# Link with flipper libraries (placeholder, depends on SDK)
target_link_libraries(scan_app furi)
