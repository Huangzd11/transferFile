# 第三方依赖

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(TRANSFER_WITH_MOSQUITTO "链接 libmosquitto 以支持真实 MQTT" ON)

if(TRANSFER_WITH_MOSQUITTO)
    find_package(Mosquitto)
    if(Mosquitto_FOUND)
        message(STATUS "libmosquitto: ${MOSQUITTO_LIBRARIES}")
        set(TRANSFER_MOSQUITTO_ENABLED ON)
    else()
        if(CMAKE_CROSSCOMPILING)
            message(FATAL_ERROR
                "交叉编译需要 libmosquitto，请先执行: ./scripts/build-mosquitto-cross.sh")
        else()
            message(WARNING "未找到 libmosquitto，真实 MQTT 不可用（仅模拟总线）")
        endif()
        set(TRANSFER_MOSQUITTO_ENABLED OFF)
    endif()
else()
    set(TRANSFER_MOSQUITTO_ENABLED OFF)
endif()

if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        set(TRANSFER_USE_GTEST ON)
        message(STATUS "Using system GTest")
    else()
        set(TRANSFER_USE_GTEST OFF)
        message(STATUS "GTest not found; using minimal_test harness")
    endif()
endif()
