# plugins/text-3d-effect/CMakeLists.txt
cmake_minimum_required(VERSION 3.13)
project(text-3d-effect-plugin LANGUAGES CXX)

# --- Находим зависимости плагина ---
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# --- Исходные файлы плагина ---
# Включаем только .cpp, так как stb_truetype.h — это header-only библиотека
set(PLUGIN_SOURCES
    "text-3d-effect.cpp"
)

# --- Создаем плагин как разделяемую библиотеку (.so) ---
add_library(text-3d-effect SHARED ${PLUGIN_SOURCES})

# --- Настройка путей и имени (убираем префикс lib) ---
set_target_properties(text-3d-effect PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIR}"
    PREFIX "" 
)

# --- Подключаем заголовочные файлы ---
# ${CMAKE_SOURCE_DIR} гарантирует доступ к заголовочным файлам Ядра (src/)
target_include_directories(text-3d-effect PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    ${GLESv2_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}" # Чтобы компилятор нашел stb_truetype.h
)

# --- Линкуем библиотеки ---
target_link_libraries(text-3d-effect PRIVATE
    glm::glm
    ${GLESv2_LIBRARIES}
    shader_utils
)

# --- Автоматическое копирование шейдеров при сборке ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(
        TARGET text-3d-effect POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${PLUGIN_OUTPUT_DIR}/shaders/text-3d-effect"
        COMMENT "Copying text-3d-effect shaders"
    )
endif()

message(STATUS "Configured plugin: text-3d-effect")