#include <iostream>
#include <string>
#include <cstring>
#include <cassert>

#include "protocol/message.h"
#include "protocol/encoder.h"
#include "protocol/decoder.h"

using namespace fileserver::protocol;

// ── 辅助: 创建一条 login 消息 ─────────────────────────────────────
static Message MakeLoginMessage(uint32_t req_id,
                                 const std::string& username,
                                 const std::string& password) {
    Message msg;
    msg.header.msg_type   = static_cast<uint32_t>(MessageType::kRequest);
    msg.header.request_id = req_id;

    nlohmann::json j;
    j["cmd"]      = "login";
    j["username"] = username;
    j["password"] = password;
    msg.SetJson(j);

    return msg;
}

// ── 辅助: 创建一条 upload_start 消息 ──────────────────────────────
static Message MakeUploadStartMessage(uint32_t req_id,
                                       const std::string& filename,
                                       uint64_t filesize,
                                       const std::string& md5) {
    Message msg;
    msg.header.msg_type   = static_cast<uint32_t>(MessageType::kRequest);
    msg.header.request_id = req_id;

    nlohmann::json j;
    j["cmd"]      = "upload_start";
    j["filename"] = filename;
    j["filesize"] = filesize;
    j["md5"]      = md5;
    msg.SetJson(j);

    // 附加一些二进制数据模拟文件块
    msg.binary_data = std::string(64, 'A');
    msg.header.data_length = static_cast<uint32_t>(msg.binary_data.size());

    return msg;
}

// ── 辅助: 验证两条 Message 相等 ───────────────────────────────────
static void AssertMessageEqual(const Message& a, const Message& b) {
    assert(a.header.magic      == b.header.magic);
    assert(a.header.version    == b.header.version);
    assert(a.header.msg_type   == b.header.msg_type);
    assert(a.header.request_id == b.header.request_id);
    assert(a.json_body         == b.json_body);
    assert(a.binary_data       == b.binary_data);
}

