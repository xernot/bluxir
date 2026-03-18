cmake_minimum_required(VERSION 3.16)
project(bluxir C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)

pkg_check_modules(NCURSESW REQUIRED ncursesw)
pkg_check_modules(EXPAT REQUIRED expat)
pkg_check_modules(AVAHI_CLIENT REQUIRED avahi-client)

# Sources
set(SOURCES
    src/main.c
    src/config.c
    src/player.c
    src/metadata.c
    src/cache.c
    src/discover.c
    src/cover_art.c
    src/ui.c
    src/ui_player.c
    src/ui_browse.c
    src/ui_search.c
    src/logger.c
    src/util.c
    lib/cJSON.c
)

add_executable(bluxir ${SOURCES})

target_include_directories(bluxir PRIVATE
    ${NCURSESW_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${EXPAT_INCLUDE_DIRS}
    ${AVAHI_CLIENT_INCLUDE_DIRS}
)

target_link_libraries(bluxir
    ${NCURSESW_LIBRARIES}
    ${CURL_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${AVAHI_CLIENT_LIBRARIES}
    pthread
    m
)
