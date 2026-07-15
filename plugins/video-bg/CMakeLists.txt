cmake_minimum_required(VERSION 3.13)
project(video-bg-plugin LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv) # <--- Ищем libmpv

add_library(video-bg SHARED "video-bg.cpp")
set_target_properties(video-bg PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(video-bg PRIVATE
    ShaderDesk::shader_desk_sdk
    ShaderDesk::shader_utils
    glm::glm
    ${MPV_LIBRARIES} # <--- Линкуем
)