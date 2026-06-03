if(TRANSFER_USE_GTEST)
    add_executable(transfer_tests
        unit/test_build_info.cpp
        unit/test_base64.cpp
        unit/test_crc32.cpp
        unit/test_json_codec.cpp
        unit/test_file_store.cpp
        unit/test_orchestrator.cpp
        unit/test_orchestrator_v002.cpp
        unit/test_timeout.cpp
        unit/test_mqtt_simulation.cpp
        unit/test_config_loader.cpp
        unit/test_runtime_log.cpp
        unit/test_push_codec.cpp
        unit/test_push_orchestrator.cpp
        unit/test_push_e2e.cpp
    )
    target_link_libraries(transfer_tests PRIVATE transfer_core GTest::gtest_main)
    add_dependencies(transfer_tests transfer_refresh_build_info)
    include(GoogleTest)
    gtest_discover_tests(transfer_tests)
else()
    add_executable(transfer_tests
        test_main.cpp
        unit/test_build_info.cpp
        unit/test_base64.cpp
        unit/test_crc32.cpp
        unit/test_json_codec.cpp
        unit/test_file_store.cpp
        unit/test_orchestrator.cpp
        unit/test_orchestrator_v002.cpp
        unit/test_timeout.cpp
        unit/test_mqtt_simulation.cpp
        unit/test_config_loader.cpp
        unit/test_runtime_log.cpp
        unit/test_push_codec.cpp
        unit/test_push_orchestrator.cpp
        unit/test_push_e2e.cpp
    )
    target_include_directories(transfer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(transfer_tests PRIVATE transfer_core)
    add_dependencies(transfer_tests transfer_refresh_build_info)
    add_test(NAME transfer_tests COMMAND transfer_tests)
endif()
