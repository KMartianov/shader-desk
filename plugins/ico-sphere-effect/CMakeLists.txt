cmake_minimum_required(VERSION 3.13)


set(PLUGIN_NAME "ico-sphere-effect")

project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

# --- Системные зависимости ---
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# --- ПОДКЛЮЧЕНИЕ СИСТЕМНОГО SDK ЯДРА ---
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Сборка библиотеки ---
set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "") # Убирает "lib" из имени (libname.so -> name.so)

# Позволяет плагину инклудить локальные файлы (например, stb_truetype.h в text-3d-effect)
target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Линковка (Критически важный шаг) ---
target_link_libraries(${PLUGIN_NAME} PRIVATE
    ShaderDesk::shader_desk_sdk    # Дает доступ к <shader-desk/wallpaper-effect.hpp>
    ShaderDesk::shader_utils       # Дает доступ к утилитам загрузки шейдеров
    glm::glm
    ${GLESv2_LIBRARIES}
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
