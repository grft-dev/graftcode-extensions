include(FetchContent)

add_executable(SqsPluginTests
    "SqsTransportTests.cpp"
    "${CMAKE_SOURCE_DIR}/SqsPlugin/SqsConfig.cpp"
    "${CMAKE_SOURCE_DIR}/SqsPlugin/SqsUtil.cpp"
    "${CMAKE_SOURCE_DIR}/SqsPlugin/SqsClient.cpp"
    "${CMAKE_SOURCE_DIR}/SqsPlugin/TransportSqs.cpp"
    "${CMAKE_SOURCE_DIR}/SqsPlugin/SqsServer.cpp"
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(aws-cpp-sdk-core CONFIG REQUIRED)
find_package(aws-cpp-sdk-sqs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(SqsPluginTests PRIVATE
    "${CMAKE_SOURCE_DIR}/SqsPlugin"
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

find_package(Threads REQUIRED)

target_link_libraries(SqsPluginTests PRIVATE
    gtest_main
    aws-cpp-sdk-sqs
    aws-cpp-sdk-core
    nlohmann_json::nlohmann_json
    Threads::Threads
)

add_executable(SqsPluginSmoke smoke.cpp)
target_link_libraries(SqsPluginSmoke PRIVATE SqsPlugin)
target_include_directories(SqsPluginSmoke PRIVATE
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)
add_test(NAME SqsPluginSmoke COMMAND SqsPluginSmoke)

include(GoogleTest)
gtest_discover_tests(SqsPluginTests)
