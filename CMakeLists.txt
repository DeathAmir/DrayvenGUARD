cmake_minimum_required(VERSION 3.20)
project(DrayvenGUARD VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
    samp_plugin_sdk
    GIT_REPOSITORY https://github.com/udan11/samp-plugin-sdk.git
    GIT_TAG a425b5b3d9f3dc1fe1a3306ee92996ec103d7e28
    GIT_SHALLOW FALSE
)

FetchContent_GetProperties(samp_plugin_sdk)
if(NOT samp_plugin_sdk_POPULATED)
    FetchContent_Populate(samp_plugin_sdk)
endif()

add_library(drayvenguard SHARED src/drayvenguard.cpp)

target_include_directories(drayvenguard PRIVATE
    "${samp_plugin_sdk_SOURCE_DIR}"
    "${samp_plugin_sdk_SOURCE_DIR}/amx"
)

target_compile_features(drayvenguard PRIVATE cxx_std_17)

if(WIN32)
    target_compile_definitions(drayvenguard PRIVATE WIN32 _CRT_SECURE_NO_WARNINGS)
    target_sources(drayvenguard PRIVATE src/drayvenguard.def)
    set_target_properties(drayvenguard PROPERTIES PREFIX "" OUTPUT_NAME "drayvenguard")
else()
    target_compile_definitions(drayvenguard PRIVATE LINUX)
    target_compile_options(drayvenguard PRIVATE -fvisibility=hidden -Wall -Wextra -Wpedantic)
    set_target_properties(drayvenguard PROPERTIES PREFIX "" OUTPUT_NAME "drayvenguard")
endif()

set_target_properties(drayvenguard PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(drayvenguard PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin"
        ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib"
    )
endforeach()
