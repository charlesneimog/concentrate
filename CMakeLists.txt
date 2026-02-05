cmake_minimum_required(VERSION 3.21)
project(Concentrate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SPDLOG_BUILD_SHARED OFF)

# ╭──────────────────────────────────────╮
# │                 CPM                  │
# ╰──────────────────────────────────────╯
set(CPM_FILE ${CMAKE_BINARY_DIR}/CPM.cmake)
set(CPM_VERSION "0.42.0")
if(NOT EXISTS "${CPM_FILE}")
    file(DOWNLOAD "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake" ${CPM_FILE})
endif()
include(${CPM_FILE})

# ╭──────────────────────────────────────╮
# │             Dependencies             │
# ╰──────────────────────────────────────╯
cpmaddpackage("gh:yhirose/cpp-httplib@0.30.1")
cpmaddpackage("gh:nlohmann/json@3.11.3")
cpmaddpackage("gh:gabime/spdlog@1.17.0")

find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_DBUS REQUIRED dbus-1)

# ╭──────────────────────────────────────╮
# │            get libsecret             │
# ╰──────────────────────────────────────╯
pkg_check_modules(PC_LIBSECRET REQUIRED libsecret-1)
include_directories(${PC_LIBSECRET_INCLUDE_DIRS})
link_directories(${PC_LIBSECRET_LIBRARY_DIRS})
add_definitions(${PC_LIBSECRET_CFLAGS_OTHER})
add_definitions("-DCPPHTTPLIB_THREAD_POOL_COUNT=2")

# ╭──────────────────────────────────────╮
# │       Concentrate executable        │
# ╰──────────────────────────────────────╯
add_executable(
    Concentrate
    src/main.cpp
    src/Concentrate.cpp
    src/tray.cpp
    src/secrets.cpp
    src/notification.cpp
    src/window.cpp
    src/anytype.cpp
    src/hydration.cpp
    src/json.cpp
    src/sqlite.cpp)

# Find tailwindcss executable
find_program(TAILWINDCSS_EXECUTABLE tailwindcss)
if(NOT TAILWINDCSS_EXECUTABLE)
    message(FATAL_ERROR "tailwindcss is not installed or not in PATH")
endif()

# Web UI source locations
set(WEBUI_DIR ${CMAKE_SOURCE_DIR}/webui)

# JS/module assets live in webui/ (preferred). Fall back to repo root for older layouts.
set(WEBUI_ASSETS_DIR ${WEBUI_DIR})
if(NOT EXISTS "${WEBUI_ASSETS_DIR}/main.js")
    set(WEBUI_ASSETS_DIR ${CMAKE_SOURCE_DIR})
endif()

set(WEBUI_INDEX ${CMAKE_SOURCE_DIR}/index.html)
if(EXISTS "${WEBUI_DIR}/index.html")
    set(WEBUI_INDEX ${WEBUI_DIR}/index.html)
endif()

set(TAILWIND_INPUT ${CMAKE_SOURCE_DIR}/input.css)
if(EXISTS "${WEBUI_DIR}/input.css")
    set(TAILWIND_INPUT ${WEBUI_DIR}/input.css)
endif()

add_custom_target(
    tailwindcss ALL
    COMMAND ${TAILWINDCSS_EXECUTABLE} -i ${TAILWIND_INPUT} -o ${CMAKE_BINARY_DIR}/style.css > /dev/null 2>&1
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/style.css $<TARGET_FILE_DIR:Concentrate>/style.css
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WEBUI_INDEX} $<TARGET_FILE_DIR:Concentrate>/index.html
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Tailwind CSS")

add_custom_target(
    webfiles
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WEBUI_INDEX} $<TARGET_FILE_DIR:Concentrate>/index.html
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WEBUI_ASSETS_DIR}/main.js $<TARGET_FILE_DIR:Concentrate>/main.js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/resources/favicon.svg $<TARGET_FILE_DIR:Concentrate>/favicon.svg
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_ASSETS_DIR}/core $<TARGET_FILE_DIR:Concentrate>/core
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_ASSETS_DIR}/modules $<TARGET_FILE_DIR:Concentrate>/modules
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_ASSETS_DIR}/views $<TARGET_FILE_DIR:Concentrate>/views
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_ASSETS_DIR}/utils $<TARGET_FILE_DIR:Concentrate>/utils
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEBUI_ASSETS_DIR}/api $<TARGET_FILE_DIR:Concentrate>/api
    COMMENT "Copying webfiles")

add_dependencies(Concentrate webfiles tailwindcss)

target_include_directories(Concentrate PRIVATE ${PC_DBUS_INCLUDE_DIRS})
target_link_directories(Concentrate PRIVATE ${PC_DBUS_LIBRARY_DIRS})
target_compile_definitions(Concentrate PRIVATE ${PC_DBUS_CFLAGS_OTHER})

target_link_libraries(
    Concentrate
    PRIVATE httplib::httplib
            nlohmann_json::nlohmann_json #
            SQLite::SQLite3
            spdlog
            # Others
            ${PC_LIBSECRET_LIBRARIES}
            ${PC_DBUS_LIBRARIES})

target_compile_options(Concentrate PRIVATE $<$<CONFIG:Release>:-O3 -march=native -DNDEBUG>)
install(TARGETS Concentrate RUNTIME DESTINATION bin)

install(
    FILES ${WEBUI_INDEX}
        ${WEBUI_ASSETS_DIR}/main.js
          ${CMAKE_SOURCE_DIR}/resources/favicon.svg
          ${CMAKE_BINARY_DIR}/style.css
    DESTINATION share/Concentrate)

install(
    DIRECTORY ${WEBUI_ASSETS_DIR}/core
              ${WEBUI_ASSETS_DIR}/modules
              ${WEBUI_ASSETS_DIR}/views
              ${WEBUI_ASSETS_DIR}/utils
              ${WEBUI_ASSETS_DIR}/api
    DESTINATION share/Concentrate)


# Linux desktop integration
install(FILES ${CMAKE_SOURCE_DIR}/resources/Concentrate.desktop DESTINATION share/applications)
install(
    FILES ${CMAKE_SOURCE_DIR}/resources/favicon.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME concentrate.svg)
install(
    FILES ${CMAKE_SOURCE_DIR}/resources/focused.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME concentrate-focused.svg)
install(
    FILES ${CMAKE_SOURCE_DIR}/resources/unfocused.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME concentrate-unfocused.svg)
install(
    FILES ${CMAKE_SOURCE_DIR}/resources/disable.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME concentrate-off.svg)
