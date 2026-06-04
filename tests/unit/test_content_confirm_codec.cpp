// V0.0.4 内容确认 JSON 编解码单测

#include "../test_compat.hpp"

#include "transfer/protocol_codec.hpp"

TEST(ContentConfirmCodecTest, DecodeAndEncode) {
    transfer::JsonProtocolCodec codec;
    const std::string json =
        R"({"Data":{"CmdId":"100","FileSegNo":"2","Status":"0","ErrorCode":"","Note":"ok"}})";
    transfer::ContentConfirmRequest req;
    std::string err;
    ASSERT_TRUE(codec.decodeContentConfirm(json, req, err));
    EXPECT_EQ(req.cmdId, 100u);
    EXPECT_EQ(req.fileSegNo, 2u);
    EXPECT_EQ(req.status, transfer::BriefStatus::Success);
    EXPECT_EQ(req.note, "ok");

    const std::string out = codec.encodeContentConfirm(req);
    EXPECT_NE(out.find("\"FileSegNo\":\"2\""), std::string::npos);
    EXPECT_NE(out.find("\"Status\":\"0\""), std::string::npos);
}

TEST(ContentConfirmCodecTest, FailureStatusRequiresFields) {
    transfer::JsonProtocolCodec codec;
    const std::string json =
        R"({"Data":{"CmdId":"1","FileSegNo":"1","Status":"1","ErrorCode":"BUSY","Note":"fail"}})";
    transfer::ContentConfirmRequest req;
    std::string err;
    ASSERT_TRUE(codec.decodeContentConfirm(json, req, err));
    EXPECT_EQ(req.status, transfer::BriefStatus::Failure);
    EXPECT_EQ(req.errorCode, "BUSY");
}
