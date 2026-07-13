#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>
#include <openssl/evp.h>

#include "client/file_client.h"

using namespace fileserver::client;

// ── 配置 ─────────────────────────────────────────────────────────
const char*  kHost     = "127.0.0.1";
const int    kPort     = 8080;
const int64_t kFile10M = 10 * 1024 * 1024;
const int64_t kFile2G  = 2LL * 1024 * 1024 * 1024;

// ── 时间工具 ─────────────────────────────────────────────────────
static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ── 内存信息 ─────────────────────────────────────────────────────
static long GetMemRSS_KB() {
    std::ifstream fs("/proc/self/status");
    std::string line;
    while (std::getline(fs, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            long kb;
            sscanf(line.c_str() + 6, "%ld", &kb);
            return kb;
        }
    }
    return 0;
}

// ── 生成随机文件 ─────────────────────────────────────────────────
static std::string MakeFile(const std::string& path, int64_t size) {
    std::ofstream fs(path, std::ios::binary);
    char buf[65536];
    int64_t written = 0;
    while (written < size) {
        int64_t n = std::min((int64_t)sizeof(buf), size - written);
        memset(buf, (char)(written & 0xFF), n);  // 伪随机, 可复现
        fs.write(buf, n);
        written += n;
    }
    return path;
}

// ── 打印分隔线 ───────────────────────────────────────────────────
static void PrintBar(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(70, '=') << "\n" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 预检查: 确认服务端可达
// ═══════════════════════════════════════════════════════════════════
static bool PreCheck() {
    std::cout << "  检查服务端连接..." << std::endl;
    FileClient c;
    if (!c.Connect(kHost, kPort)) {
        std::cerr << "  [失败] 无法连接到 " << kHost << ":" << kPort
                  << "\n  请确认: 服务端是否已启动? 端口是否正确?\n" << std::endl;
        return false;
    }
    auto r = c.Register("precheck_user", "pass");
    std::cout << "  注册测试: code=" << r.code << " msg=" << r.msg << std::endl;
    c.Disconnect();
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 1: 200 并发连接
// ═══════════════════════════════════════════════════════════════════
static void Test200Connections() {
    PrintBar("测试 1: 200 并发连接");
    const int N = 200;
    std::atomic<int> ok{0}, fail{0};

    std::vector<std::thread> threads;
    auto start = NowMs();

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([i, &ok, &fail]() {
            FileClient c;
            if (c.Connect(kHost, kPort)) {
                // 先注册
                std::string user = "stress_conn_" + std::to_string(i);
                auto r = c.Register(user, "pass");
                if (r.ok() || r.code == -1) ok++;
                else { fail++; std::cerr << "  [" << i << "] code=" << r.code << " " << r.msg << std::endl; }
                c.Disconnect();
            } else {
                fail++;
            }
        });
    }

    for (auto& t : threads) t.join();
    auto elapsed = NowMs() - start;

    std::cout << "  并发连接: " << N << std::endl;
    std::cout << "  成功: " << ok << ", 失败: " << fail << std::endl;
    std::cout << "  耗时: " << elapsed << " ms" << std::endl;
    std::cout << "  内存: " << (GetMemRSS_KB() >> 10) << " MB" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 2: 200 连接保持（5 分钟）
// ═══════════════════════════════════════════════════════════════════
static void Test200Persistent() {
    PrintBar("测试 2: 200 连接持续保持");
    const int N = 200;
    std::atomic<bool> stop{false};
    std::atomic<int> ok{0}, fail{0};

    auto start = NowMs();

    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([i, &ok, &fail, &stop]() {
            FileClient c;
            if (!c.Connect(kHost, kPort)) { fail++; return; }

            std::string user = "stress_keep_" + std::to_string(i);
            c.Register(user, "pass");
            auto r = c.Login(user, "pass");
            if (!r.ok()) { fail++; return; }
            ok++;

            // 每 10 秒发一次心跳（list 请求）
            while (!stop.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                c.ListFiles(c.GetToken());
            }
            c.Disconnect();
        });
    }

    // 让它跑
    for (int sec = 0; sec < 60 && ok + fail < N; ++sec)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "  " << ok.load() << " 个连接已建立并保持..." << std::endl;
    std::cout << "  运行 60 秒..." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(60));
    stop = true;

    for (auto& t : threads) t.join();
    auto elapsed = NowMs() - start;

    std::cout << "  活跃连接: " << ok.load() << " (持续 60s)" << std::endl;
    std::cout << "  失败: " << fail.load() << std::endl;
    std::cout << "  内存: " << (GetMemRSS_KB() >> 10) << " MB" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
// 测试 3: 50 并发上传 (10MB 文件)
// ═══════════════════════════════════════════════════════════════════
static void Test50ConcurrentUpload() {
    PrintBar("测试 3: 50 并发上传 (10MB 文件)");
    const int N = 50;
    std::atomic<int> ok{0}, fail{0};
    std::atomic<int64_t> total_bytes{0};

    // 准备测试文件
    std::string test_file = "/tmp/stress_upload_10mb.dat";
    MakeFile(test_file, kFile10M);

    auto start = NowMs();
    std::vector<std::thread> threads;

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([i, &ok, &fail, &total_bytes]() {
            FileClient c;
            if (!c.Connect(kHost, kPort)) { fail++; return; }

            std::string user = "stress_ul_" + std::to_string(i);
            c.Register(user, "pass");
            auto login = c.Login(user, "pass");
            if (!login.ok()) { fail++; return; }

            auto resp = c.UploadFile(c.GetToken(), "/tmp/stress_upload_10mb.dat", nullptr);
            if (resp.ok()) {
                ok++;
                total_bytes += kFile10M;
            } else {
                fail++;
                std::cerr << "  [失败] 上传失败: code=" << resp.code
                          << " msg=" << resp.msg << std::endl;
            }
            c.Disconnect();
        });
    }

    for (auto& t : threads) t.join();
    auto elapsed = NowMs() - start;

    int64_t total_mb = total_bytes.load() >> 20;
    double speed_mbps = elapsed > 0
        ? (double)total_bytes.load() * 8.0 / (elapsed * 1000.0)
        : 0;

    std::cout << "  并发数: " << N << std::endl;
    std::cout << "  成功: " << ok << ", 失败: " << fail << std::endl;
    std::cout << "  总传输: " << total_mb << " MB" << std::endl;
    std::cout << "  耗时: " << elapsed << " ms" << std::endl;
    std::cout << "  吞吐: " << std::fixed << std::setprecision(1)
              << speed_mbps << " Mbps" << std::endl;
    std::cout << "  内存: " << (GetMemRSS_KB() >> 10) << " MB" << std::endl;

    std::remove(test_file.c_str());
}

