#ifndef NEWSSCOPE_THREAD_POOL_H
#define NEWSSCOPE_THREAD_POOL_H

#include "types.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>
#include <vector>
#include <stdexcept>

namespace newsscope {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_workers = 32);
    ~ThreadPool();
    
    template<typename Func, typename... Args>
    void enqueue(Func&& f, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<Func>(f), std::forward<Args>(args)...);
        }
        condition.notify_one();
    }
    
    void wait_for_all();
    size_t pending_task_count() const;
    size_t pool_size() const { return num_workers; }
    void shutdown();
    bool is_shutdown() const { return shutdown_flag; }
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable done_condition;
    
    size_t num_workers;
    bool shutdown_flag = false;
    size_t active_tasks = 0;
    
    void worker_loop();
};

}

#endif
