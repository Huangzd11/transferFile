// 本地文件存储抽象
// 路径白名单校验、只读上传与写创建推送落盘，基于 POSIX 文件描述符。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace transfer {

enum class FileOpenError {
    Ok,
    NotFound,
    PermissionDenied,
    InvalidPath,
    IoError
};

// 只读文件句柄
class FileHandle {
public:
    FileHandle() = default;
    explicit FileHandle(int fd);
    bool valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&& other) noexcept;
    FileHandle& operator=(FileHandle&& other) noexcept;
    ~FileHandle();

private:
    int fd_ = -1;
};

class IFileStore {
public:
    virtual ~IFileStore() = default;

    virtual FileOpenError validatePath(const std::string& fullPath) const = 0;
    virtual FileOpenError openReadOnly(const std::string& fullPath,
                                       FileHandle& out) = 0;
    virtual FileOpenError getSize(const FileHandle& handle,
                                  uint64_t& outSize) const = 0;
    virtual FileOpenError getModifyTime(const FileHandle& handle,
                                        std::string& outTime) const = 0;
    virtual FileOpenError readAt(const FileHandle& handle, uint64_t offset,
                                 uint8_t* buffer, size_t bufferSize,
                                 size_t& outRead) const = 0;
    virtual FileOpenError openWriteCreate(const std::string& fullPath,
                                          FileHandle& out) = 0;
    virtual FileOpenError writeAt(const FileHandle& handle, uint64_t offset,
                                  const uint8_t* buffer, size_t bufferSize,
                                  size_t& outWritten) = 0;
    virtual void close(FileHandle& handle) = 0;
};

class FileStore : public IFileStore {
public:
    explicit FileStore(std::vector<std::string> allowedRoots);

    FileOpenError validatePath(const std::string& fullPath) const override;
    FileOpenError openReadOnly(const std::string& fullPath,
                               FileHandle& out) override;
    FileOpenError getSize(const FileHandle& handle,
                          uint64_t& outSize) const override;
    FileOpenError getModifyTime(const FileHandle& handle,
                                std::string& outTime) const override;
    FileOpenError readAt(const FileHandle& handle, uint64_t offset,
                         uint8_t* buffer, size_t bufferSize,
                         size_t& outRead) const override;
    FileOpenError openWriteCreate(const std::string& fullPath,
                                  FileHandle& out) override;
    FileOpenError writeAt(const FileHandle& handle, uint64_t offset,
                          const uint8_t* buffer, size_t bufferSize,
                          size_t& outWritten) override;
    void close(FileHandle& handle) override;

private:
    std::vector<std::string> allowedRoots_;
    static std::string normalizePath(const std::string& path);
};

}  // namespace transfer
