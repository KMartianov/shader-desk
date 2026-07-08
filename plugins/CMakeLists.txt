# plugins/CMakeLists.txt
cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

# Ищем все поддиректории в текущей папке
file(GLOB PLUGIN_SUBDIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")

message(STATUS "Discovering plugins...")
# Определяем, собирает ли юзер плагины локально в своем воркспейсе
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IS_USER_WORKSPACE TRUE)
    project(ShaderDesk-UserPlugins LANGUAGES CXX)
    find_package(ShaderDesk REQUIRED) # Ищем системный SDK
else()
    set(IS_USER_WORKSPACE FALSE)
endif()

message(STATUS "Discovering plugins...")
foreach(PLUGIN_DIR ${PLUGIN_SUBDIRS})
    if(IS_DIRECTORY "${PLUGIN_DIR}" AND EXISTS "${PLUGIN_DIR}/CMakeLists.txt")
        get_filename_component(PLUGIN_NAME "${PLUGIN_DIR}" NAME)
        
        if(PLUGIN_NAME STREQUAL "templates" OR PLUGIN_NAME MATCHES "^build")
            continue()
        endif()

        add_subdirectory(${PLUGIN_NAME})
        
        # ЛОГИКА БАНДЛОВ
        if(IS_USER_WORKSPACE)
            # В воркспейсе кладем .so прямо к исходникам и шейдерам
            set_target_properties(${PLUGIN_NAME} PROPERTIES 
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${PLUGIN_NAME}"
            )
        else()
            # При сборке ядра - системная установка (FHS)
            install(TARGETS ${PLUGIN_NAME}
                LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}"
            )
            if(EXISTS "${PLUGIN_DIR}/shaders")
                install(DIRECTORY "${PLUGIN_DIR}/shaders"
                    DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}")
            endif()
            if(EXISTS "${PLUGIN_DIR}/presets")
                install(DIRECTORY "${PLUGIN_DIR}/presets"
                    DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}")
            endif()
        endif()
    endif()
endforeach()