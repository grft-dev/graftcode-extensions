include(FetchContent)

add_executable(PubSubPluginTests
    "PubSubTransportTests.cpp"
    "${CMAKE_SOURCE_DIR}/PubSubPlugin/PubSubConfig.cpp"
    "${CMAKE_SOURCE_DIR}/PubSubPlugin/PubSubUtil.cpp"
    "${CMAKE_SOURCE_DIR}/PubSubPlugin/PubSubClient.cpp"
    "${CMAKE_SOURCE_DIR}/PubSubPlugin/TransportPubSub.cpp"
    "${CMAKE_SOURCE_DIR}/PubSubPlugin/PubSubServer.cpp"
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

target_include_directories(PubSubPluginTests PRIVATE
    "${CMAKE_SOURCE_DIR}/PubSubPlugin"
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

target_link_libraries(PubSubPluginTests PRIVATE
    gtest_main
    CURL::libcurl
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
)

get_target_property(_curl_type CURL::libcurl TYPE)
if(_curl_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(PubSubPluginTests PRIVATE CURL_STATICLIB)
endif()

add_executable(PubSubPluginSmoke smoke.cpp)
target_link_libraries(PubSubPluginSmoke PRIVATE PubSubPlugin)
target_include_directories(PubSubPluginSmoke PRIVATE
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)
add_test(NAME PubSubPluginSmoke COMMAND PubSubPluginSmoke)
set_tests_properties(PubSubPluginSmoke PROPERTIES TIMEOUT 60)

# Windows has no rpath. Copy the plugin and curl/OpenSSL DLLs next to each
# test executable so the loader does not block on a missing-DLL dialog.
if(WIN32)
    foreach(pubsub_test_target PubSubPluginSmoke PubSubPluginTests)
        add_custom_command(TARGET ${pubsub_test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DDEST=$<TARGET_FILE_DIR:${pubsub_test_target}>"
                "-DDLL_LIST=$<JOIN:$<TARGET_RUNTIME_DLLS:${pubsub_test_target}>,|>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_runtime_dlls.cmake"
            VERBATIM)
    endforeach()
endif()

include(GoogleTest)
gtest_discover_tests(PubSubPluginTests DISCOVERY_TIMEOUT 60)
