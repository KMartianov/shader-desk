cmake_minimum_required(VERSION 3.10)
project(audio-daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
pkg_check_modules(PULSEAUDIO REQUIRED libpulse)
pkg_check_modules(PULSEAUDIO_SIMPLE REQUIRED libpulse-simple)

if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()

# Add executable
add_executable(audio-daemon main.cpp)

# Link libraries
target_include_directories(audio-daemon PUBLIC
    ${FFTW3_INCLUDE_DIRS}
    ${PULSEAUDIO_INCLUDE_DIRS}
    ${PULSEAUDIO_SIMPLE_INCLUDE_DIRS}
)
target_link_libraries(audio-daemon PRIVATE
    ${FFTW3_LIBRARIES}
    ${PULSEAUDIO_LIBRARIES}
    ${PULSEAUDIO_SIMPLE_LIBRARIES}
    ShaderDesk::shader_desk_sdk
)

include(GNUInstallDirs)
install(TARGETS audio-daemon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)