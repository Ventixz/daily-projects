// Fixed-size worker pool. The server uses it for one job: freeing very large
// sorted sets off the event-loop thread, so DEL on a million-member set does
// not stall every other client.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t nthreads);
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
    std::vector<std::thread> threads_;
    size_t running_ = 0;
    bool stop_ = false;
};
