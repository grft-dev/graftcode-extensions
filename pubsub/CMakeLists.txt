set(CMAKE_MIN 3.22)
cmake_minimum_required(VERSION ${CMAKE_MIN})
set(CMAKE_POLICY_VERSION_MINIMUM ${CMAKE_MIN})
cmake_policy(VERSION ${CMAKE_MIN})

project("PubSubPlugin" VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)

add_definitions(-DUNICODE)
enable_testing()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_subdirectory("PubSubPlugin")
add_subdirectory("PubSubPluginTest")

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/InstallPluginRuntime.cmake")
graftcode_install_plugin(PubSubPlugin)
