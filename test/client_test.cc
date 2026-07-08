#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cassert>

// POSIX socket headers (Linux)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const int    kServerPort   = 8080;
const char*  kServerIP     = "127.0.0.1";
const int    kNumClients   = 100;
const char*  kTestMessage  = "Hello from client!";

// ── 统计计数器 ───────────────────────────────────────────────────
std::atomic<int> g_connect_ok{0};
std::atomic<int> g_connect_fail{0};
std::atomic<int> g_send_ok{0};
std::atomic<int> g_send_fail{0};
std::atomic<int> g_recv_ok{0};
std::atomic<int> g_recv_fail{0};

// ── 单个客户端任务 ───────────────────────────────────────────────
void ClientTask(int client_id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        g_connect_fail.fetch_add(1);
        std::cerr << "[Client " << client_id << "] socket() 失败" << std::endl;
        return;
    }

    // 设置服务器地址
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(kServerPort);
    inet_pton(AF_INET, kServerIP, &server_addr.sin_addr);

    // ── 1. 连接 ──────────────────────────────────────────────────
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        g_connect_fail.fetch_add(1);
        std::cerr << "[Client " << client_id << "] connect() 失败" << std::endl;
        close(sock);
        return;
    }
    g_connect_ok.fetch_add(1);

    // ── 2. 发送消息 ──────────────────────────────────────────────
    std::string msg = std::string(kTestMessage) + " #" + std::to_string(client_id);
    ssize_t sent = send(sock, msg.c_str(), msg.size(), 0);
    if (sent != static_cast<ssize_t>(msg.size())) {
        g_send_fail.fetch_add(1);
        std::cerr << "[Client " << client_id << "] send() 失败: "
                  << sent << "/" << msg.size() << std::endl;
        close(sock);
        return;
    }
    g_send_ok.fetch_add(1);

    // ── 3. 接收回显 ──────────────────────────────────────────────
    char buffer[4096];
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        std::string response(buffer);

        // 验证回显内容（服务器是 echo，应该原样返回）
        if (response == msg) {
            g_recv_ok.fetch_add(1);
        } else {
            g_recv_fail.fetch_add(1);
            std::cerr << "[Client " << client_id << "] 回显内容不匹配: "
                      << "期望=\"" << msg << "\", 实际=\"" << response << "\"" << std::endl;
        }
    } else {
        g_recv_fail.fetch_add(1);
        std::cerr << "[Client " << client_id << "] recv() 失败" << std::endl;
    }

    // ── 4. 关闭连接 ──────────────────────────────────────────────
    close(sock);
}

// ── 程序入口 ─────────────────────────────────────────────────────
int main() {
    std::cout << "\n============================================" << std::endl;
    std::cout << "  FileServer - 客户端并发测试" << std::endl;
    std::cout << "  目标: " << kServerIP << ":" << kServerPort << std::endl;
    std::cout << "  并发客户端数: " << kNumClients << std::endl;
    std::cout << "============================================\n" << std::endl;

    // ── 启动 N 个客户端线程 ──────────────────────────────────────
    std::vector<std::thread> threads;
    threads.reserve(kNumClients);

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < kNumClients; ++i) {
        threads.emplace_back(ClientTask, i + 1);
    }

    // ── 等待所有线程完成 ─────────────────────────────────────────
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end_time - start_time).count();

    // ── 输出统计结果 ─────────────────────────────────────────────
    std::cout << "\n============================================" << std::endl;
    std::cout << "  测试结果" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "  连接成功: " << g_connect_ok.load() << "/" << kNumClients << std::endl;
    std::cout << "  连接失败: " << g_connect_fail.load() << std::endl;
    std::cout << "  发送成功: " << g_send_ok.load() << "/" << kNumClients << std::endl;
    std::cout << "  发送失败: " << g_send_fail.load() << std::endl;
    std::cout << "  接收成功: " << g_recv_ok.load() << "/" << kNumClients << std::endl;
    std::cout << "  接收失败: " << g_recv_fail.load() << std::endl;
    std::cout << "  耗时: " << elapsed_ms << " ms" << std::endl;
    std::cout << "============================================" << std::endl;

    // ── 判断是否全部通过 ─────────────────────────────────────────
    bool all_pass = (g_connect_ok.load() == kNumClients)
                 && (g_send_ok.load()    == kNumClients)
                 && (g_recv_ok.load()    == kNumClients);

    if (all_pass) {
        std::cout << "  全部测试通过!" << std::endl;
    } else {
        std::cout << "  存在失败项，请检查服务端是否正常运行" << std::endl;
    }
    std::cout << "============================================\n" << std::endl;

    return all_pass ? 0 : 1;
}
