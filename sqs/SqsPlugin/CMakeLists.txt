set(target_name SqsPlugin)

add_library(${target_name} SHARED
    "SqsConfig.cpp"
    "SqsUtil.cpp"
    "SqsClient.cpp"
    "TransportSqs.cpp"
    "SqsServer.cpp"
)

find_package(aws-cpp-sdk-sqs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(${target_name} PUBLIC
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)

target_link_libraries(${target_name} PUBLIC
    aws-cpp-sdk-sqs
    nlohmann_json::nlohmann_json
)
