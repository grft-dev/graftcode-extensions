# Graftcode Kafka plugin (request/reply via librdkafka C++ API).
set(target_name KafkaPlugin)

add_library(${target_name} SHARED
    TransportKafka.cpp
    KafkaClient.cpp
    KafkaServer.cpp
)

target_include_directories(${target_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

set(RDKAFKA_BUILD_STATIC ON CACHE BOOL "Build librdkafka as static library" FORCE)
set(RDKAFKA_BUILD_EXAMPLES OFF CACHE BOOL "Skip librdkafka examples" FORCE)
set(RDKAFKA_BUILD_TESTS OFF CACHE BOOL "Skip librdkafka tests" FORCE)
# Bundled LZ4. External liblz4 is a shared library on Homebrew and some distros.
set(ENABLE_LZ4_EXT OFF CACHE BOOL "Use librdkafka's bundled LZ4" FORCE)
# libcurl would be another shared dependency, and OIDC is unused by this plugin.
set(WITH_CURL OFF CACHE BOOL "Do not link libcurl into librdkafka" FORCE)

# Distro libz.a / libzstd.a are not built PIC, so they cannot be linked into
# this shared plugin. Build both from source with position-independent code
# and hand those targets to librdkafka. OpenSSL on Unix is the distro static
# archive (it is PIC). On Windows, OpenSSL comes from the vcpkg static triplet;
# OPENSSL_USE_STATIC_LIBS would look for libcrypto_static, which vcpkg does not
# ship.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON)
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zlib)
if(TARGET zlib)
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE FILEPATH "" FORCE)

set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
if(MSVC)
    set(ZSTD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(zstd)
set(ZSTD_INCLUDE_DIR "${zstd_SOURCE_DIR}/lib" CACHE PATH "" FORCE)
set(ZSTD_LIBRARY_RELEASE libzstd_static CACHE FILEPATH "" FORCE)
set(ZSTD_LIBRARY_DEBUG libzstd_static CACHE FILEPATH "" FORCE)

if(UNIX AND NOT WIN32)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()

# librdkafka install(EXPORT) rejects zlibstatic because that target is not in
# its export set. Drop EXPORT while configuring librdkafka; the plugin does not
# install those targets.
function(install)
    if(ARGV0 STREQUAL "EXPORT")
        return()
    endif()
    set(_graftcode_skip_next OFF)
    set(_graftcode_kept "")
    foreach(_graftcode_arg IN LISTS ARGV)
        if(_graftcode_skip_next)
            set(_graftcode_skip_next OFF)
            continue()
        endif()
        if(_graftcode_arg STREQUAL "EXPORT")
            set(_graftcode_skip_next ON)
            continue()
        endif()
        list(APPEND _graftcode_kept "${_graftcode_arg}")
    endforeach()
    _install(${_graftcode_kept})
endfunction()

FetchContent_Declare(
    librdkafka
    GIT_REPOSITORY https://github.com/confluentinc/librdkafka.git
    GIT_TAG v2.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(librdkafka)

if(NOT TARGET rdkafka++)
    message(FATAL_ERROR "librdkafka FetchContent succeeded but rdkafka++ target was not found")
endif()

target_link_libraries(${target_name} PUBLIC
    nlohmann_json::nlohmann_json
    rdkafka++
)

if(TARGET rdkafka)
    target_link_libraries(${target_name} PUBLIC rdkafka)
endif()
# librdkafka links zstd as PRIVATE, which a static archive does not forward.
target_link_libraries(${target_name} PRIVATE libzstd_static zlibstatic)
if(UNIX AND NOT APPLE)
    # Keep the static OpenSSL copy inside this .so. libsasl2 loads its own
    # libcrypto.so.3; exporting both sets of symbols from one process breaks TLS.
    target_link_options(${target_name} PRIVATE "LINKER:--exclude-libs,ALL")
endif()

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(${target_name} PUBLIC ws2_32)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(${target_name} PUBLIC Threads::Threads)
endif()
