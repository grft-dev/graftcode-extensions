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
