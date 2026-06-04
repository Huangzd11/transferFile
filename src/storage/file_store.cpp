// 本地文件存储实现（Linux POSIX）
// 路径规范化与白名单；O_RDONLY 上传读、O_TRUNC 推送写。

#include "transfer/file_store.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace transfer {

FileHandle::FileHandle(int fd) : fd_(fd) {}

FileHandle::FileHandle(FileHandle&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FileHandle& FileHandle::operator=(FileHandle&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

FileHandle::~FileHandle() {
    if (fd_ >= 0) ::close(fd_);
}

FileStore::FileStore(std::vector<std::string> allowedRoots)
    : allowedRoots_(std::move(allowedRoots)) {
    for (auto& r : allowedRoots_) {
        r = normalizePath(r);
        if (r != "/" && !r.empty() && r.back() != '/') r += '/';
    }
}

// 折叠重复 '/'，去除简单 ./ 段
std::string FileStore::normalizePath(const std::string& path) {
    if (path.empty()) return path;
    std::string out;
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!out.empty() && out.back() == '/') continue;
            if (out.empty() && i > 0) continue;
            out += '/';
        } else if (path[i] == '.' && i + 1 < path.size() && path[i + 1] == '/') {
            continue;
        } else {
            out += path[i];
        }
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out.empty() ? "/" : out;
}

FileOpenError FileStore::validatePath(const std::string& fullPath) const {
    std::string norm = normalizePath(fullPath);
    if (norm.find("..") != std::string::npos) return FileOpenError::InvalidPath;
    for (const auto& root : allowedRoots_) {
        if (norm == root.substr(0, root.size() - 1) ||
            (norm.size() >= root.size() &&
             norm.compare(0, root.size(), root) == 0)) {
            return FileOpenError::Ok;
        }
    }
    return FileOpenError::InvalidPath;
}

FileOpenError FileStore::openReadOnly(const std::string& fullPath, FileHandle& out) {
    if (validatePath(fullPath) != FileOpenError::Ok) return FileOpenError::InvalidPath;
    int fd = ::open(fullPath.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) return FileOpenError::NotFound;
        if (errno == EACCES) return FileOpenError::PermissionDenied;
        return FileOpenError::IoError;
    }
    out = FileHandle(fd);
    return FileOpenError::Ok;
}

FileOpenError FileStore::getSize(const FileHandle& handle, uint64_t& outSize) const {
    if (!handle.valid()) return FileOpenError::IoError;
    struct stat st {};
    if (::fstat(handle.fd(), &st) != 0) return FileOpenError::IoError;
    outSize = static_cast<uint64_t>(st.st_size);
    return FileOpenError::Ok;
}

FileOpenError FileStore::getModifyTime(const FileHandle& handle,
                                     std::string& outTime) const {
    if (!handle.valid()) return FileOpenError::IoError;
    struct stat st {};
    if (::fstat(handle.fd(), &st) != 0) return FileOpenError::IoError;
    std::time_t t = st.st_mtime;
    std::tm tm_buf {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream os;
    os << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    outTime = os.str();
    return FileOpenError::Ok;
}

FileOpenError FileStore::readAt(const FileHandle& handle, uint64_t offset,
                                uint8_t* buffer, size_t bufferSize,
                                size_t& outRead) const {
    outRead = 0;
    if (!handle.valid() || bufferSize == 0) return FileOpenError::IoError;
    if (offset > static_cast<uint64_t>(LLONG_MAX)) return FileOpenError::IoError;
    off_t off = static_cast<off_t>(offset);
    if (::lseek(handle.fd(), off, SEEK_SET) < 0) return FileOpenError::IoError;
    ssize_t n = ::read(handle.fd(), buffer, bufferSize);
    if (n < 0) return FileOpenError::IoError;
    outRead = static_cast<size_t>(n);
    return FileOpenError::Ok;
}

FileOpenError FileStore::openWriteCreate(const std::string& fullPath, FileHandle& out) {
    if (validatePath(fullPath) != FileOpenError::Ok) return FileOpenError::InvalidPath;
    int fd = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (errno == EACCES) return FileOpenError::PermissionDenied;
        return FileOpenError::IoError;
    }
    out = FileHandle(fd);
    return FileOpenError::Ok;
}

FileOpenError FileStore::writeAt(const FileHandle& handle, uint64_t offset,
                                 const uint8_t* buffer, size_t bufferSize,
                                 size_t& outWritten) {
    outWritten = 0;
    if (!handle.valid() || bufferSize == 0) return FileOpenError::IoError;
    if (offset > static_cast<uint64_t>(LLONG_MAX)) return FileOpenError::IoError;
    off_t off = static_cast<off_t>(offset);
    if (::lseek(handle.fd(), off, SEEK_SET) < 0) return FileOpenError::IoError;
    ssize_t n = ::write(handle.fd(), buffer, bufferSize);
    if (n < 0) return FileOpenError::IoError;
    outWritten = static_cast<size_t>(n);
    return FileOpenError::Ok;
}

void FileStore::close(FileHandle& handle) { handle = FileHandle(); }

}  // namespace transfer
