cmake_minimum_required(VERSION 3.13)

# ==============================================================================
# 1. УКАЖИ ИМЯ ПРОВАЙДЕРА (Должно совпадать с именем .cpp файла!)
# ==============================================================================
set(PLUGIN_NAME "pointer-provider")

project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

# --- ПОДКЛЮЧЕНИЕ СИСТЕМНОГО SDK ЯДРА ---
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Сборка библиотеки ---
add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES 
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Линковка ---
target_link_libraries(${PLUGIN_NAME} PRIVATE
    ShaderDesk::shader_desk_sdk    # Дает доступ к <shader-desk/data-provider.hpp>
)