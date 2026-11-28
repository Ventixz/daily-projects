// Fixed-size worker pool. The event loop is single threaded; the only work it
// hands off is freeing very large sorted sets, which would otherwise stall
// every client for the time it takes to walk and free millions of nodes.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t nthreads);
    ~ThreadPool();  // runs every queued job, then joins the workers
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // With zero threads the job runs inline, which keeps tests deterministic.
    void submit(std::function<void()> job);
    size_t threads() const { return threads_.size(); }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    bool stopping_ = false;

    void worker();
};
