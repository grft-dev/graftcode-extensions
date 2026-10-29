add_executable(KafkaPluginTest smoke.cpp)
target_link_libraries(KafkaPluginTest PRIVATE KafkaPlugin)
target_include_directories(KafkaPluginTest PRIVATE
    "${CMAKE_SOURCE_DIR}/GraftcodePluginsInterfaces"
)
add_test(NAME KafkaPluginSmoke COMMAND KafkaPluginTest)
set_tests_properties(KafkaPluginSmoke PROPERTIES TIMEOUT 60)

# Windows has no rpath. ctest must find KafkaPlugin.dll beside the test
# executable, or the loader can block on a missing-DLL dialog.
if(WIN32)
    add_custom_command(TARGET KafkaPluginTest POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DDEST=$<TARGET_FILE_DIR:KafkaPluginTest>"
            "-DDLL_LIST=$<JOIN:$<TARGET_RUNTIME_DLLS:KafkaPluginTest>,|>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_runtime_dlls.cmake"
        VERBATIM)
endif()
