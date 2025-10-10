cmake_minimum_required(VERSION 3.13)
project(ico-sphere-effect-plugin LANGUAGES CXX)

# --- Находим зависимости плагина ---
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# --- Исходные файлы плагина ---
set(PLUGIN_SOURCES
    "ico-sphere-effect.cpp"
)

# --- Создаем плагин как разделяемую библиотеку (.so) ---
add_library(ico-sphere-effect SHARED ${PLUGIN_SOURCES})

# --- Настройка путей и имени ---
set_target_properties(ico-sphere-effect PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    PREFIX "" 
)

# --- Подключаем заголовочные файлы ---
# ${SRC_DIR} унаследован из корневого CMakeLists.txt.
target_include_directories(ico-sphere-effect PRIVATE
    "${SRC_DIR}"
    ${GLESv2_INCLUDE_DIRS}
)

# --- Линкуем библиотеки ---
target_link_libraries(ico-sphere-effect PRIVATE
    glm::glm
    ${GLESv2_LIBRARIES}
    shader_utils
)


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(
        TARGET ico-sphere-effect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${PLUGIN_OUTPUT_DIR}/shaders/ico-sphere-effect"
        COMMENT "Copying ico-sphere-effect shaders"
    )
endif()

message(STATUS "Configured plugin: ico-sphere-effect")