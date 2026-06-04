// 文件存储单测：路径白名单、读写、错误码

#include "../test_compat.hpp"

#include "transfer/file_store.hpp"

#include <fstream>
#include <string>

static std::string writeTempFile() {
    const std::string path = "/tmp/transfer_test_file.bin";
    std::ofstream out(path, std::ios::binary);
    out << "ABCDEF";
    return path;
}

TEST(FileStoreTest, ReadFile) {
    const std::string path = writeTempFile();
    transfer::FileStore store({"/tmp/"});
    EXPECT_EQ(store.validatePath(path), transfer::FileOpenError::Ok);
    transfer::FileHandle h;
    ASSERT_TRUE(store.openReadOnly(path, h) == transfer::FileOpenError::Ok);
    uint64_t size = 0;
    ASSERT_TRUE(store.getSize(h, size) == transfer::FileOpenError::Ok);
    EXPECT_EQ(size, 6u);
    uint8_t buf[4] = {};
    size_t n = 0;
    ASSERT_TRUE(store.readAt(h, 2, buf, 4, n) == transfer::FileOpenError::Ok);
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(buf[0], static_cast<uint8_t>('C'));
    store.close(h);
}

TEST(FileStoreTest, RejectPathOutsideRoot) {
    transfer::FileStore store({"/tmp/"});
    EXPECT_EQ(store.validatePath("/etc/passwd"), transfer::FileOpenError::InvalidPath);
}
