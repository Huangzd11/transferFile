// 协议错误码常量
// 填入简报/确认的 ErrorCode 字段；平台侧确认后可与 document/04 对齐替换。

#pragma once

namespace transfer {
namespace errc {

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
inline constexpr const char* CrcMismatch = "CRC_MISMATCH";
inline constexpr const char* InvalidSegNo = "INVALID_SEG_NO";
inline constexpr const char* NoSession = "NO_SESSION";

}  // namespace errc
}  // namespace transfer
