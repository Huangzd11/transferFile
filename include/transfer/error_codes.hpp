#pragma once

namespace transfer {
namespace errc {

// 占位错误码（平台侧确认后统一替换，见 document/04-通信协议.md）
inline constexpr const char* Ok = "OK";
inline constexpr const char* BadFrame = "BAD_FRAME";
inline constexpr const char* InvalidCmdId = "INVALID_CMD_ID";
inline constexpr const char* InvalidStartByte = "INVALID_START_BYTE";
inline constexpr const char* FileNotFound = "FILE_NOT_FOUND";
inline constexpr const char* PermissionDenied = "PERMISSION_DENIED";
inline constexpr const char* InvalidPath = "INVALID_PATH";
inline constexpr const char* Busy = "BUSY";
inline constexpr const char* IoError = "IO_ERROR";
inline constexpr const char* FileChanged = "FILE_CHANGED";

}  // namespace errc
}  // namespace transfer
