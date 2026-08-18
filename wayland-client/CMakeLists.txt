cmake_minimum_required(VERSION 3.16)
project(unikey-wayland LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 components
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 COMPONENTS Core Gui Widgets DBus REQUIRED)

# Wayland protocol generation
find_program(WAYLAND_SCANNER_BIN wayland-scanner REQUIRED)
set(PROTOCOL_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v1.xml)
set(PROTOCOL_C ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v1-protocol.c)
set(PROTOCOL_H ${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v1-client-protocol.h)

add_custom_command(
    OUTPUT ${PROTOCOL_C}
    COMMAND ${WAYLAND_SCANNER_BIN} private-code ${PROTOCOL_XML} ${PROTOCOL_C}
    DEPENDS ${PROTOCOL_XML}
)

add_custom_command(
    OUTPUT ${PROTOCOL_H}
    COMMAND ${WAYLAND_SCANNER_BIN} client-header ${PROTOCOL_XML} ${PROTOCOL_H}
    DEPENDS ${PROTOCOL_XML}
)

# Source files
set(UK_SOURCES
    src/trayicon.cpp
    src/mainwindow.cpp
    src/macrodialog.cpp
    src/windowtracker.cpp
    src/main.cpp
)

find_program(GO_BIN go REQUIRED)
set(BAMBOO_LIB ${CMAKE_CURRENT_SOURCE_DIR}/src/libbamboo.a)
add_custom_command(
    OUTPUT ${BAMBOO_LIB} ${CMAKE_CURRENT_SOURCE_DIR}/src/libbamboo.h
    COMMAND ${GO_BIN} build -mod=vendor -buildmode=c-archive -o ${BAMBOO_LIB} bamboo_wrapper.go
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/bamboo_wrapper.go
)

add_custom_target(bamboo_target DEPENDS ${BAMBOO_LIB})

add_executable(unikey-wayland 
    ${UK_SOURCES} 
    ${PROTOCOL_C}
    ${PROTOCOL_H}
    resources/resources.qrc
)

add_dependencies(unikey-wayland bamboo_target)

target_include_directories(unikey-wayland PRIVATE 
    src 
    ${CMAKE_CURRENT_BINARY_DIR} 
    include
)

# Add preprocessor definition so it avoids MFC/Windows headers if possible, 
# though we supply our own windows.h
target_compile_definitions(unikey-wayland PRIVATE _LINUX)

target_link_libraries(unikey-wayland PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    ${BAMBOO_LIB}
    pthread
)

install(TARGETS unikey-wayland DESTINATION bin)
