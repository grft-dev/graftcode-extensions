# Graftcode Azure Service Bus plugin (AMQP 1.0 via Azure SDK for C++).
# Standalone project — same idea as rabbitmq/: FetchContent each dependency,
# then link. azure-core and azure-core-amqp are separate FetchContent projects.
set(target_name ServiceBusPlugin)

add_library(${target_name} SHARED
    TransportServiceBus.cpp
    ServiceBusClient.cpp
    ServiceBusServer.cpp
)

target_include_directories(${target_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

include(FetchContent)

# ---------------------------------------------------------------------------
# nlohmann/json
# ---------------------------------------------------------------------------
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

# ---------------------------------------------------------------------------
# Azure SDK build switches (apply before FetchContent of azure-* targets)
# ---------------------------------------------------------------------------
set(FETCH_SOURCE_DEPS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DISABLE_AMQP OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSPORT_CURL ON CACHE BOOL "" FORCE)
set(BUILD_TRANSPORT_WINHTTP ON CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# azure-core (package name consumed by azure-core-amqp: azure-core-cpp)
# Requires CMake >= 3.25 for SOURCE_SUBDIR.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    azure-core
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG azure-core-amqp_1.0.0-beta.11
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR sdk/core/azure-core
)
FetchContent_MakeAvailable(azure-core)

set(azure-core-cpp_DIR "${CMAKE_CURRENT_BINARY_DIR}/azure-core-cpp-config")
file(MAKE_DIRECTORY "${azure-core-cpp_DIR}")
file(WRITE "${azure-core-cpp_DIR}/azure-core-cppConfig.cmake"
    "set(azure-core-cpp_FOUND TRUE)\n"
)
set(azure-core-cpp_DIR "${azure-core-cpp_DIR}" CACHE PATH "" FORCE)

# ---------------------------------------------------------------------------
# azure-core-amqp (find_package(azure-core-cpp) → package above)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    azure-core-amqp
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG azure-core-amqp_1.0.0-beta.11
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR sdk/core/azure-core-amqp
)
FetchContent_MakeAvailable(azure-core-amqp)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
target_compile_definitions(${target_name} PRIVATE ENABLE_UAMQP=1)

target_link_libraries(${target_name} PUBLIC
    Azure::azure-core-amqp
    nlohmann_json::nlohmann_json
)
