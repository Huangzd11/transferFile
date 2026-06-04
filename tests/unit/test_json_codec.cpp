// 召唤协议 JSON 编解码单测

#include "transfer/protocol_codec.hpp"

#include "../test_compat.hpp"

TEST(JsonCodecTest, DecodeSummonOk) {
    transfer::JsonProtocolCodec codec;
  const char* json = R"({
    "Data": {
      "CmdId": "1001",
      "FullPathFileName": "/tmp/a.bin",
      "StartByte": "1"
    }
  })";
    transfer::SummonRequest req;
    std::string err;
    ASSERT_TRUE(codec.decodeSummon(json, req, err));
    EXPECT_EQ(req.cmdId, 1001u);
    EXPECT_EQ(req.fullPathFileName, "/tmp/a.bin");
    EXPECT_EQ(req.startByte, 1u);
    EXPECT_EQ(req.fileOffset, 0u);
}

TEST(JsonCodecTest, DecodeSummonInvalidStartByte) {
    transfer::JsonProtocolCodec codec;
    const char* json = R"({"Data":{"CmdId":"1","FullPathFileName":"/x","StartByte":"0"}})";
    transfer::SummonRequest req;
    std::string err;
    EXPECT_FALSE(codec.decodeSummon(json, req, err));
}

TEST(JsonCodecTest, EncodeBriefSuccess) {
    transfer::JsonProtocolCodec codec;
    transfer::BriefResponse br;
    br.cmdId = 1001;
    br.status = transfer::BriefStatus::Success;
    br.fileSize = 100;
    br.fileCrc = "0x01020304";
    br.modifyTime = "2026-06-01 12:00:00";
    std::string json = codec.encodeBrief(br);
    EXPECT_NE(json.find("\"Status\":\"0\""), std::string::npos);
    EXPECT_NE(json.find("\"FileSize\":\"100\""), std::string::npos);
}

TEST(JsonCodecTest, EncodeContentContinueFlags) {
    transfer::JsonProtocolCodec codec;
    transfer::ContentSegment seg;
    seg.cmdId = 1;
    seg.fileSegNo = 2;
    seg.contentRaw = {0xAA};
    seg.continueFlag = false;
    std::string json = codec.encodeContent(seg);
    EXPECT_NE(json.find("\"FileSegNo\":\"2\""), std::string::npos);
    EXPECT_NE(json.find("\"Continue\":\"0\""), std::string::npos);
}
