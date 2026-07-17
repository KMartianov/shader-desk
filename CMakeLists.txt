# ==============================================================================
# SHADER DESK - INTERACTIVE WAYLAND WALLPAPER ENGINE
# Production-ready CMake configuration for Linux package managers (AUR, APT, etc.)
# ==============================================================================
cmake_minimum_required(VERSION 3.14)

# define project version for package managers
project(shader-desk VERSION 1.0.0 LANGUAGES C CXX)

# ==============================================================================
# 1. GLOBAL CONFIGURATION & GNU INSTALL DIRS (FHS COMPLIANCE)
# ==============================================================================
# Automatically sets CMAKE_INSTALL_BINDIR, CMAKE_INSTALL_LIBDIR, etc.
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Define standard paths ---
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(PROTOCOL_OUTPUT_DIR "${BIN_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTOCOL_OUTPUT_DIR}")

# Create a virtual directory namespace for proper <shader-desk/...> includes
file(MAKE_DIRECTORY "${BIN_DIR}/include")
if(NOT EXISTS "${BIN_DIR}/include/shader-desk")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${SRC_DIR}" "${BIN_DIR}/include/shader-desk")
endif()

# define where compiled plugins should be installed globally
set(PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins" 
    CACHE PATH "System directory for compiled .so plugins")

# --- Build Options ---
option(BUILD_AUDIO_DAEMON "Build PipeWire/PulseAudio FFT daemon" ON)
option(BUILD_EVDEV_DAEMON "Build /dev/input mouse tracking daemon" ON)
option(ENABLE_PROFILING "Enable Tracy Profiler telemetry" OFF)

# ==============================================================================
# 2. SYSTEM DEPENDENCIES
# ==============================================================================
find_package(PkgConfig REQUIRED)

# Try to find system-wide dependencies (Highly preferred for package managers)
find_package(nlohmann_json REQUIRED)
find_package(glm REQUIRED)
find_package(sol2 QUIET) # Try to find system sol2 (e.g., lua-sol2 in AUR/APT)

pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESv2 REQUIRED glesv2)
pkg_check_modules(LUAJIT REQUIRED luajit)

# ==============================================================================
# 3. EXTERNAL DEPENDENCIES (FetchContent Fallback)
# ==============================================================================
include(FetchContent)

