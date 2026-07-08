#include <iostream>
#include <csignal>
#include <string>

#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include "gateway/network/tcp_server_wrapper.h"
#include "common/logger.h"
#include "common/common.h"

using namespace fileserver;
using namespace fileserver::gateway::network;

// 全局指针，用于信号处理中优雅退出
muduo::net::EventLoop* g_loop = nullptr;
TcpServerWrapper* g_server = nullptr;

// ── 信号处理 ─────────────────────────────────────────────────────
void SignalHandler(int sig) {
    LOG_INFO("收到信号 " + std::to_string(sig) + "，正在关闭服务...");
    if (g_server) {
        g_server->Stop();
    }
    if (g_loop) {
        g_loop->quit();
    }
}

// ── 程序入口 ─────────────────────────────────────────────────────
int main() {
    // 设置日志级别
    Logger::Instance().SetLevel(LogLevel::kDebug);

    LOG_INFO("FileServer 服务端启动中...");

    // ── 主 Reactor（运行在 main 线程）─────────────────────────────
    muduo::net::EventLoop loop;
    g_loop = &loop;

    // ── 监听地址 ──────────────────────────────────────────────────
    muduo::net::InetAddress listen_addr(kDefaultPort);
    // 若指定了其他端口，可通过命令行参数或配置修改

    // ── 创建 TcpServerWrapper ─────────────────────────────────────
    //  参数说明:
    //    &loop        - 主 Reactor（处理 accept）
    //    listen_addr  - 监听地址
    //    "FileServer" - 服务名称
    //    4            - Sub Reactor 线程数（处理已连接 socket 的 IO 事件）
    TcpServerWrapper server(&loop, listen_addr, "FileServer", 4);
    g_server = &server;

    // ── 注册信号处理（Ctrl+C 优雅退出）───────────────────────────
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // ── 启动服务 ──────────────────────────────────────────────────
    server.Start();
    std::cout << "\n============================================" << std::endl;
    std::cout << "  FileServer 已启动" << std::endl;
    std::cout << "  监听端口: " << kDefaultPort << std::endl;
    std::cout << "  Sub Reactor 线程数: 4" << std::endl;
    std::cout << "  按 Ctrl+C 停止服务" << std::endl;
    std::cout << "============================================\n" << std::endl;

    // ── 进入事件循环（阻塞，直到 quit() 被调用）───────────────────
    loop.loop();

    LOG_INFO("FileServer 已退出");
    return 0;
}
