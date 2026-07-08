#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <cassert>

#include "threadpool/thread_pool.h"

using namespace fileserver;

// ── 测试: 提交 10000 个任务，验证线程安全和并发正确性 ──────────────
int main() {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  FileServer – 线程池压力测试" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    const int kNumWorkers = 8;
    const int kNumTasks   = 10000;

    ThreadPool pool(kNumWorkers);
    std::cout << "  Worker 线程数: " << pool.WorkerCount() << std::endl;
    std::cout << "  提交任务数:    " << kNumTasks << std::endl;

    std::atomic<int> counter{0};
    std::atomic<int> error_count{0};

    std::vector<std::future<int>> futures;
    futures.reserve(kNumTasks);

    auto start = std::chrono::steady_clock::now();

    // ── 提交 10000 个任务 ───────────────────────────────────────
    for (int i = 0; i < kNumTasks; ++i) {
        try {
            auto future = pool.Submit([i, &counter, &error_count]() -> int {
                // 模拟轻微计算
                int value = i * i;
                counter.fetch_add(1, std::memory_order_relaxed);

                // 验证不会出现数据竞争
                if (value < 0) {
                    error_count.fetch_add(1);
                }
                return value;
            });
            futures.push_back(std::move(future));
        } catch (const std::exception& e) {
            std::cerr << "  提交任务 " << i << " 失败: " << e.what() << std::endl;
            error_count.fetch_add(1);
        }
    }

    std::cout << "  全部任务已提交" << std::endl;

    // ── 等待所有任务完成 ────────────────────────────────────────
    int64_t sum = 0;
    for (int i = 0; i < static_cast<int>(futures.size()); ++i) {
        try {
            int result = futures[i].get();
            sum += result;
        } catch (const std::exception& e) {
            std::cerr << "  任务 " << i << " 执行异常: " << e.what() << std::endl;
            error_count.fetch_add(1);
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end - start).count();

    // ── 输出统计 ────────────────────────────────────────────────
    std::cout << "\n  ============ 测试结果 ============" << std::endl;
    std::cout << "  完成任务数: " << counter.load() << " / " << kNumTasks << std::endl;
    std::cout << "  错误数:     " << error_count.load() << std::endl;
    std::cout << "  总耗时:     " << elapsed_ms << " ms" << std::endl;
    std::cout << "  平均每任务: "
              << (elapsed_ms > 0 ? static_cast<double>(elapsed_ms) / kNumTasks : 0)
              << " ms" << std::endl;
    std::cout << "  吞吐量:     "
              << (elapsed_ms > 0 ? kNumTasks * 1000.0 / elapsed_ms : 0)
              << " tasks/s" << std::endl;

    // ── 验证 ────────────────────────────────────────────────────
    bool pass = (counter.load() == kNumTasks) && (error_count.load() == 0);

    if (pass) {
        std::cout << "\n  [通过] 10000 个任务全部成功，线程安全验证通过\n" << std::endl;
    } else {
        std::cout << "\n  [失败] 存在未完成或异常的任务\n" << std::endl;
    }

    std::cout << std::string(60, '=') << "\n" << std::endl;

    return pass ? 0 : 1;
}
