#include "thread_pool.h"

ThreadPool::ThreadPool(size_t nthreads) {
    for (size_t i = 0; i < nthreads; i++) threads_.emplace_back([this] { worker(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    not_empty_.notify_all();
    for (auto &t : threads_) t.join();
}

void ThreadPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(std::move(job));
    }
    not_empty_.notify_one();
}

void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(mu_);
    idle_.wait(lock, [this] { return queue_.empty() && running_ == 0; });
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            not_empty_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) return;  // stop_ is set and nothing is left
            job = std::move(queue_.front());
            queue_.pop_front();
            running_++;
        }
        job();
        {
            std::lock_guard<std::mutex> lock(mu_);
            running_--;
            if (queue_.empty() && running_ == 0) idle_.notify_all();
        }
    }
}
