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

# Unix OpenSSL static archives are PIC and link into the plugin. Distro
# libz.a and libzstd.a are not, so zlib and zstd stay shared. Windows uses
# the vcpkg static triplet (libcrypto.lib / zlib.lib built with /MT).
# OPENSSL_USE_STATIC_LIBS would look for libcrypto_static, which vcpkg does
# not install, so it is Unix-only.
if(UNIX AND NOT WIN32)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()

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

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(${target_name} PUBLIC ws2_32)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(${target_name} PUBLIC Threads::Threads)
endif()
