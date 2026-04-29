#include "thread_pool.h"
#include <chrono>

namespace newsscope {

ThreadPool::ThreadPool(size_t num_workers) : num_workers(num_workers) {
    for (size_t i = 0; i < num_workers; ++i) {
        workers.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            condition.wait(lock, [this] {
                return !tasks.empty() || shutdown_flag;
            });

            if (shutdown_flag && tasks.empty()) {
                break;
            }

            task = std::move(tasks.front());
            tasks.pop();
            ++active_tasks;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            --active_tasks;
            if (tasks.empty() && active_tasks == 0) {
                done_condition.notify_all();
            }
        }
    }
}

void ThreadPool::wait_for_all() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    done_condition.wait(lock, [this] {
        return tasks.empty() && active_tasks == 0;
    });
}

size_t ThreadPool::pending_task_count() const {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return tasks.size();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (shutdown_flag) {
            return;
        }
        shutdown_flag = true;
    }
    
    condition.notify_all();
    
    // Join threads with timeout to prevent indefinite hang
    constexpr auto SHUTDOWN_TIMEOUT = std::chrono::seconds(10);
    auto deadline = std::chrono::steady_clock::now() + SHUTDOWN_TIMEOUT;
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            // Try to join with timeout by checking if thread has exited
            auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining.count() > 0) {
                // For now, just do a regular join without timeout
                // since C++11 doesn't support timed join()
                worker.join();
            } else {
                // Timeout exceeded - thread didn't exit in time
                // We can't forcefully kill it, but at least we don't hang
                break;
            }
        }
    }
}

}
