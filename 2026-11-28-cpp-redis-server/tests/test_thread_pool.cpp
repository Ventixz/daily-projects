#include <atomic>
#include <thread>

#include "testing.h"
#include "thread_pool.h"

TEST(thread_pool_runs_every_job_before_destruction) {
    std::atomic<int> done{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; i++) pool.submit([&] { done.fetch_add(1); });
    }  // destructor drains the queue
    CHECK_EQ(done.load(), 1000);
}

TEST(thread_pool_jobs_run_off_the_caller_thread) {
    std::atomic<bool> other{false};
    auto caller = std::this_thread::get_id();
    {
        ThreadPool pool(1);
        pool.submit([&] { other = std::this_thread::get_id() != caller; });
    }
    CHECK(other.load());
}

TEST(thread_pool_with_zero_threads_runs_inline) {
    ThreadPool pool(0);
    int x = 0;
    pool.submit([&] { x = 1; });
    CHECK_EQ(x, 1);
}
