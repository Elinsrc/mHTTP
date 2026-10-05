cmake_minimum_required(VERSION 3.16)
project(mHTTP C)

set(CMAKE_C_STANDARD 11)

option(MHTTP_BUILD_CLI "Build the mHTTP command line tool" ON)

message(STATUS "mHTTP: build CLI: ${MHTTP_BUILD_CLI}")

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES ON CACHE BOOL "" FORCE) 
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)

add_subdirectory(external/mbedtls)

add_library(mhttp
    src/platform.c
    src/util.c
    src/url.c
    src/conn.c
    src/tls.c
    src/ca.c
    src/http.c
    src/mhttp.c)

target_include_directories(mhttp PUBLIC include PRIVATE src)
target_link_libraries(mhttp PUBLIC mbedtls mbedx509 mbedcrypto)
if(WIN32)
    target_link_libraries(mhttp PUBLIC ws2_32 crypt32)
endif()

if(MHTTP_BUILD_CLI)
    add_executable(mhttp-cli 
    cli/main.c
    cli/cli.c
    cli/fmt.c
    cli/outname.c
    cli/sighandler.c
    cli/term.c
    cli/transfer.c)
    target_link_libraries(mhttp-cli PRIVATE mhttp)
endif()
