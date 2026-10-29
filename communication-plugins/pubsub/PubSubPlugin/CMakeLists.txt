set(target_name PubSubPlugin)

add_library(${target_name} SHARED
    "PubSubConfig.cpp"
    "PubSubUtil.cpp"
    "PubSubClient.cpp"
    "TransportPubSub.cpp"
    "PubSubServer.cpp"
)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

target_include_directories(${target_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

target_link_libraries(${target_name} PUBLIC
    CURL::libcurl
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
)

get_target_property(_curl_type CURL::libcurl TYPE)
if(_curl_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(${target_name} PRIVATE CURL_STATICLIB)
endif()
