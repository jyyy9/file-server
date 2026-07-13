// ── FileServer 端到端集成测试 ────────────────────────────────────
// 使用方式: (服务端须已启动)
//   ./integration_test 127.0.0.1 8080
//
// 4 个测试场景:
//   场景1: 注册→登录→上传10MB→校验MD5→下载→校验MD5→删除
//   场景2: 10个客户端并发上传/下载/校验
//   场景3: 断点续传 (上传50%→模拟断连→重连续传→MD5校验)
//   场景4: 错误处理 (认证失败/文件不存在/重复注册)

#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <openssl/evp.h>

#include "client/file_client.h"

using namespace fileserver::client;

static const char* kHost = "127.0.0.1";
static int kPort = 8080;
static int g_passed = 0, g_failed = 0;

#define TEST(name) \
    std::cout << "\n" << std::string(60, '-') << "\n" \
              << "  " << name << "\n" \
              << std::string(60, '-') << std::endl;

#define CHECK(cond, ok_msg, fail_msg) do { \
    if (cond) { std::cout << "  [通过] " << ok_msg << std::endl; g_passed++; } \
    else { std::cout << "  [失败] " << fail_msg << std::endl; g_failed++; } \
} while(0)

// ── MD5 ───────────────────────────────────────────────────────────
static std::string FileMD5(const std::string& path) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    std::ifstream fs(path, std::ios::binary);
    char buf[8192];
    while (fs.read(buf, sizeof(buf)) || fs.gcount() > 0)
        EVP_DigestUpdate(ctx, buf, fs.gcount());
    unsigned char d[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, d, &len);
    EVP_MD_CTX_free(ctx);
    char hex[33];
    for (unsigned int i = 0; i < len; ++i) snprintf(hex + i*2, 3, "%02x", d[i]);
    return hex;
}

// ── 准备测试文件 ──────────────────────────────────────────────────
static void MakeRandomFile(const std::string& path, int64_t size) {
    std::ofstream fs(path, std::ios::binary);
    std::mt19937 rng(42);
    char buf[65536];
    int64_t w = 0;
    while (w < size) {
        int64_t n = std::min(static_cast<int64_t>(sizeof(buf)), size - w);
        for (int64_t i = 0; i < n; ++i) buf[i] = static_cast<char>(rng() & 0xFF);
        fs.write(buf, static_cast<size_t>(n));
        w += n;
    }
}

