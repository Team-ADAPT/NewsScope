#include "thread_pool.h"

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
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace newsscope
