cmake_minimum_required(VERSION 3.13)

# ==============================================================================
# 1. УКАЖИ ИМЯ ПРОВАЙДЕРА (Должно совпадать с именем .cpp файла!)
# ==============================================================================
set(PLUGIN_NAME "audio-provider")

project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

# --- ПОДКЛЮЧЕНИЕ СИСТЕМНОГО SDK ЯДРА ---
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Сборка библиотеки ---
set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "")

target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Линковка ---
target_link_libraries(${PLUGIN_NAME} PRIVATE
    ShaderDesk::shader_desk_sdk    # Дает доступ к <shader-desk/data-provider.hpp>
)

# --- Автоматическое копирование ресурсов в папку сборки ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${PLUGIN_BUILD_DIR}/shaders"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/presets")
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/presets"
                "${PLUGIN_BUILD_DIR}/presets"
    )
endif()
