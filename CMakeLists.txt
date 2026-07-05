cmake_minimum_required(VERSION 3.14)
project(shader-desk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Directories ---
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(PROTOCOL_OUTPUT_DIR "${BIN_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTOCOL_OUTPUT_DIR}")

# --- System Dependencies ---
find_package(PkgConfig REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESv2 REQUIRED glesv2)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)
pkg_check_modules(LUAJIT REQUIRED luajit)

# --- Fetch Lua/C++ Binding (sol2) ---
include(FetchContent)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG main
)
FetchContent_MakeAvailable(sol2)

# ==============================================================================
# WAYLAND PROTOCOLS GENERATION
# ==============================================================================
find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
find_program(SED NAMES sed REQUIRED)

set(PROTOCOL_SOURCES "")
set(GENERATED_PROTOCOL_HEADERS "")
set(THIRD_PARTY_PROTO_DIR "${SRC_DIR}/third_party/wlr-protocols")

# 1. wlr-layer-shell (unstable)
set(LAYER_SHELL_XML "${THIRD_PARTY_PROTO_DIR}/unstable/wlr-layer-shell-unstable-v1.xml")
set(LAYER_SHELL_XML_FIXED "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-fixed.xml")
set(LAYER_SHELL_HEADER "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
set(LAYER_SHELL_SOURCE "${PROTOCOL_OUTPUT_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c")

if(EXISTS "${LAYER_SHELL_XML}")
    add_custom_command(
        OUTPUT "${LAYER_SHELL_XML_FIXED}"
        COMMAND ${CMAKE_COMMAND} -E copy "${LAYER_SHELL_XML}" "${LAYER_SHELL_XML_FIXED}"
        COMMAND ${SED} -i "s/name=\\\"namespace\\\"/name=\\\"namespace_\\\"/g" "${LAYER_SHELL_XML_FIXED}"
        DEPENDS "${LAYER_SHELL_XML}"
        COMMENT "Patching namespace keyword in wlr-layer-shell protocol"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${LAYER_SHELL_HEADER}" "${LAYER_SHELL_SOURCE}"
        COMMAND ${WAYLAND_SCANNER} client-header "${LAYER_SHELL_XML_FIXED}" "${LAYER_SHELL_HEADER}"
        COMMAND ${WAYLAND_SCANNER} private-code  "${LAYER_SHELL_XML_FIXED}" "${LAYER_SHELL_SOURCE}"
        DEPENDS "${LAYER_SHELL_XML_FIXED}"
        COMMENT "Generating wlr-layer-shell protocol"
        VERBATIM
    )
    list(APPEND PROTOCOL_SOURCES "${LAYER_SHELL_SOURCE}")
    list(APPEND GENERATED_PROTOCOL_HEADERS "${LAYER_SHELL_HEADER}")
endif()

# 2. viewporter
if(WAYLAND_PROTOCOLS_FOUND)
    set(VIEWPORTER_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable/viewporter/viewporter.xml")
else()
    set(VIEWPORTER_XML "/usr/share/wayland-protocols/stable/viewporter/viewporter.xml")
endif()

set(VIEWPORTER_HEADER "${PROTOCOL_OUTPUT_DIR}/viewporter-client-protocol.h")
set(VIEWPORTER_SOURCE "${PROTOCOL_OUTPUT_DIR}/viewporter-client-protocol.c")

if(EXISTS "${VIEWPORTER_XML}")
    add_custom_command(
        OUTPUT "${VIEWPORTER_HEADER}" "${VIEWPORTER_SOURCE}"
        COMMAND ${WAYLAND_SCANNER} client-header "${VIEWPORTER_XML}" "${VIEWPORTER_HEADER}"
        COMMAND ${WAYLAND_SCANNER} private-code  "${VIEWPORTER_XML}" "${VIEWPORTER_SOURCE}"
        DEPENDS "${VIEWPORTER_XML}"
        COMMENT "Generating viewporter protocol"
        VERBATIM
    )
    list(APPEND PROTOCOL_SOURCES "${VIEWPORTER_SOURCE}")
    list(APPEND GENERATED_PROTOCOL_HEADERS "${VIEWPORTER_HEADER}")
endif()

# 3. xdg-shell (Required by wlr-layer-shell for popups)
if(WAYLAND_PROTOCOLS_FOUND)
    set(XDG_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
else()
    set(XDG_XML "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
endif()

set(XDG_HEADER "${PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.h")
set(XDG_SOURCE "${PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.c")

if(EXISTS "${XDG_XML}")
    add_custom_command(
        OUTPUT "${XDG_HEADER}" "${XDG_SOURCE}"
        COMMAND ${WAYLAND_SCANNER} client-header "${XDG_XML}" "${XDG_HEADER}"
        COMMAND ${WAYLAND_SCANNER} private-code  "${XDG_XML}" "${XDG_SOURCE}"
        DEPENDS "${XDG_XML}"
        COMMENT "Generating xdg-shell protocol"
        VERBATIM
    )
    list(APPEND PROTOCOL_SOURCES "${XDG_SOURCE}")
    list(APPEND GENERATED_PROTOCOL_HEADERS "${XDG_HEADER}")
endif()

add_custom_target(wayland_protocols ALL
    DEPENDS ${PROTOCOL_SOURCES} ${GENERATED_PROTOCOL_HEADERS}
)

# ==============================================================================
# EMBEDDED RESOURCES GENERATION (Lua -> C++ Headers)
# ==============================================================================
set(GENERATED_DIR "${BIN_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# 1. Генерация для ctl.lua
set(EMBED_CTL_HPP "${GENERATED_DIR}/embedded_ctl.hpp")
add_custom_command(
    OUTPUT "${EMBED_CTL_HPP}"
    COMMAND ${CMAKE_COMMAND} 
        -DINPUT_FILE="${SRC_DIR}/defaults/ctl.lua"
        -DOUTPUT_FILE="${EMBED_CTL_HPP}"
        -DVAR_NAME="CTL_LUA"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
    DEPENDS "${SRC_DIR}/defaults/ctl.lua" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
    COMMENT "Baking ctl.lua into C++ header"
    VERBATIM
)

# 2. Генерация для init.lua
set(EMBED_INIT_HPP "${GENERATED_DIR}/embedded_init.hpp")
add_custom_command(
    OUTPUT "${EMBED_INIT_HPP}"
    COMMAND ${CMAKE_COMMAND} 
        -DINPUT_FILE="${SRC_DIR}/defaults/init.lua"
        -DOUTPUT_FILE="${EMBED_INIT_HPP}"
        -DVAR_NAME="INIT_LUA"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
    DEPENDS "${SRC_DIR}/defaults/init.lua" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_resource.cmake"
    COMMENT "Baking init.lua into C++ header"
    VERBATIM
)

add_custom_target(embedded_resources ALL
    DEPENDS "${EMBED_CTL_HPP}" "${EMBED_INIT_HPP}"
)

# ==============================================================================
# CORE & SDK LIBRARIES
# ==============================================================================

# --- Shader Utilities (Exported as part of the SDK) ---
add_library(shader_utils SHARED "${SRC_DIR}/shader-utils.cpp")
set_property(TARGET shader_utils PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(shader_utils PUBLIC
    $<BUILD_INTERFACE:${SRC_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/shader-desk>
)
target_link_libraries(shader_utils PRIVATE ${GLESv2_LIBRARIES})

# --- Main Executable (Microkernel) ---
set(APP_SRC_FILES
    "${SRC_DIR}/main.cpp"
    "${SRC_DIR}/interactive-wallpaper.cpp"
    "${SRC_DIR}/lua-engine.cpp"             
    "${SRC_DIR}/lua-config-generator.cpp"   
    "${SRC_DIR}/plugin-manager.cpp"
)
list(APPEND APP_SRC_FILES ${PROTOCOL_SOURCES})

# --- Установка RPATH для поиска разделяемых библиотек SDK ---
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${CMAKE_INSTALL_PREFIX}/lib64")
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# 1. Основное ядро
add_executable(interactive-wallpaper ${APP_SRC_FILES})

# 2. CLI-контроллер (не требует линковки с Wayland/OpenGL, собирается мгновенно)
add_executable(shader-desk-ctl utilities/shader-desk-ctl.cpp)

target_include_directories(interactive-wallpaper PRIVATE
    "${GENERATED_DIR}"      # Добавлен доступ к сгенерированным .hpp с вшитым Lua
    "${PROTOCOL_OUTPUT_DIR}"
    "${SRC_DIR}"
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_EGL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESv2_INCLUDE_DIRS}
    ${LUAJIT_INCLUDE_DIRS} 
)

target_link_libraries(interactive-wallpaper PRIVATE
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

# Зависимости сборки (сначала генерируем протоколы Wayland и ресурсы Lua)
if(TARGET wayland_protocols)
    add_dependencies(interactive-wallpaper wayland_protocols)
endif()

if(TARGET embedded_resources)
    add_dependencies(interactive-wallpaper embedded_resources)
endif()

# ==============================================================================
# INSTALLATION & SDK EXPORT RULES
# ==============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 1. Install both engine binary and CLI controller
install(TARGETS interactive-wallpaper shader-desk-ctl
        DESTINATION ${CMAKE_INSTALL_BINDIR})

# 2. Install public C-ABI SDK headers for third-party plugins
set(SDK_HEADERS
    "${SRC_DIR}/plugin-abi.hpp"
    "${SRC_DIR}/core-context.hpp"
    "${SRC_DIR}/wallpaper-effect.hpp"
    "${SRC_DIR}/data-provider.hpp"
    "${SRC_DIR}/shader-utils.hpp"
)
install(FILES ${SDK_HEADERS} 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shader-desk)

# 3. Create a virtual INTERFACE target for modern CMake find_package linking
add_library(shader_desk_sdk INTERFACE)
target_include_directories(shader_desk_sdk INTERFACE
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/shader-desk>
)

# 4. Export the SDK targets (shader_utils and shader_desk_sdk)
install(TARGETS shader_utils shader_desk_sdk
        EXPORT ShaderDeskTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/shader-desk)

install(EXPORT ShaderDeskTargets
        FILE ShaderDeskTargets.cmake
        NAMESPACE ShaderDesk::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ShaderDesk)

# Create config file so `find_package(ShaderDesk REQUIRED)` works for plugins
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ShaderDeskConfig.cmake" 
"include(\"\${CMAKE_CURRENT_LIST_DIR}/ShaderDeskTargets.cmake\")\n"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ShaderDeskConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ShaderDesk)

# 5. Install the plugin generator python script and its templates
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/generate_plugin.py"
        DESTINATION ${CMAKE_INSTALL_BINDIR} 
        RENAME shader-desk-generate)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugins/templates/"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/shader-desk/templates)

message(STATUS "Shader Desk build configured. Run 'make install' to export the SDK.")