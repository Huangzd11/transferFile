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

FileHandle::FileHandle(int fd) : fd_(fd) {}    // 构造函数

FileHandle::FileHandle(FileHandle&& other) noexcept : fd_(other.fd_) {    // 移动构造函数
    other.fd_ = -1;
}   

FileHandle& FileHandle::operator=(FileHandle&& other) noexcept {    // 移动赋值运算符
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);    // 关闭文件描述符
        fd_ = other.fd_;
        other.fd_ = -1;    // 设置其他文件描述符为-1
    }
    return *this;
}

FileHandle::~FileHandle() {    // 析构函数
    if (fd_ >= 0) ::close(fd_);    // 关闭文件描述符
}

FileStore::FileStore(std::vector<std::string> allowedRoots)
    : allowedRoots_(std::move(allowedRoots)) {    // 构造函数
    for (auto& r : allowedRoots_) {
        r = normalizePath(r);    // 规范化路径
        if (r != "/" && !r.empty() && r.back() != '/') r += '/';
    }    // 添加斜杠
}

// 折叠重复 '/'，去除简单 ./ 段
std::string FileStore::normalizePath(const std::string& path) {    // 规范化路径
    if (path.empty()) return path;
    std::string out;    // 输出路径
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!out.empty() && out.back() == '/') continue;    // 如果输出路径不为空且最后一个字符为/，则跳过
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
    std::string norm = normalizePath(fullPath);    // 规范化路径
    if (norm.find("..") != std::string::npos) return FileOpenError::InvalidPath;    // 如果路径中包含..，则返回无效路径
    for (const auto& root : allowedRoots_) {
        if (norm == root.substr(0, root.size() - 1) ||
            (norm.size() >= root.size() &&
             norm.compare(0, root.size(), root) == 0)) {
            return FileOpenError::Ok;    // 返回有效路径
        }
    }
    return FileOpenError::InvalidPath;    // 返回无效路径
}

FileOpenError FileStore::openReadOnly(const std::string& fullPath, FileHandle& out) {    // 打开只读文件
    if (validatePath(fullPath) != FileOpenError::Ok) return FileOpenError::InvalidPath;
    int fd = ::open(fullPath.c_str(), O_RDONLY);    // 打开文件
    if (fd < 0) {
        if (errno == ENOENT) return FileOpenError::NotFound;    // 如果文件不存在，则返回文件不存在
        if (errno == EACCES) return FileOpenError::PermissionDenied;    // 如果权限不足，则返回权限不足
        return FileOpenError::IoError;    // 如果读取失败，则返回读取失败
    }
    out = FileHandle(fd);    // 设置文件句柄
    return FileOpenError::Ok;
}

FileOpenError FileStore::getSize(const FileHandle& handle, uint64_t& outSize) const {    // 获取文件大小
    if (!handle.valid()) return FileOpenError::IoError;
    struct stat st {};
    if (::fstat(handle.fd(), &st) != 0) return FileOpenError::IoError;    // 如果获取失败，则返回读取失败
    outSize = static_cast<uint64_t>(st.st_size);    // 设置文件大小
    return FileOpenError::Ok;
}

FileOpenError FileStore::getModifyTime(const FileHandle& handle,    // 获取修改时间 
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
                                size_t& outRead) const {    // 读取文件
    outRead = 0;
    if (!handle.valid() || bufferSize == 0) return FileOpenError::IoError;
    if (offset > static_cast<uint64_t>(LLONG_MAX)) return FileOpenError::IoError;    // 如果偏移量大于最大值，则返回读取失败
    off_t off = static_cast<off_t>(offset);
    if (::lseek(handle.fd(), off, SEEK_SET) < 0) return FileOpenError::IoError;    // 如果设置偏移量失败，则返回读取失败
    ssize_t n = ::read(handle.fd(), buffer, bufferSize);
    if (n < 0) return FileOpenError::IoError;
    outRead = static_cast<size_t>(n);    // 设置读取字节数
    return FileOpenError::Ok;
}

FileOpenError FileStore::openWriteCreate(const std::string& fullPath, FileHandle& out) {    // 打开写入文件
    if (validatePath(fullPath) != FileOpenError::Ok) return FileOpenError::InvalidPath;
    int fd = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (errno == EACCES) return FileOpenError::PermissionDenied;    // 如果权限不足，则返回权限不足
        return FileOpenError::IoError;
    }
    out = FileHandle(fd);    // 设置文件句柄
    return FileOpenError::Ok;
}

FileOpenError FileStore::writeAt(const FileHandle& handle, uint64_t offset,    // 写入文件
                                 const uint8_t* buffer, size_t bufferSize,
                                 size_t& outWritten) {
    outWritten = 0;    // 设置写入字节数
    if (!handle.valid() || bufferSize == 0) return FileOpenError::IoError;
    if (offset > static_cast<uint64_t>(LLONG_MAX)) return FileOpenError::IoError;    // 如果偏移量大于最大值，则返回读取失败
    off_t off = static_cast<off_t>(offset);
    if (::lseek(handle.fd(), off, SEEK_SET) < 0) return FileOpenError::IoError;    // 如果设置偏移量失败，则返回读取失败
    ssize_t n = ::write(handle.fd(), buffer, bufferSize);
    if (n < 0) return FileOpenError::IoError;    // 如果写入失败，则返回读取失败
    outWritten = static_cast<size_t>(n);
    return FileOpenError::Ok;
}

void FileStore::close(FileHandle& handle) { handle = FileHandle(); }

}  // namespace transfer
