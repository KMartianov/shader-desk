cmake_minimum_required(VERSION 3.13)

# ==============================================================================
# 1. SPECIFY THE PROVIDER NAME (Must match the .cpp filename!)
# ==============================================================================
set(PLUGIN_NAME "audio-provider")

project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

# --- CORE SYSTEM SDK CONNECTION ---
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Library build ---
set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "")

target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Linking ---
target_link_libraries(${PLUGIN_NAME} PRIVATE
    ShaderDesk::shader_desk_sdk    # Provides access to <shader-desk/data-provider.hpp>
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
