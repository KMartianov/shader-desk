cmake_minimum_required(VERSION 3.13)

project(ico-sphere-effect LANGUAGES CXX)

# Пути к основным заголовкам
set(MAIN_PROJECT_SRC_DIR "${CMAKE_SOURCE_DIR}/src")

# Находим необходимые библиотеки
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESv2 REQUIRED glesv2)

# Исходные файлы плагина
set(PLUGIN_SOURCES
    "ico-sphere-effect.cpp"
)

# Создаем плагин как shared library
add_library(ico-sphere-plugin SHARED ${PLUGIN_SOURCES})

# Настройка выходного пути
set_target_properties(ico-sphere-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../../effects"
    PREFIX ""
    OUTPUT_NAME "ico-sphere-effect"
)

# Подключаем заголовки
target_include_directories(ico-sphere-plugin PRIVATE
    "${MAIN_PROJECT_SRC_DIR}"
    ${GLESv2_INCLUDE_DIRS}
)

# Линковка
target_link_libraries(ico-sphere-plugin PRIVATE
    glm::glm
    ${GLESv2_LIBRARIES}
)

message(STATUS "Configured plugin: ico-sphere-effect")