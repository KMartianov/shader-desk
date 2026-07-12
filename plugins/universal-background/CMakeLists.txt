cmake_minimum_required(VERSION 3.13)

set(PLUGIN_NAME "universal-background")
project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# ==============================================================================
# IN-TREE AND OUT-OF-TREE BUILD SUPPORT (MODULARITY)
# ==============================================================================
# if the plugin is built together with the core, the target already exists.
# if a third-party developer builds the plugin separately, look for the system package.
if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# --- Library build ---
set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")

# Remove the "lib" prefix and place the .so in the common plugin build folder
set_target_properties(${PLUGIN_NAME} PROPERTIES 
    PREFIX "" 
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_BUILD_DIR}"
)

target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# --- Linking ---
target_link_libraries(${PLUGIN_NAME} PRIVATE 
    ShaderDesk::shader_desk_sdk 
    ShaderDesk::shader_utils 
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