# Lua/C++ Binding (sol2)
# if the package manager already provided sol2, target sol2::sol2 already exists.
# Download from the network only if it is missing.
if(NOT TARGET sol2::sol2)
    message(STATUS "sol2 not found in system. Fetching from GitHub...")
    FetchContent_Declare(
        sol2
        GIT_REPOSITORY https://Github.com/ThePhD/sol2.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(sol2)
else()
    message(STATUS "Using system-provided sol2.")
endif()

# Tracy Profiler (Isolated: guarantees offline builds when ENABLE_PROFILING=OFF)
if(ENABLE_PROFILING)
    # Package managers might also provide Tracy globally
    find_package(Tracy QUIET)

    if(NOT TARGET TracyClient)
        message(STATUS "Tracy not found in system. Fetching from GitHub...")
        set(TRACY_SHARED ON CACHE BOOL "Build Tracy as a shared library" FORCE)
        set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiling" FORCE)

        FetchContent_Declare(
            tracy
            GIT_REPOSITORY https://Github.com/wolfpld/tracy.git
            GIT_TAG 5c75032c
        )
        FetchContent_MakeAvailable(tracy)
    else()
        message(STATUS "Using system-provided Tracy.")
    endif()
endif()

# ==============================================================================
# 4. WAYLAND PROTOCOLS GENERATION
# ==============================================================================
find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
find_program(SED NAMES sed REQUIRED)

set(PROTOCOL_SOURCES "")
set(GENERATED_PROTOCOL_HEADERS "")
set(THIRD_PARTY_PROTO_DIR "${SRC_DIR}/third_party/wlr-protocols")

# Helper macro for generating protocols from existing system files
macro(generate_wayland_protocol XML_PATH HEADER_FILE SOURCE_FILE)
    if(EXISTS "${XML_PATH}")
        add_custom_command(
            OUTPUT "${HEADER_FILE}" "${SOURCE_FILE}"
            COMMAND ${WAYLAND_SCANNER} client-header "${XML_PATH}" "${HEADER_FILE}"
            COMMAND ${WAYLAND_SCANNER} private-code  "${XML_PATH}" "${SOURCE_FILE}"
            DEPENDS "${XML_PATH}"
            COMMENT "Generating Wayland protocol: ${XML_PATH}"
            VERBATIM
        )
        list(APPEND PROTOCOL_SOURCES "${SOURCE_FILE}")
        list(APPEND GENERATED_PROTOCOL_HEADERS "${HEADER_FILE}")
    endif()
endmacro()

# 1. wlr-layer-shell (Patching C++ keyword collision)
# We avoid using the macro with if(EXISTS) for the generated file,
# Because it doesn't exist yet during the CMake configuration phase.
set(LAYER_SHELL_XML "${THIRD_PARTY_PROTO_DIR}/unstable/wlr-layer-shell-unstable-v1.xml")
set(LAYER_SHELL_XML_FIXED "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-fixed.xml")
if(EXISTS "${LAYER_SHELL_XML}")
    add_custom_command(
        OUTPUT "${LAYER_SHELL_XML_FIXED}"
        COMMAND ${CMAKE_COMMAND} -E copy "${LAYER_SHELL_XML}" "${LAYER_SHELL_XML_FIXED}"
        COMMAND ${SED} -i "s/name=\\\"namespace\\\"/name=\\\"namespace_\\\"/g" "${LAYER_SHELL_XML_FIXED}"
        DEPENDS "${LAYER_SHELL_XML}"
    )
    
    set(LS_HEADER "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
    set(LS_SOURCE "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c")
    
    add_custom_command(
        OUTPUT "${LS_HEADER}" "${LS_SOURCE}"
        COMMAND ${WAYLAND_SCANNER} client-header "${LAYER_SHELL_XML_FIXED}" "${LS_HEADER}"
        COMMAND ${WAYLAND_SCANNER} private-code  "${LAYER_SHELL_XML_FIXED}" "${LS_SOURCE}"
        DEPENDS "${LAYER_SHELL_XML_FIXED}"
        COMMENT "Generating Wayland protocol: ${LAYER_SHELL_XML_FIXED}"
        VERBATIM
    )
    list(APPEND PROTOCOL_SOURCES "${LS_SOURCE}")
    list(APPEND GENERATED_PROTOCOL_HEADERS "${LS_HEADER}")
endif()

# 2. Viewporter & XDG-Shell
if(WAYLAND_PROTOCOLS_FOUND)
    set(WP_STABLE_DIR "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable")
else()
    set(WP_STABLE_DIR "/usr/share/wayland-protocols/stable")
endif()

generate_wayland_protocol("${WP_STABLE_DIR}/viewporter/viewporter.xml"
    "${PROTOCOL_OUTPUT_DIR}/viewporter-client-protocol.h"
    "${PROTOCOL_OUTPUT_DIR}/viewporter-client-protocol.c")

generate_wayland_protocol("${WP_STABLE_DIR}/xdg-shell/xdg-shell.xml"
    "${PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.h"
    "${PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.c")

add_custom_target(wayland_protocols ALL DEPENDS ${PROTOCOL_SOURCES} ${GENERATED_PROTOCOL_HEADERS})

# ==============================================================================
# 5. EMBEDDED RESOURCES GENERATION (Text/Lua -> C++ Headers)
# ==============================================================================
set(GENERATED_DIR "${BIN_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# A generic macro to bake any text file into a C++ header at compile time
macro(embed_text_resource FILE_NAME HEADER_NAME VAR_NAME)
    set(HEADER_OUT "${GENERATED_DIR}/embedded_${HEADER_NAME}.hpp")
    add_custom_command(
        OUTPUT "${HEADER_OUT}"
        COMMAND ${CMAKE_COMMAND} 
            -DINPUT_FILE=${SRC_DIR}/defaults/${FILE_NAME}
            -DOUTPUT_FILE=${HEADER_OUT}
            -DVAR_NAME=${VAR_NAME}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
        DEPENDS "${SRC_DIR}/defaults/${FILE_NAME}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
        COMMENT "Baking ${FILE_NAME} into C++ header"
        VERBATIM
    )
    list(APPEND EMBEDDED_HEADERS "${HEADER_OUT}")
endmacro()

# Bake Lua configs and the CLI Help text
embed_text_resource("ctl.lua" "ctl" "CTL_LUA")
embed_text_resource("init.lua" "init" "INIT_LUA")
embed_text_resource("help.txt" "help" "HELP_TEXT")

add_custom_target(embedded_resources ALL DEPENDS ${EMBEDDED_HEADERS})

# ==============================================================================
# 6. CORE & SDK LIBRARIES
# ==============================================================================

# RPATH configuration for system-wide installation
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 1. SDK Helper Library (Shader Utils)
add_library(shader_utils SHARED "${SRC_DIR}/shader-utils.cpp")
add_library(ShaderDesk::shader_utils ALIAS shader_utils)
set_target_properties(shader_utils PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(shader_utils PUBLIC
    $<BUILD_INTERFACE:${SRC_DIR}>
    $<BUILD_INTERFACE:${BIN_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(shader_utils PRIVATE ${GLESv2_LIBRARIES})

# 2. Virtual SDK Interface for Plugins (Header-only ABI layer)
add_library(shader_desk_sdk INTERFACE)
add_library(ShaderDesk::shader_desk_sdk ALIAS shader_desk_sdk)
target_include_directories(shader_desk_sdk INTERFACE
    $<BUILD_INTERFACE:${SRC_DIR}>
    $<BUILD_INTERFACE:${BIN_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(ENABLE_PROFILING)
    target_link_libraries(shader_desk_sdk INTERFACE TracyClient)
    target_compile_definitions(shader_desk_sdk INTERFACE TRACY_ENABLE=1)
endif()

# ==============================================================================
# 7. EXECUTABLES: CORE & UTILITIES
# ==============================================================================
set(APP_SRC_FILES
    "${SRC_DIR}/main.cpp"
    "${SRC_DIR}/interactive-wallpaper.cpp"
    "${SRC_DIR}/lua-engine.cpp"             
    "${SRC_DIR}/lua-config-generator.cpp"   
    "${SRC_DIR}/plugin-manager.cpp"
)

if(ENABLE_PROFILING)
    list(APPEND APP_SRC_FILES "${SRC_DIR}/tracy-memory.cpp")
endif()

list(APPEND APP_SRC_FILES ${PROTOCOL_SOURCES})

# Main Core Executable (Wayland Microkernel)
add_executable(interactive-wallpaper ${APP_SRC_FILES})
target_include_directories(interactive-wallpaper PRIVATE
    "${GENERATED_DIR}"
    "${PROTOCOL_OUTPUT_DIR}"
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_EGL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESv2_INCLUDE_DIRS}
    ${LUAJIT_INCLUDE_DIRS} 
)

# Pass system paths to C++ via macros to avoid hardcoding!
target_compile_definitions(interactive-wallpaper PRIVATE
    SYSTEM_PLUGIN_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/shader-desk/plugins"
    SYSTEM_SOURCE_DIR="${CMAKE_INSTALL_FULL_DATADIR}/shader-desk/plugins-source"
    LOCAL_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins"
    LOCAL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/plugins"
    SYSTEM_SCENES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/shader-desk/scenes"
    LOCAL_SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/defaults/scenes"
)

target_link_libraries(interactive-wallpaper PRIVATE
    shader_desk_sdk
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_EGL_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESv2_LIBRARIES}
    ${LUAJIT_LIBRARIES}
    nlohmann_json::nlohmann_json
    glm::glm
    shader_utils
    sol2::sol2
    dl
    pthread
)

add_dependencies(interactive-wallpaper wayland_protocols embedded_resources)

# CLI Controller Utility
add_executable(shader-desk-ctl utilities/shader-desk-ctl.cpp)
target_link_libraries(shader-desk-ctl PRIVATE shader_desk_sdk)

# ==============================================================================
# 8. SUBDIRECTORIES (Daemons & Plugins)
# ==============================================================================
macro(add_submodule_if_exists SUBDIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt")
        add_subdirectory(${SUBDIR})
    endif()
endmacro()

if(BUILD_AUDIO_DAEMON)
    add_submodule_if_exists(daemons/audio-daemon)
endif()

if(BUILD_EVDEV_DAEMON)
    add_submodule_if_exists(daemons/evdev-daemon)
endif()

# The plugins directory handles compiling and installing native .so plugins
add_submodule_if_exists(plugins)

# ==============================================================================
# 9. SYSTEM INSTALLATION EXPORT RULES (FHS Compliant)
# ==============================================================================
include(CMakePackageConfigHelpers)

# 1. Binaries (Core & CLI)
install(TARGETS interactive-wallpaper shader-desk-ctl
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/utilities/shader-desk-run.sh"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME shader-desk-run)

# 2. Systemd User Services
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/systemd/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/systemd/user"
        FILES_MATCHING PATTERN "*.service")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/defaults/scenes/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/shader-desk/scenes")

# 3. SDK Headers (Required for out-of-tree plugin compilation)
set(SDK_HEADERS
    "${SRC_DIR}/plugin-abi.hpp"
    "${SRC_DIR}/core-context.hpp"
    "${SRC_DIR}/wallpaper-effect.hpp"
    "${SRC_DIR}/kinematic-effect.hpp"
    "${SRC_DIR}/data-provider.hpp"
    "${SRC_DIR}/shader-utils.hpp"
    "${SRC_DIR}/ipc-utils.hpp"
)
install(FILES ${SDK_HEADERS} 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shader-desk)

# 4. Export SDK CMake targets for 3rd-party plugin developers
install(TARGETS shader_utils shader_desk_sdk
        EXPORT ShaderDeskTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shader-desk)

install(EXPORT ShaderDeskTargets
        FILE ShaderDeskTargets.cmake
        NAMESPACE ShaderDesk::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ShaderDesk)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ShaderDeskConfig.cmake" 
"include(\"\${CMAKE_CURRENT_LIST_DIR}/ShaderDeskTargets.cmake\")\n"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ShaderDeskConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ShaderDesk)

# 5. User Workspace Setup: Copy Plugin Sources
# This allows users to run `--init-workspace` and modify raw .cpp/.glsl files
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugins/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/shader-desk/plugins-source"
        PATTERN "build*" EXCLUDE
        PATTERN ".git*" EXCLUDE
        PATTERN "*.so" EXCLUDE
        PATTERN "*.o" EXCLUDE)

# 6. Python Plugin Generator & Templates
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/generate_plugin.py"
        DESTINATION ${CMAKE_INSTALL_BINDIR} 
        RENAME shader-desk-generate)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugins/templates/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/shader-desk/templates")

message(STATUS "-------------------------------------------------------------")
message(STATUS "Shader Desk v1.0.0 build configured.")
message(STATUS "System Plugin Dir: ${PLUGIN_INSTALL_DIR}")
message(STATUS "System Source Dir: ${CMAKE_INSTALL_FULL_DATADIR}/shader-desk/plugins-source")
message(STATUS "Run 'cmake --build build -j$(nproc)' to compile.")
message(STATUS "Run 'sudo cmake --install build' to deploy to the system.")
message(STATUS "-------------------------------------------------------------")

# ==============================================================================
# 10. UNINSTALL TARGET
# ==============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstalling Shader Desk from the system..."
)