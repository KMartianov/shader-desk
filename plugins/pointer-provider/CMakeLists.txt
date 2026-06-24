cmake_minimum_required(VERSION 3.13)
project(pointer-provider-plugin LANGUAGES CXX)

add_library(pointer-provider SHARED pointer-provider.cpp)

set_target_properties(pointer-provider PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    PREFIX "" 
)

target_include_directories(pointer-provider PRIVATE "${CMAKE_SOURCE_DIR}/src")
message(STATUS "Configured Data Provider: pointer-provider")