// ═══════════════════════════════════════════════════════════════════
// 测试 4: 大文件传输 (2GB 串行, 避免占满磁盘)
// ═══════════════════════════════════════════════════════════════════
static void Test2GBTransfer() {
    PrintBar("测试 4: 大文件传输 (2GB)");

    std::string test_file = "/tmp/stress_2gb.dat";
    std::cout << "  生成 2GB 测试文件..." << std::endl;

    // 稀疏文件生成（快）
    {
        std::ofstream fs(test_file, std::ios::binary);
        char buf[65536];
        memset(buf, 'A', sizeof(buf));
        int64_t written = 0;
        while (written < kFile2G) {
            int64_t n = std::min((int64_t)sizeof(buf), kFile2G - written);
            fs.write(buf, n);
            written += n;
            if (written % (100LL << 20) == 0)
                std::cout << "    " << (written >> 30) << "GB...\r" << std::flush;
        }
        fs.close();
    }
    std::cout << "    生成完毕" << std::endl;

    FileClient c;
    if (!c.Connect(kHost, kPort)) { std::cout << "连接失败\n"; return; }

    c.Register("stress_big", "pass");
    auto login = c.Login("stress_big", "pass");
    if (!login.ok()) { std::cout << "登录失败\n"; return; }

    // ── 上传 ───────────────────────────────────────────────────
    std::cout << "\n  --- 上传 2GB ---" << std::endl;
    auto up_start = NowMs();
    int64_t up_last = 0;

    auto up_resp = c.UploadFile(c.GetToken(), test_file,
        [&](int64_t done, int64_t total) {
            auto now = NowMs();
            if (now - up_last > 500) {
                int pct = (int)(done * 100 / total);
                double speed = (up_last > 0 && now > up_last)
                    ? (double)(done) / ((now - up_start) * 1000.0) * 1000.0
                    : 0;
                std::cout << "\r    上传: " << pct << "% ("
                          << (done >> 20) << " / " << (total >> 20) << " MB)"
                          << "  " << std::fixed << std::setprecision(1)
                          << (speed / 1048576.0) << " MB/s" << std::flush;
                up_last = now;
            }
        });

    auto up_elapsed = NowMs() - up_start;
    double up_speed = up_elapsed > 0
        ? (double)kFile2G / (up_elapsed * 1000.0)
        : 0;

    std::cout << "\n\n  上传耗时: " << (up_elapsed / 1000) << " 秒"
              << ", 速度: " << std::fixed << std::setprecision(1)
              << (up_speed / 1048576.0) << " MB/s" << std::endl;

    if (!up_resp.ok()) {
        std::cout << "  上传失败: " << up_resp.msg << std::endl;
        c.Disconnect();
        std::remove(test_file.c_str());
        return;
    }

    int64_t file_id = up_resp.data["file_id"];

    // ── 下载 ───────────────────────────────────────────────────
    std::cout << "\n  --- 下载 2GB ---" << std::endl;
    auto dl_start = NowMs();
    int64_t dl_last = 0;
    std::string save_path = "/tmp/stress_2gb_dl.dat";

    auto dl_resp = c.DownloadFile(c.GetToken(), file_id, save_path,
        [&](int64_t done, int64_t total) {
            auto now = NowMs();
            if (now - dl_last > 500) {
                int pct = (int)(done * 100 / total);
                double speed = dl_last > 0
                    ? (double)(done) / ((now - dl_start) * 1000.0) * 1000.0
                    : 0;
                std::cout << "\r    下载: " << pct << "% ("
                          << (done >> 20) << " / " << (total >> 20) << " MB)"
                          << "  " << std::fixed << std::setprecision(1)
                          << (speed / 1048576.0) << " MB/s" << std::flush;
                dl_last = now;
            }
        });

    auto dl_elapsed = NowMs() - dl_start;
    double dl_speed = dl_elapsed > 0
        ? (double)kFile2G / (dl_elapsed * 1000.0)
        : 0;

    std::cout << "\n\n  下载耗时: " << (dl_elapsed / 1000) << " 秒"
              << ", 速度: " << std::fixed << std::setprecision(1)
              << (dl_speed / 1048576.0) << " MB/s" << std::endl;

    // ── 清理 ───────────────────────────────────────────────────
    c.DeleteFile(c.GetToken(), file_id);
    c.Disconnect();
    std::remove(test_file.c_str());
    std::remove(save_path.c_str());

    std::cout << "\n  内存: " << (GetMemRSS_KB() >> 10) << " MB" << std::endl;
    std::cout << "  注: 本地回环速度约为磁盘/2, 实际网络环境下以上传速度为准" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    std::cout << "\n======================================" << std::endl;
    std::cout << "  FileServer 压力测试" << std::endl;
    std::cout << "  目标: " << kHost << ":" << kPort << std::endl;
    std::cout << "======================================" << std::endl;

    std::string test = (argc >= 2) ? argv[1] : "all";

    if (!PreCheck()) return 1;

    int64_t t0 = NowMs();

    if (test == "1" || test == "all") Test200Connections();
    if (test == "2" || test == "all") Test200Persistent();
    if (test == "3" || test == "all") Test50ConcurrentUpload();
    if (test == "4" || test == "all") Test2GBTransfer();

    auto total_elapsed = NowMs() - t0;
    PrintBar("总耗时: " + std::to_string(total_elapsed / 1000) + " 秒");

    std::cout << "  用法: " << argv[0] << " [1|2|3|4|all]" << std::endl;
    std::cout << "    1 = 200并发连接\n"
              << "    2 = 200连接持续保持\n"
              << "    3 = 50并发上传10MB\n"
              << "    4 = 2GB大文件传输\n"
              << "    all = 全部\n" << std::endl;

    return 0;
}
