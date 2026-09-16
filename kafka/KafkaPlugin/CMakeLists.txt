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

# ---------------------------------------------------------------------------
# nlohmann/json
# ---------------------------------------------------------------------------
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

# ---------------------------------------------------------------------------
# librdkafka (rdkafka++)
#
# Preferred resolution order:
#   1) find_package(RdKafka) — system install or vcpkg toolchain
#   2) FetchContent from confluentinc/librdkafka (edenhill redirects here)
#
# Override the FetchContent tag with -DKAFKA_LIBRDKAFKA_GIT_TAG=vX.Y.Z
# Skip FetchContent with -DKAFKA_USE_SYSTEM_RDKAFKA=ON (requires find_package).
# ---------------------------------------------------------------------------
set(KAFKA_LIBRDKAFKA_GIT_TAG "v2.8.0" CACHE STRING "librdkafka git tag for FetchContent")
option(KAFKA_USE_SYSTEM_RDKAFKA "Require system/vcpkg RdKafka; do not FetchContent" OFF)

find_package(RdKafka CONFIG QUIET)
set(_kafka_rdkafka_target "")

if(RdKafka_FOUND)
    if(TARGET RdKafka::rdkafka++)
        set(_kafka_rdkafka_target RdKafka::rdkafka++)
    elseif(TARGET rdkafka++)
        set(_kafka_rdkafka_target rdkafka++)
    endif()
endif()

if(NOT _kafka_rdkafka_target)
    if(KAFKA_USE_SYSTEM_RDKAFKA)
        message(FATAL_ERROR
            "KAFKA_USE_SYSTEM_RDKAFKA=ON but find_package(RdKafka) failed. "
            "Install librdkafka or configure with the vcpkg toolchain.")
    endif()

    message(STATUS "RdKafka not found via find_package; FetchContent ${KAFKA_LIBRDKAFKA_GIT_TAG}")

    # Prefer a static librdkafka so the shared plugin is easier to drop in.
    set(RDKAFKA_BUILD_STATIC ON CACHE BOOL "Build librdkafka as static library" FORCE)
    set(RDKAFKA_BUILD_EXAMPLES OFF CACHE BOOL "Skip librdkafka examples" FORCE)
    set(RDKAFKA_BUILD_TESTS OFF CACHE BOOL "Skip librdkafka tests" FORCE)
    set(WITH_SSL ON CACHE BOOL "Enable SSL in librdkafka" FORCE)

    FetchContent_Declare(
        librdkafka
        GIT_REPOSITORY https://github.com/confluentinc/librdkafka.git
        GIT_TAG ${KAFKA_LIBRDKAFKA_GIT_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(librdkafka)

    if(TARGET rdkafka++)
        set(_kafka_rdkafka_target rdkafka++)
    elseif(TARGET RdKafka::rdkafka++)
        set(_kafka_rdkafka_target RdKafka::rdkafka++)
    else()
        message(FATAL_ERROR "librdkafka FetchContent succeeded but rdkafka++ target is missing")
    endif()
endif()

target_link_libraries(${target_name} PUBLIC
    nlohmann_json::nlohmann_json
    ${_kafka_rdkafka_target}
)

# Ensure consumers see librdkafka headers when linking a plain target name.
if(TARGET rdkafka)
    target_link_libraries(${target_name} PUBLIC rdkafka)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(${target_name} PUBLIC Threads::Threads)
endif()
