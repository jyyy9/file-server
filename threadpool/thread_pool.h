#ifndef FILESERVER_THREADPOOL_THREAD_POOL_H_
#define FILESERVER_THREADPOOL_THREAD_POOL_H_

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <type_traits>

namespace fileserver {

// ── 固定大小线程池 ───────────────────────────────────────────────
//
// 用途：将阻塞操作（数据库IO、文件IO、RPC调用）从 Reactor 事件循环
//       线程中剥离，避免阻塞 epoll 事件处理，保证其他客户端的响应。
//
// 特性：
//   - 固定数量的工作线程，避免频繁创建/销毁开销
//   - 任务队列 + 条件变量实现生产者-消费者模型
//   - Submit() 返回 std::future，支持异步获取结果
//   - 线程安全
class ThreadPool {
public:
    // 构造函数
    // num_threads: 工作线程数量，0表示自动检测CPU核心数
    explicit ThreadPool(size_t num_threads);

    // 析构函数：通知所有线程退出并等待join
    ~ThreadPool();

    // 提交一个可调用对象到线程池
    // 返回 std::future，调用者可通过 .get() 阻塞等待结果
    template <typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // 获取当前工作线程数量
    size_t WorkerCount() const { return workers_.size(); }

    // 获取当前等待中的任务数量
    size_t TaskCount();

private:
    // 工作线程主循环
    void WorkerLoop();

    std::vector<std::thread> workers_;              // 工作线程列表
    std::queue<std::function<void()>> tasks_;       // 任务队列
    std::mutex mutex_;                              // 互斥锁
    std::condition_variable cond_;                  // 条件变量
    std::atomic<bool> running_{true};               // 运行标志
};

// ── 模板实现 ─────────────────────────────────────────────────────
template <typename F, typename... Args>
auto ThreadPool::Submit(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {

    using ReturnType = typename std::result_of<F(Args...)>::type;

    // 将任务包装为 packaged_task，以便获取 future
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> future = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            throw std::runtime_error("ThreadPool 已停止，无法提交新任务");
        }
        tasks_.emplace([task]() { (*task)(); });
    }

    cond_.notify_one();   // 唤醒一个等待中的工作线程
    return future;
}

}  // namespace fileserver

#endif  // FILESERVER_THREADPOOL_THREAD_POOL_H_