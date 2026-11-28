#include "thread_pool.h"

#include <utility>

ThreadPool::ThreadPool(size_t nthreads) {
    threads_.reserve(nthreads);
    for (size_t i = 0; i < nthreads; i++) threads_.emplace_back([this] { worker(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (std::thread &t : threads_) t.join();
}

void ThreadPool::submit(std::function<void()> job) {
    if (threads_.empty()) {
        job();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;  // stopping and drained
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job();
    }
}
