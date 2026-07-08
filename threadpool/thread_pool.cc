#include "threadpool/thread_pool.h"
#include "common/logger.h"

namespace fileserver {

ThreadPool::ThreadPool(size_t num_threads) {
    // 若未指定线程数，则自动检测CPU核心数
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;  // fallback
    }

    LOG_INFO("初始化线程池，工作线程数: " + std::to_string(num_threads));

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cond_.notify_all();   // 唤醒所有工作线程

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    LOG_INFO("线程池已销毁");
}

size_t ThreadPool::TaskCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 等待直到有任务或线程池停止
            cond_.wait(lock, [this] {
                return !running_ || !tasks_.empty();
            });

            // 线程池停止且任务队列为空时退出
            if (!running_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();  // 在锁外执行任务，避免阻塞其他线程
    }
}

}  // namespace fileserver