cmake_minimum_required(VERSION 3.14)
project(transferFile VERSION 0.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build unit tests" ON)

include(cmake/Dependencies.cmake)

# 编译前生成版本与编译时间（YYYY-MM-DD HH:MM:SS.mmm）
set(TRANSFER_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(TRANSFER_BUILD_INFO_HPP "${TRANSFER_GENERATED_DIR}/transfer/build_info.hpp")
add_custom_command(
    OUTPUT "${TRANSFER_BUILD_INFO_HPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSFER_GENERATED_DIR}/transfer"
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_FILE=${TRANSFER_BUILD_INFO_HPP}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_build_info.cmake
    COMMENT "生成 build_info.hpp（版本与编译时间）"
    VERBATIM
)

if(BUILD_TESTING AND NOT TRANSFER_USE_GTEST)
    # 无系统 GTest 时由 minimal_test 提供测试入口
endif()

add_library(transfer_core
    ${TRANSFER_BUILD_INFO_HPP}
    src/protocol/base64.cpp
    src/protocol/crc32.cpp
    src/protocol/json_codec.cpp
    src/protocol/json_push_codec.cpp
    src/storage/file_store.cpp
    src/session/memory_session_store.cpp
    src/session/memory_push_session_store.cpp
    src/app/timeout_watchdog.cpp
    src/app/runtime_log.cpp
    src/app/mqtt_publisher.cpp
    src/app/transfer_orchestrator.cpp
    src/app/push_receive_orchestrator.cpp
    src/mqtt/mqtt_config.cpp
    src/mqtt/simulated_mqtt_bus.cpp
    src/mqtt/mqtt_adapter.cpp
    src/mqtt/mosquitto_mqtt_adapter.cpp
    src/mqtt/gateway_mqtt_factory.cpp
    src/config/app_config.cpp
    src/config/config_loader.cpp
)

target_include_directories(transfer_core
    PUBLIC
        ${TRANSFER_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_source_files_properties(${TRANSFER_BUILD_INFO_HPP} PROPERTIES GENERATED TRUE)

target_compile_options(transfer_core PRIVATE -Wall -Wextra)

if(TRANSFER_MOSQUITTO_ENABLED)
    target_compile_definitions(transfer_core PUBLIC TRANSFER_WITH_MOSQUITTO)
    target_include_directories(transfer_core PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(transfer_core PRIVATE ${MOSQUITTO_LIBRARIES} pthread)
endif()

add_executable(transferFile src/main.cpp)
target_link_libraries(transferFile PRIVATE transfer_core)

# 本机平台模拟程序（仅主机编译）
if(NOT CMAKE_CROSSCOMPILING AND TRANSFER_MOSQUITTO_ENABLED)
    add_executable(platform_sim src/tools/platform_sim_main.cpp)
    target_link_libraries(platform_sim PRIVATE transfer_core ${MOSQUITTO_LIBRARIES} pthread)
    target_include_directories(platform_sim PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_compile_definitions(platform_sim PRIVATE TRANSFER_WITH_MOSQUITTO)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
