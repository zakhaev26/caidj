#include "caidj/util/thread_pool.hpp"

namespace caidj::util
{

    ThreadPool::ThreadPool(size_t threads)
    {
        threads = threads == 0 ? 1 : threads;
        workers_.reserve(threads);
        for (size_t i = 0; i < threads; ++i)
        {
            workers_.emplace_back([this]()
                                  {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            } });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

} // namespace caidj::util