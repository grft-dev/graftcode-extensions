set(CMAKE_MIN 3.22)
cmake_minimum_required (VERSION ${CMAKE_MIN})
set(CMAKE_POLICY_VERSION_MINIMUM ${CMAKE_MIN})
cmake_policy(VERSION ${CMAKE_MIN})

set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
# Same static-runtime rules as the communication plugins (/MT on Windows).
include("${CMAKE_CURRENT_SOURCE_DIR}/../../communication-plugins/cmake/StaticPluginRuntime.cmake")
graftcode_prepare_static_runtime()

project ("ZevioPaymentPlugin" VERSION 0.1.0 LANGUAGES CXX)

graftcode_apply_static_crt()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DUNICODE -D_UNICODE)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory ("ZevioPlugin")

include("${CMAKE_CURRENT_SOURCE_DIR}/../../communication-plugins/cmake/InstallPluginRuntime.cmake")
graftcode_install_plugin(payment_zevio)
