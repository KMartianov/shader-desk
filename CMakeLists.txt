cmake_minimum_required(VERSION 3.13)
project(interactive-wallpaper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Рекомендуется для удобства отладки/IDE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(glm REQUIRED)
find_package(nlohmann_json REQUIRED)

# Пути
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Скопируем общие шейдеры в бинарный каталог
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/shaders" DESTINATION "${BIN_DIR}")
endif()

# Wayland/GL ES зависимости через pkg-config
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESv2 REQUIRED glesv2)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

# Optional system wayland-protocols
pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)

# Find wayland-scanner and sed
find_program(WAYLAND_SCANNER NAMES wayland-scanner)
find_program(SED NAMES sed)

# Папка вывода для сгенерированных протоколов
set(PROTOCOL_OUTPUT_DIR "${BIN_DIR}")
file(MAKE_DIRECTORY "${PROTOCOL_OUTPUT_DIR}")

set(PROTOCOL_SOURCES "")
set(GENERATED_PROTOCOL_HEADERS "")

if(WAYLAND_SCANNER AND SED)
    message(STATUS "Found wayland-scanner: ${WAYLAND_SCANNER}")
    message(STATUS "Using sed: ${SED}")

    # Third-party protocol dir shipped with project
    set(THIRD_PARTY_PROTO_DIR "${SRC_DIR}/third_party/wlr-protocols")

    #
    # --- wlr-layer-shell (unstable) ---
    #
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
            COMMENT "Fixing namespace keyword in layer shell protocol"
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
    else()
        message(WARNING "wlr-layer-shell xml not found at ${LAYER_SHELL_XML}")
    endif()

    #
    # --- viewporter ---
    #
    if(WAYLAND_PROTOCOLS_FOUND)
        set(VIEWPORTER_XML "${WAYLAND_PROTOCOLS_PREFIX}/share/wayland-protocols/stable/viewporter/viewporter.xml")
    else()
        # Try common system paths
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
    else()
        # try alternate location
        set(VIEWPORTER_XML_ALT "/usr/local/share/wayland-protocols/stable/viewporter/viewporter.xml")
        if(EXISTS "${VIEWPORTER_XML_ALT}")
            add_custom_command(
                OUTPUT "${VIEWPORTER_HEADER}" "${VIEWPORTER_SOURCE}"
                COMMAND ${WAYLAND_SCANNER} client-header "${VIEWPORTER_XML_ALT}" "${VIEWPORTER_HEADER}"
                COMMAND ${WAYLAND_SCANNER} private-code  "${VIEWPORTER_XML_ALT}" "${VIEWPORTER_SOURCE}"
                DEPENDS "${VIEWPORTER_XML_ALT}"
                COMMENT "Generating viewporter protocol (alt path)"
                VERBATIM
            )
            list(APPEND PROTOCOL_SOURCES "${VIEWPORTER_SOURCE}")
            list(APPEND GENERATED_PROTOCOL_HEADERS "${VIEWPORTER_HEADER}")
        else()
            message(WARNING "viewporter XML not found. Tried: ${VIEWPORTER_XML} and ${VIEWPORTER_XML_ALT}")
        endif()
    endif()

    #
    # --- xdg-shell (system) ---
    #
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
    else()
        message(WARNING "xdg-shell xml not found at ${XDG_XML}")
    endif()

    # custom target to build all protocols
    add_custom_target(wayland_protocols ALL
        DEPENDS ${PROTOCOL_SOURCES} ${GENERATED_PROTOCOL_HEADERS}
    )
else()
    message(WARNING "wayland-scanner or sed not found. CMake will try to use any existing protocol sources in build/ or source tree.")
endif()

#
# Если источники протоколов не были сгенерированы, попробуем найти уже готовые файлы
#
if(PROTOCOL_SOURCES STREQUAL "")
    set(POSSIBLE_PROTO_SRCS
        "${BIN_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c"
        "${BIN_DIR}/viewporter-client-protocol.c"
        "${BIN_DIR}/xdg-shell-client-protocol.c"
        "${SRC_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c"
        "${SRC_DIR}/viewporter-client-protocol.c"
        "${SRC_DIR}/xdg-shell-client-protocol.c"
    )

    foreach(_psrc IN LISTS POSSIBLE_PROTO_SRCS)
        if(EXISTS "${_psrc}")
            list(APPEND PROTOCOL_SOURCES "${_psrc}")
            message(STATUS "Found existing protocol source: ${_psrc}")
        endif()
    endforeach()
endif()

# Основные исходники
set(APP_SRC_FILES
    "${SRC_DIR}/main.cpp"
    "${SRC_DIR}/interactive-wallpaper.cpp"
    "${SRC_DIR}/config-loader.cpp"
    "${SRC_DIR}/input-handler.cpp"
    "${SRC_DIR}/pointer-daemon-client.cpp"
    "${SRC_DIR}/plugin-manager.cpp"
)
list(APPEND APP_SRC_FILES ${PROTOCOL_SOURCES})

# Сборка основного приложения
add_executable(interactive-wallpaper ${APP_SRC_FILES})

# Подключаем плагины как подпроект
add_subdirectory(plugins)

# Настройка основного приложения
target_include_directories(interactive-wallpaper PRIVATE
    "${BIN_DIR}"
    "${PROTOCOL_OUTPUT_DIR}"
    "${SRC_DIR}"
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_EGL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESv2_INCLUDE_DIRS}
)

target_link_libraries(interactive-wallpaper PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_EGL_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESv2_LIBRARIES}
    nlohmann_json::nlohmann_json
    glm::glm
    dl
    pthread
)

# Если сгенерирован target wayland_protocols — добавить зависимость
if(TARGET wayland_protocols)
    add_dependencies(interactive-wallpaper wayland_protocols)
endif()

# Установка путей для плагинов
set(PLUGIN_OUTPUT_DIR "${BIN_DIR}/effects")
file(MAKE_DIRECTORY ${PLUGIN_OUTPUT_DIR})

# Копирование шейдеров плагинов
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ico-sphere-effect/shaders")
    add_custom_target(copy_plugin_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ico-sphere-effect/shaders"
            "${PLUGIN_OUTPUT_DIR}/shaders"
        COMMENT "Copying plugin shaders"
    )

    add_dependencies(interactive-wallpaper copy_plugin_shaders)
endif()

message(STATUS "Protocol sources: ${PROTOCOL_SOURCES}")
message(STATUS "Include dirs: ${BIN_DIR} ${PROTOCOL_OUTPUT_DIR} ${SRC_DIR}")