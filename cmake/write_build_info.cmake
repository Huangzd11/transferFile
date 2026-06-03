# 生成 transfer/build_info.hpp（版本号 + 编译时间戳）
# 由 CMakeLists 的 transfer_refresh_build_info（ALL）在每次构建时调用
if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE 未指定")
endif()
if(NOT DEFINED PROJECT_VERSION)
    set(PROJECT_VERSION "0.0.0")
endif()

# Linux date 支持毫秒 %3N；失败则退化为秒 + .000
execute_process(
    COMMAND date "+%Y-%m-%d %H:%M:%S.%3N"
    OUTPUT_VARIABLE BUILD_DATETIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE DATE_RC
    ERROR_QUIET
)
if(DATE_RC OR NOT BUILD_DATETIME)
    string(TIMESTAMP BUILD_DATETIME_SEC "%Y-%m-%d %H:%M:%S")
    set(BUILD_DATETIME "${BUILD_DATETIME_SEC}.000")
endif()

file(WRITE "${OUTPUT_FILE}"
"// 本文件由 CMake 在编译前自动生成，请勿手改\n"
"#pragma once\n"
"\n"
"namespace transfer {\n"
"\n"
"inline constexpr const char* kVersionString = \"${PROJECT_VERSION}\";\n"
"inline constexpr const char* kBuildDateTime = \"${BUILD_DATETIME}\";\n"
"\n"
"}  // namespace transfer\n"
)
