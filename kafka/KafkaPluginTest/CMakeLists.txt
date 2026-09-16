add_executable(KafkaPluginTest smoke.cpp)
target_link_libraries(KafkaPluginTest PRIVATE KafkaPlugin)
target_include_directories(KafkaPluginTest PRIVATE
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)
add_test(NAME KafkaPluginSmoke COMMAND KafkaPluginTest)
