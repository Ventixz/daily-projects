// Fixed-size worker pool fed from a queue.
//
// The server is single-threaded on purpose, but freeing a sorted set with
// millions of members can stall every client for many milliseconds. Such
// containers are unlinked on the main thread (so nobody can reach them) and
// then handed to a worker to be destroyed.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t n_threads);
    ~ThreadPool();  // finishes queued jobs, then joins
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    void submit(std::function<void()> job);
    void wait_idle();  // blocks until the queue is empty and no job runs

private:
    void worker();

    std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> queue_;
    size_t running_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};
