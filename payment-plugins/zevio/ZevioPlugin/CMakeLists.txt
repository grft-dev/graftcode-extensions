# payment.zevio - Graftcode Gateway payment provider plugin (GraftcodePluginsInterfaces/PaymentProviderAbi.h).
# The Gateway maps plugin id "payment.zevio" to the library name payment_zevio, so the output name is fixed.
set(target_name payment_zevio)

add_library(${target_name} SHARED
        "ZevioProvider.cpp"
        "WinHttpClient.cpp"
)

target_include_directories(${target_name} PRIVATE
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

target_link_libraries(${target_name} PRIVATE nlohmann_json::nlohmann_json winhttp)
set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "payment_zevio" PREFIX "")
