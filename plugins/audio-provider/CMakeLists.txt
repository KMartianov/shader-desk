cmake_minimum_required(VERSION 3.13)
project(audio-provider-plugin LANGUAGES CXX)

add_library(audio-provider SHARED audio-provider.cpp)

set_target_properties(audio-provider PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    PREFIX "" 
)

target_include_directories(audio-provider PRIVATE "${CMAKE_SOURCE_DIR}/src")
message(STATUS "Configured Data Provider: audio-provider")