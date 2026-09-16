# Graftcode Azure Service Bus plugin (AMQP 1.0 via Azure SDK for C++).
set(target_name ServiceBusPlugin)

add_library(${target_name} SHARED
    "TransportServiceBus.cpp"
    "ServiceBusClient.cpp"
    "ServiceBusServer.cpp"
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

# Pull Azure SDK sources via CMake (no vcpkg). FETCH_SOURCE_DEPS makes the
# SDK fetch its own C deps; AZURE_SDK_DISABLE_AUTO_VCPKG blocks vcpkg bootstrap.
set(ENV{AZURE_SDK_DISABLE_AUTO_VCPKG} ON)
set(AZURE_SDK_DISABLE_AUTO_VCPKG ON CACHE BOOL "" FORCE)
set(FETCH_SOURCE_DEPS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TRANSPORT_CURL ON CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DISABLE_AMQP OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    azure-sdk-for-cpp
    GIT_REPOSITORY https://github.com/Azure/azure-sdk-for-cpp.git
    GIT_TAG azure-core-amqp_1.0.0-beta.11
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(azure-sdk-for-cpp)
if(NOT azure-sdk-for-cpp_POPULATED)
    FetchContent_Populate(azure-sdk-for-cpp)
    add_subdirectory(
        ${azure-sdk-for-cpp_SOURCE_DIR}/sdk/core/azure-core
        ${azure-sdk-for-cpp_BINARY_DIR}/azure-core
        EXCLUDE_FROM_ALL
    )
    add_subdirectory(
        ${azure-sdk-for-cpp_SOURCE_DIR}/sdk/core/azure-core-amqp
        ${azure-sdk-for-cpp_BINARY_DIR}/azure-core-amqp
        EXCLUDE_FROM_ALL
    )
endif()

# uAMQP backend APIs used by this plugin (CreateMessageSender, MessageSender::Send, ...)
target_compile_definitions(${target_name} PRIVATE ENABLE_UAMQP=1)

target_link_libraries(${target_name} PUBLIC
    Azure::azure-core-amqp
    nlohmann_json::nlohmann_json
)
