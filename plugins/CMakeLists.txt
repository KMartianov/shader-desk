# ./plugins/CMakeLists.txt

# Этот файл автоматически найдет все папки с плагинами и соберет их.

# Ищем все поддиректории, содержащие CMakeLists.txt
file(GLOB PLUGIN_SUBDIRS LIST_DIRECTORIES true ./*)

message(STATUS "Discovering plugins...")
foreach(PLUGIN_DIR ${PLUGIN_SUBDIRS})
    if(IS_DIRECTORY "${PLUGIN_DIR}" AND EXISTS "${PLUGIN_DIR}/CMakeLists.txt")
        string(REPLACE "./" "" PLUGIN_NAME ${PLUGIN_DIR})
        message(STATUS "  -> Found plugin: ${PLUGIN_NAME}, adding to build.")
        add_subdirectory(${PLUGIN_NAME})
    endif()
endforeach()