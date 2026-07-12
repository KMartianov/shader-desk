cmake_minimum_required(VERSION 3.13)


set(PLUGIN_NAME "ico-sphere-effect")

project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

# --- System dependencies ---
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# --- CORE SYSTEM SDK CONNECTION ---
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Library build ---
set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "") # Removes "lib" from the name (libname.so -> name.so)

# Allows the plugin to include local files (e.g., stb_truetype.h in text-3d-effect)
target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Linking (Critically important step) ---
target_link_libraries(${PLUGIN_NAME} PRIVATE
    ShaderDesk::shader_desk_sdk    # Provides access to <shader-desk/wallpaper-effect.hpp>
    ShaderDesk::shader_utils       # Provides access to shader loading utilities
    glm::glm
    ${GLESv2_LIBRARIES}
)

# --- Automatic copying of resources to the build folder ---
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
