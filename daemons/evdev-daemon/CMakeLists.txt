cmake_minimum_required(VERSION 3.10)
project(evdev-pointer-daemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For convenience when building outside the source folder
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

if(NOT TARGET ShaderDesk::shader_desk_sdk)
    find_package(ShaderDesk REQUIRED)
endif()


# Executable file (use absolute path relative to the source directory)
add_executable(evdev-pointer-daemon
    "${SRC_DIR}/main.cpp"
)

# include dirs (includes the source folder so main.cpp can access local headers if needed)
target_include_directories(evdev-pointer-daemon PRIVATE
    ${LIBEVDEV_INCLUDE_DIRS}
    "${SRC_DIR}"
)

# Convert pkg-config flags to a list and apply as compiler options
if(LIBEVDEV_CFLAGS)
    string(REPLACE " " ";" _libevdev_cflags_list "${LIBEVDEV_CFLAGS}")
    target_compile_options(evdev-pointer-daemon PRIVATE ${_libevdev_cflags_list})
endif()

if(LIBEVDEV_CFLAGS_OTHER)
    string(REPLACE " " ";" _libevdev_cflags_other_list "${LIBEVDEV_CFLAGS_OTHER}")
    target_compile_options(evdev-pointer-daemon PRIVATE ${_libevdev_cflags_other_list})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(evdev-pointer-daemon PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Link libraries
target_link_libraries(evdev-pointer-daemon PRIVATE 
    ${LIBEVDEV_LIBRARIES}
    ShaderDesk::shader_desk_sdk   
)

# Specify the binary output directory (in build/)
set_target_properties(evdev-pointer-daemon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}"
)

# Installation rule
include(GNUInstallDirs)
install(TARGETS evdev-pointer-daemon
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Clear messages during configuration
message(STATUS "evdev-pointer-daemon source dir: ${SRC_DIR}")
message(STATUS "evdev-pointer-daemon binary dir: ${BIN_DIR}")
message(STATUS "libevdev includes: ${LIBEVDEV_INCLUDE_DIRS}")
message(STATUS "libevdev libraries: ${LIBEVDEV_LIBRARIES}")
