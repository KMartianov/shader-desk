cmake_minimum_required(VERSION 3.13)

# Этот файл собирает все плагины
set(PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/effects")
file(MAKE_DIRECTORY ${PLUGIN_OUTPUT_DIR})

# Собираем каждый плагин
add_subdirectory(ico-sphere-effect)