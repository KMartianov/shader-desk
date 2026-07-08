cmake_minimum_required(VERSION 3.14)
set(PLUGIN_NAME "shadertoy-effect")
project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

set(PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_NAME}")
add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")

set_target_properties(${PLUGIN_NAME} PROPERTIES 
    PREFIX "" 
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_BUILD_DIR}"
)

target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${PLUGIN_NAME} PRIVATE 
    ShaderDesk::shader_desk_sdk 
    ShaderDesk::shader_utils 
    glm::glm 
    ${GLESv2_LIBRARIES}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
                "${PLUGIN_BUILD_DIR}/shaders"
    )
endif()