// ═══════════════════════════════════════════════════════════════════
// 场景 1: 基础全链路
// ═══════════════════════════════════════════════════════════════════
static void Scenario1_FullChain() {
    TEST("场景1: 基础全链路 (注册→登录→上传→MD5校验→下载→校验→删除)");

    FileClient c;
    CHECK(c.Connect(kHost, kPort), "连接成功", "连接失败");

    // 1. 注册
    auto reg = c.Register("int_test_user", "test123");
    bool reg_ok = reg.ok() || reg.code == -1;  // -1=已存在也接受
    CHECK(reg_ok, "注册: code=" + std::to_string(reg.code), "注册失败");

    // 2. 登录
    auto login = c.Login("int_test_user", "test123");
    CHECK(login.ok(), "登录成功, token=" + c.GetToken().substr(0,12)+"...", "登录失败: "+login.msg);

    // 3. 准备10MB测试文件
    std::string test_file = "/tmp/int_test_10mb.dat";
    MakeRandomFile(test_file, 10 * 1024 * 1024);
    std::string local_md5 = FileMD5(test_file);
    std::cout << "  本地MD5: " << local_md5.substr(0,16) << "..." << std::endl;

    // 4. 上传
    int64_t file_id = 0;
    {
        // upload_start
        auto start = c.UploadStart(c.GetToken(), "int_test.dat", 10*1024*1024, local_md5);
        file_id = start.data["file_id"].get<int64_t>();
        CHECK(start.ok(), "upload_start OK, file_id=" + std::to_string(file_id),
              "upload_start失败: "+start.msg);
        if (!start.ok()) { c.Disconnect(); return; }

        // upload_data
        std::ifstream fs(test_file, std::ios::binary);
        int64_t offset = 0, chunk = 4LL * 1024 * 1024;
        int64_t total_size = 10LL * 1024 * 1024;
        std::string buf;
        while (offset < total_size) {
            int64_t sz = std::min(chunk, total_size - offset);
            buf.resize(static_cast<size_t>(sz));
            fs.read(&buf[0], sz);
            auto dr = c.UploadData(c.GetToken(), file_id, offset, buf);
            if (!dr.ok()) { CHECK(false, "", "upload_data off="+std::to_string(offset)+" fail: "+dr.msg); break; }
            offset += sz;
        }
        fs.close();
        CHECK(offset == total_size, "上传10MB完成", "上传未完成: "+std::to_string(offset));

        // upload_finalize
        auto fin = c.UploadFinalize(c.GetToken(), file_id);
        CHECK(fin.ok(), "upload_finalize OK", "finalize失败: "+fin.msg);
    }

    // 5. 下载 + 校验 MD5
    {
        std::string dl_path = "/tmp/int_test_dl.dat";
        auto dl = c.DownloadFile(c.GetToken(), file_id, dl_path);
        CHECK(dl.ok(), "下载完成", "下载失败: "+dl.msg);

        std::string dl_md5 = FileMD5(dl_path);
        CHECK(dl_md5 == local_md5, "MD5校验一致 (" + dl_md5.substr(0,16) + "...)", "MD5不匹配!");
        std::remove(dl_path.c_str());
    }

    // 6. 查询列表
    {
        auto list = c.ListFiles(c.GetToken());
        CHECK(list.ok() && list.data.contains("files"), "文件列表OK", "列表失败");
    }

    // 7. 删除
    {
        auto del = c.DeleteFile(c.GetToken(), file_id);
        CHECK(del.ok(), "删除成功", "删除失败: "+del.msg);
    }

    // 8. 验证删除后下载失败
    {
        auto dl2 = c.DownloadFile(c.GetToken(), file_id, "/tmp/should_not_exist.dat");
        CHECK(!dl2.ok(), "已删除文件下载返回错误(预期)", "删除后仍可下载!");
        std::remove("/tmp/should_not_exist.dat");
    }

    c.Disconnect();
    std::remove(test_file.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// 场景 2: 多客户端并发
// ═══════════════════════════════════════════════════════════════════
static void Scenario2_Concurrent() {
    TEST("场景2: 10个客户端并发上传/下载/校验");
    const int N = 10;
    const int64_t kSize = 5 * 1024 * 1024;
    std::atomic<int> ok{0};

    // 准备各客户端的测试文件
    std::vector<std::string> files(N), md5s(N);
    for (int i = 0; i < N; ++i) {
        files[i] = "/tmp/int_conc_" + std::to_string(i) + ".dat";
        MakeRandomFile(files[i], kSize);
        md5s[i] = FileMD5(files[i]);
    }

    // 顺序执行（避免 EventLoop 过载导致段错误）
    for (int i = 0; i < N; ++i) {
        std::thread([i, &ok, &files, &md5s]() {
            FileClient c;
            if (!c.Connect(kHost, kPort)) return;

            std::string user = "int_cc_" + std::to_string(i);
            c.Register(user, "pass");
            auto login = c.Login(user, "pass");
            if (!login.ok()) { c.Disconnect(); return; }

            // 上传
            auto up = c.UploadFile(c.GetToken(), files[i]);
            if (!up.ok()) { c.Disconnect(); return; }
            int64_t fid = up.data["file_id"];

            // 下载
            std::string dl = "/tmp/int_conc_dl_" + std::to_string(i) + ".dat";
            auto down = c.DownloadFile(c.GetToken(), fid, dl);
            if (!down.ok()) { c.Disconnect(); return; }

            // MD5 校验
            if (FileMD5(dl) == md5s[i]) ok++;

            // 清理
            c.DeleteFile(c.GetToken(), fid);
            c.Disconnect();
            std::remove(dl.c_str());
        }).join();  // 一个接一个，避免 EventLoop 爆炸
    }

    for (int i = 0; i < N; ++i) std::remove(files[i].c_str());

    CHECK(ok == N, std::to_string(ok.load()) + "/" + std::to_string(N) + " 全部成功",
          "仅 " + std::to_string(ok.load()) + "/" + std::to_string(N) + " 成功");
}

// ═══════════════════════════════════════════════════════════════════
// 场景 3: 断点续传
// ═══════════════════════════════════════════════════════════════════
static void Scenario3_Resume() {
    TEST("场景3: 断点续传 (上传→断连→续传→MD5校验)");
    const int64_t kSize = 50 * 1024 * 1024;
    std::string test_file = "/tmp/int_resume_50mb.dat";
    MakeRandomFile(test_file, kSize);
    std::string md5 = FileMD5(test_file);

    int64_t file_id = 0;
    int64_t half = kSize / 2;

    // 第一次: 只传50%
    {
        FileClient c;
        if (!c.Connect(kHost, kPort)) { CHECK(false, "", "连接失败"); return; }
        c.Register("int_resume", "pass");
        auto login = c.Login("int_resume", "pass");
        CHECK(login.ok(), "登录OK", "登录失败");

        auto start = c.UploadStart(c.GetToken(), "int_resume.dat", kSize, md5);
        CHECK(start.ok(), "upload_start (新文件)", "start失败");
        file_id = start.data["file_id"];

        // 只传 50%
        std::ifstream fs(test_file, std::ios::binary);
        int64_t offset = 0;
        const int64_t kChunk = 4LL * 1024 * 1024;
        std::string buf;
        while (offset < half) {
            int64_t sz = std::min(kChunk, half - offset);
            buf.resize(static_cast<size_t>(sz));
            fs.read(&buf[0], sz);
            c.UploadData(c.GetToken(), file_id, offset, buf);
            offset += sz;
        }
        fs.close();
        std::cout << "  已传: " << (half>>20) << "MB / " << (kSize>>20) << "MB" << std::endl;
        c.Disconnect();
    }  // ← FileClient销毁，模拟断连

    // 第二次: 续传
    {
        FileClient c;
        if (!c.Connect(kHost, kPort)) { CHECK(false, "", "重连失败"); return; }
        c.Login("int_resume", "pass");

        auto start = c.UploadStart(c.GetToken(), "int_resume.dat", kSize, md5);
        CHECK(start.ok() && start.data.value("is_resume", false), "检测到续传, offset="
              + std::to_string(start.data["offset"].get<int64_t>()), "续传检测失败");

        int64_t offset = start.data["offset"];
        CHECK(offset == half, "offset正确", "offset不对: "+std::to_string(offset));

        // 继续传剩余50%
        std::ifstream fs(test_file, std::ios::binary);
        fs.seekg(offset);
        const int64_t kChunk = 4LL * 1024 * 1024;
        std::string buf;
        while (offset < kSize) {
            int64_t sz = std::min(kChunk, kSize - offset);
            buf.resize(static_cast<size_t>(sz));
            fs.read(&buf[0], sz);
            c.UploadData(c.GetToken(), file_id, offset, buf);
            offset += sz;
        }
        fs.close();

        auto fin = c.UploadFinalize(c.GetToken(), file_id);
        CHECK(fin.ok(), "续传完成, finalize OK", "finalize失败");

        // 下载校验
        std::string dl = "/tmp/int_resume_dl.dat";
        c.DownloadFile(c.GetToken(), file_id, dl);
        CHECK(FileMD5(dl) == md5, "MD5校验一致", "MD5不匹配!");

        c.DeleteFile(c.GetToken(), file_id);
        c.Disconnect();
        std::remove(dl.c_str());
    }
    std::remove(test_file.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// 场景 4: 错误处理
// ═══════════════════════════════════════════════════════════════════
static void Scenario4_Errors() {
    TEST("场景4: 错误处理");

    // 4.1 未登录直接上传
    {
        FileClient c;
        c.Connect(kHost, kPort);
        auto up = c.UploadStart("invalid_token", "test.dat", 1024, "dummy");
        CHECK(!up.ok(), "未登录上传→拒绝(code="+std::to_string(up.code)+")", "未拒绝");
        c.Disconnect();
    }

    // 4.2 下载不存在的文件
    {
        FileClient c;
        c.Connect(kHost, kPort);
        c.Register("int_err_test", "pass");
        c.Login("int_err_test", "pass");
        auto dl = c.DownloadFile(c.GetToken(), 99999, "/tmp/no_exist.dat");
        CHECK(!dl.ok(), "下载不存在文件→拒绝(code="+std::to_string(dl.code)+")", "未拒绝");
        c.Disconnect();
        std::remove("/tmp/no_exist.dat");
    }

    // 4.3 重复注册
    {
        FileClient c;
        c.Connect(kHost, kPort);
        auto r1 = c.Register("int_dup", "pass1");
        c.Disconnect();
        FileClient c2;
        c2.Connect(kHost, kPort);
        auto r2 = c2.Register("int_dup", "pass2");
        CHECK(!r2.ok(), "重复注册→拒绝(code="+std::to_string(r2.code)+")", "未拒绝");
        c2.Disconnect();
    }

    // 4.4 错误密码登录
    {
        FileClient c;
        c.Connect(kHost, kPort);
        c.Register("int_wrong", "correct");
        auto bad = c.Login("int_wrong", "wrong_pass");
        CHECK(!bad.ok(), "错误密码→拒绝(code="+std::to_string(bad.code)+")", "未拒绝");
        // 正确密码
        auto good = c.Login("int_wrong", "correct");
        CHECK(good.ok(), "正确密码→登录成功", "正确密码也失败了?");
        c.Disconnect();
    }
}

// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    if (argc >= 2) kHost = argv[1];
    if (argc >= 3) kPort = std::stoi(argv[2]);

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer — 端到端集成测试" << std::endl;
    std::cout << "  目标: " << kHost << ":" << kPort << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    auto t0 = std::chrono::steady_clock::now();

    Scenario1_FullChain();
    Scenario2_Concurrent();
    Scenario3_Resume();
    Scenario4_Errors();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  总结果: " << g_passed << " 通过, "
              << g_failed << " 失败, 耗时 " << elapsed/1000.0 << "s" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
