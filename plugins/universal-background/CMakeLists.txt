cmake_minimum_required(VERSION 3.13)
set(PLUGIN_NAME "universal-background")
project(${PLUGIN_NAME}-plugin LANGUAGES CXX)

find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)
find_package(ShaderDesk REQUIRED)

add_library(${PLUGIN_NAME} SHARED "${PLUGIN_NAME}.cpp")
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(${PLUGIN_NAME} PRIVATE ShaderDesk::shader_desk_sdk ShaderDesk::shader_utils glm::glm ${GLESv2_LIBRARIES})

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(SHADER_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders/${PLUGIN_NAME}")
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${SHADER_DEST_DIR}"
    )
endif()