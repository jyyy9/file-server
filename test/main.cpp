#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

#include "common/logger.h"
#include "common/common.h"
#include "threadpool/thread_pool.h"

using namespace fileserver;

// ── 测试: 日志模块 ───────────────────────────────────────────────
static void TestLogger() {
    std::cout << "\n=== 测试: 日志模块 ===\n" << std::endl;

    LOG_DEBUG("这是一条调试信息");
    LOG_INFO("这是一条普通信息");
    LOG_WARNING("这是一条警告信息");
    LOG_ERROR("这是一条错误信息");

    // 测试流式日志宏
    LOG_STREAM(Info) << "流式日志: answer = " << 42;

    // 测试日志级别过滤
    Logger::Instance().SetLevel(LogLevel::kWarning);
    LOG_DEBUG("这条调试信息不应该出现");
    LOG_WARNING("这条警告信息应该出现");
    LOG_ERROR("这条错误信息应该出现");

    // 恢复默认级别
    Logger::Instance().SetLevel(LogLevel::kDebug);

    std::cout << "\n[通过] 日志模块测试\n" << std::endl;
}

// ── 测试: 公共类型定义 ───────────────────────────────────────────
static void TestCommon() {
    std::cout << "\n=== 测试: 公共模块 ===\n" << std::endl;

    std::cout << "默认端口          = " << kDefaultPort << std::endl;
    std::cout << "缓冲区大小        = " << kBufferSize << std::endl;
    std::cout << "默认分块大小      = " << kDefaultChunkSize << std::endl;

    StringMap map;
    map["key1"] = "value1";
    map["key2"] = "value2";
    std::cout << "StringMap 大小    = " << map.size() << std::endl;

    std::cout << "\n[通过] 公共模块测试\n" << std::endl;
}

// ── 测试: 线程池 ─────────────────────────────────────────────────
static void TestThreadPool() {
    std::cout << "\n=== 测试: 线程池 ===\n" << std::endl;

    ThreadPool pool(4);
    std::cout << "工作线程数: " << pool.WorkerCount() << std::endl;

    // 提交几个简单任务
    auto future1 = pool.Submit([]() -> int {
        LOG_INFO("任务1 在工作线程上运行");
        return 100;
    });

    auto future2 = pool.Submit([](int a, int b) -> int {
        LOG_INFO("任务2 在工作线程上运行: " + std::to_string(a) + " + " + std::to_string(b));
        return a + b;
    }, 3, 7);

    auto future3 = pool.Submit([]() -> std::string {
        LOG_INFO("任务3 在工作线程上运行");
        return "hello from threadpool";
    });

    // 等待并验证结果
    assert(future1.get() == 100);
    assert(future2.get() == 10);
    assert(future3.get() == "hello from threadpool");

    // 批量提交任务，测试并发执行
    const int kNumTasks = 20;
    std::vector<std::future<int>> futures;
    for (int i = 0; i < kNumTasks; ++i) {
        futures.push_back(pool.Submit([i]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i * i;
        }));
    }

    for (int i = 0; i < kNumTasks; ++i) {
        assert(futures[i].get() == i * i);
    }

    std::cout << "\n[通过] 线程池测试 (" << kNumTasks << " 个任务)\n" << std::endl;
}

// ── 测试: 错误码枚举 ─────────────────────────────────────────────
static void TestErrorCodes() {
    std::cout << "\n=== 测试: 错误码 ===\n" << std::endl;

    auto printCode = [](ErrorCode code, const char* name) {
        std::cout << "  " << name << " = " << static_cast<int32_t>(code) << std::endl;
    };

    printCode(ErrorCode::kOk,          "kOk         (成功)");
    printCode(ErrorCode::kUnknown,     "kUnknown    (未知错误)");
    printCode(ErrorCode::kAuthFailed,  "kAuthFailed (认证失败)");
    printCode(ErrorCode::kNotFound,    "kNotFound   (未找到)");

    std::cout << "\n[通过] 错误码测试\n" << std::endl;
}

// ── 测试: 文件状态枚举 ───────────────────────────────────────────
static void TestFileStatus() {
    std::cout << "\n=== 测试: 文件状态 ===\n" << std::endl;

    std::cout << "  上传中 = " << static_cast<int32_t>(FileStatus::kUploading) << std::endl;
    std::cout << "  已完成 = " << static_cast<int32_t>(FileStatus::kCompleted) << std::endl;
    std::cout << "  已删除 = " << static_cast<int32_t>(FileStatus::kDeleted) << std::endl;

    std::cout << "\n[通过] 文件状态测试\n" << std::endl;
}

// ── 程序入口 ─────────────────────────────────────────────────────
int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer – 单元测试" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    try {
        TestLogger();
        TestCommon();
        TestErrorCodes();
        TestFileStatus();
        TestThreadPool();
    } catch (const std::exception& e) {
        LOG_FATAL(std::string("测试因异常而失败: ") + e.what());
        return 1;
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  全部测试通过" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    return 0;
}