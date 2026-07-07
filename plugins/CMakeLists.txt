# plugins/CMakeLists.txt
cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

# Ищем все поддиректории в текущей папке
file(GLOB PLUGIN_SUBDIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")

message(STATUS "Discovering plugins...")
foreach(PLUGIN_DIR ${PLUGIN_SUBDIRS})
    if(IS_DIRECTORY "${PLUGIN_DIR}" AND EXISTS "${PLUGIN_DIR}/CMakeLists.txt")
        # НАДЕЖНО извлекаем имя папки из абсолютного пути
        get_filename_component(PLUGIN_NAME "${PLUGIN_DIR}" NAME)
        
        # Игнорируем папку с шаблонами
        if(PLUGIN_NAME STREQUAL "templates")
            continue()
        endif()

        message(STATUS "  -> Found plugin: ${PLUGIN_NAME}, adding to build.")
        add_subdirectory(${PLUGIN_NAME})
        
        # УСТАНАВЛИВАЕМ .so плагина в системную директорию (для sudo cmake --install)
        install(TARGETS ${PLUGIN_NAME}
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" # Для Windows/DLL совместимости, если вдруг
        )
        
        # УСТАНАВЛИВАЕМ ресурсы плагина в систему
        if(EXISTS "${PLUGIN_DIR}/shaders")
            install(DIRECTORY "${PLUGIN_DIR}/shaders"
                DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}"
            )
        endif()
        
        if(EXISTS "${PLUGIN_DIR}/presets")
            install(DIRECTORY "${PLUGIN_DIR}/presets"
                DESTINATION "${CMAKE_INSTALL_LIBDIR}/shader-desk/plugins/${PLUGIN_NAME}"
            )
        endif()
    endif()
endforeach()