// ═══════════════════════════════════════════════════════════════════
// 测试 1: 单条消息编码 → 解码
// ═══════════════════════════════════════════════════════════════════
static void TestSingleMessage() {
    std::cout << "\n=== 测试 1: 单条消息编解码 ===\n" << std::endl;

    Message original = MakeLoginMessage(1, "admin", "123456");

    // 编码
    std::string wire = Encoder::Encode(original);
    std::cout << "  编码后字节数: " << wire.size()
              << " (header=24 + body=" << original.json_body.size() << ")"
              << std::endl;

    // 解码
    Decoder decoder;
    auto msgs = decoder.Feed(wire);

    assert(msgs.size() == 1);
    AssertMessageEqual(msgs[0], original);

    // 验证 JSON 内容
    auto j = msgs[0].GetJson();
    assert(j["cmd"]      == "login");
    assert(j["username"] == "admin");
    assert(j["password"] == "123456");
    assert(msgs[0].GetCmd() == "login");

    std::cout << "  JSON cmd: " << msgs[0].GetCmd() << std::endl;
    std::cout << "  CRC32: 0x" << std::hex << msgs[0].header.checksum << std::dec << std::endl;

    std::cout << "\n[通过] 单条消息编解码\n" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 2: 粘包 — 两条消息首尾拼接, 一次 Feed
// ═══════════════════════════════════════════════════════════════════
static void TestStickyPacket() {
    std::cout << "\n=== 测试 2: 粘包（两条消息一次发送）===\n" << std::endl;

    Message msg1 = MakeLoginMessage(1, "alice", "pass1");
    Message msg2 = MakeUploadStartMessage(2, "video.mp4", 102400000, "abc123def456");

    // 编码后拼接在一起
    std::string wire1 = Encoder::Encode(msg1);
    std::string wire2 = Encoder::Encode(msg2);
    std::string sticky = wire1 + wire2;

    std::cout << "  消息1 字节数: " << wire1.size() << std::endl;
    std::cout << "  消息2 字节数: " << wire2.size() << std::endl;
    std::cout << "  粘包总字节数: " << sticky.size() << std::endl;

    // 一次性 Feed 到 Decoder
    Decoder decoder;
    auto msgs = decoder.Feed(sticky);

    std::cout << "  解析出消息数: " << msgs.size() << std::endl;

    assert(msgs.size() == 2);
    AssertMessageEqual(msgs[0], msg1);
    AssertMessageEqual(msgs[1], msg2);

    assert(msgs[0].GetCmd() == "login");
    assert(msgs[1].GetCmd() == "upload_start");

    std::cout << "  消息1 cmd: " << msgs[0].GetCmd() << std::endl;
    std::cout << "  消息2 cmd: " << msgs[1].GetCmd() << std::endl;

    std::cout << "\n[通过] 粘包测试\n" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 3: 半包 — 一条消息分两次 Feed
// ═══════════════════════════════════════════════════════════════════
static void TestHalfPacket() {
    std::cout << "\n=== 测试 3: 半包（一条消息分两次发送）===\n" << std::endl;

    Message original = MakeUploadStartMessage(3, "backup.tar.gz", 512000000, "fedcba9876543210");

    std::string wire = Encoder::Encode(original);
    std::cout << "  完整消息字节数: " << wire.size() << std::endl;

    // 从中间切成两半
    size_t split_pos = wire.size() / 2;
    std::string part1(wire.data(), split_pos);
    std::string part2(wire.data() + split_pos, wire.size() - split_pos);

    std::cout << "  第一部分: " << part1.size() << " 字节" << std::endl;
    std::cout << "  第二部分: " << part2.size() << " 字节" << std::endl;

    Decoder decoder;

    // 第一次 Feed: header 都不完整 → 应该返回 0 条
    // (取决于切分位置，可能在 header 内部或 body 内部)
    auto msgs1 = decoder.Feed(part1);
    std::cout << "  第1次 Feed → " << msgs1.size() << " 条完整消息"
              << " (缓冲未完成: " << decoder.BufferedBytes() << " 字节)" << std::endl;

    // 第二次 Feed: 补全 → 应该返回 1 条完整消息
    auto msgs2 = decoder.Feed(part2);
    std::cout << "  第2次 Feed → " << msgs2.size() << " 条完整消息"
              << " (缓冲剩余: " << decoder.BufferedBytes() << " 字节)" << std::endl;

    // 总共解析出 1 条消息
    size_t total_msgs = msgs1.size() + msgs2.size();
    assert(total_msgs == 1);

    Message& result = (msgs1.size() > 0) ? msgs1[0] : msgs2[0];
    AssertMessageEqual(result, original);
    assert(result.GetCmd() == "upload_start");

    std::cout << "  消息 cmd: " << result.GetCmd() << std::endl;

    std::cout << "\n[通过] 半包测试\n" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 4: 批量编码辅助函数
// ═══════════════════════════════════════════════════════════════════
static void TestEncodeBatch() {
    std::cout << "\n=== 测试 4: 批量编码 ===\n" << std::endl;

    std::vector<Message> batch;
    batch.push_back(MakeLoginMessage(1, "user1", "pass1"));
    batch.push_back(MakeLoginMessage(2, "user2", "pass2"));
    batch.push_back(MakeLoginMessage(3, "user3", "pass3"));

    std::string wire = Encoder::EncodeBatch(batch);

    Decoder decoder;
    auto msgs = decoder.Feed(wire);

    assert(msgs.size() == 3);
    assert(msgs[0].header.request_id == 1);
    assert(msgs[1].header.request_id == 2);
    assert(msgs[2].header.request_id == 3);

    std::cout << "  批量 3 条消息 → 解析出 " << msgs.size() << " 条" << std::endl;

    std::cout << "\n[通过] 批量编码测试\n" << std::endl;
}

// ── 程序入口 ─────────────────────────────────────────────────────
int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer – 协议模块测试" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    try {
        TestSingleMessage();
        TestStickyPacket();
        TestHalfPacket();
        TestEncodeBatch();
    } catch (const std::exception& e) {
        std::cerr << "\n测试异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  全部协议测试通过" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    return 0;
}
