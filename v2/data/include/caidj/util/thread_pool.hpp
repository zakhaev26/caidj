#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace caidj::util
{

    class ThreadPool
    {
    public:
        explicit ThreadPool(size_t threads);
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        template <typename F>
        auto enqueue(F &&f) -> std::future<std::invoke_result_t<F>>
        {
            using R = std::invoke_result_t<F>;
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
            auto future = task->get_future();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tasks_.emplace([task]()
                               { (*task)(); });
            }
            cv_.notify_one();
            return future;
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool stop_ = false;
    };

} // namespace caidj::util