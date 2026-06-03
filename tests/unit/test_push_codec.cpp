#include "../test_compat.hpp"

#include "transfer/base64.hpp"
#include "transfer/push_protocol_codec.hpp"

TEST(PushCodecTest, DecodePushBrief) {
    transfer::JsonPushProtocolCodec codec;
    transfer::PushBriefRequest req;
    std::string err;
    const std::string json = R"({
      "Data": {
        "CmdId": "9001",
        "FullPathFileName": "/tmp/push_dst.bin",
        "FileCrc": "0xAABBCCDD",
        "FileSize": "10",
        "ModifyTime": "2026-06-03 10:00:00"
      }
    })";
    ASSERT_TRUE(codec.decodePushBrief(json, req, err));
    EXPECT_EQ(req.cmdId, 9001u);
    EXPECT_EQ(req.fullPathFileName, "/tmp/push_dst.bin");
    EXPECT_EQ(req.fileCrc, "0xAABBCCDD");
    EXPECT_EQ(req.fileSize, 10u);
    EXPECT_EQ(req.modifyTime, "2026-06-03 10:00:00");
}

TEST(PushCodecTest, DecodePushContent) {
    transfer::JsonPushProtocolCodec codec;
    transfer::PushContentSegment seg;
    std::string err;
    std::vector<uint8_t> raw{'A', 'B', 'C'};
    const std::string b64 = transfer::base64Encode(raw);
    const std::string json = R"({"Data":{"CmdId":"1","FileSegNo":"2","Content":")" + b64 +
                             R"(","Continue":"0"}})";
    ASSERT_TRUE(codec.decodePushContent(json, seg, err));
    EXPECT_EQ(seg.cmdId, 1u);
    EXPECT_EQ(seg.fileSegNo, 2u);
    EXPECT_EQ(seg.contentRaw, raw);
    EXPECT_FALSE(seg.continueFlag);
}

TEST(PushCodecTest, EncodePushBriefConfirm) {
    transfer::JsonPushProtocolCodec codec;
    transfer::ProtocolConfirmResponse resp;
    resp.cmdId = 42;
    resp.status = transfer::BriefStatus::Success;
    const std::string json = codec.encodePushBriefConfirm(resp);
    EXPECT_NE(json.find("\"CmdId\":\"42\""), std::string::npos);
    EXPECT_NE(json.find("\"Status\":\"0\""), std::string::npos);
    EXPECT_EQ(json.find("FileSegNo"), std::string::npos);
}

TEST(PushCodecTest, ParseFileCrcHex) {
    uint32_t v = 0;
    EXPECT_TRUE(transfer::parseFileCrcHex("0xCBF43926", v));
    EXPECT_EQ(v, 0xCBF43926U);
    EXPECT_TRUE(transfer::parseFileCrcHex("CBF43926", v));
    EXPECT_EQ(v, 0xCBF43926U);
}
