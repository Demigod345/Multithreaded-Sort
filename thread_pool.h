#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <vector> // Added for priority queue underlying container
#include <thread> // Added for std::thread
#include <utility> // Added for std::forward, std::move, std::bind


struct TaskWithPriority {
    int priority; // Lower number = higher priority
    std::function<void()> task;
    bool operator>(const TaskWithPriority& other) const {
        return priority > other.priority;
    }
};

template <typename T>
class SafeQueue {
private:
    std::queue<T> q;
    std::condition_variable cv;
    std::mutex mtx;

public:
    void push(T const& val) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(val);
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> uLock(mtx);
        cv.wait(uLock, [&] { return !q.empty(); });
        T front = q.front();
        q.pop();
        return front;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }

    bool try_pop(T& val) {
        std::lock_guard<std::mutex> lock(mtx);
        if (q.empty())
            return false;
        val = q.front();
        q.pop();
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mtx);
        cv.notify_all();
    }
};

template <typename T>
class SafePriorityQueue {
private:
    std::priority_queue<T, std::vector<T>, std::greater<T>> pq;
    std::condition_variable cv;
    std::mutex mtx;

public:
    void push(T val) {
        std::lock_guard<std::mutex> lock(mtx);
        pq.push(std::move(val));
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> uLock(mtx);
        cv.wait(uLock, [this] { return !pq.empty(); });

        T top_task = std::move(const_cast<T&>(pq.top()));
        pq.pop();
        return top_task;
    }

    bool try_pop(T& val) {
        std::lock_guard<std::mutex> lock(mtx);
        if (pq.empty())
            return false;
        val = pq.top();
        pq.pop();
        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mtx);
        cv.notify_all();
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return pq.empty();
    }
};

class ThreadPool {
private:
    int m_threads;
    std::vector<std::thread> threads;
    SafeQueue<std::function<void()>> tasks;
    std::atomic<bool> stop;

public:
    explicit ThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
        threads.resize(m_threads);
        for (int i = 0; i < m_threads; i++) {
            threads.emplace_back([this] {
                while (!stop) {
                    std::function<void()> task;
                    tasks.try_pop(task);
                    if (task) {
                        task();
                    } else {
                        std::this_thread::yield(); // Nothing to do, yield CPU
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        stop = true;
        tasks.shutdown();
        for (auto& th : threads) {
            if (th.joinable())
                th.join();
        }
    }

    template<class F, class... Args>
    auto ExecuteTask(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        
        std::future<return_type> res = task->get_future();

        tasks.push([task]() -> void { (*task)(); });

        return res;
    }
};


class PriorityThreadPool {
private:
    int m_threads;
    std::vector<std::thread> threads;
    SafePriorityQueue<TaskWithPriority> tasks;
    std::atomic<bool> stop;

public:
     explicit PriorityThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
        threads.resize(m_threads);
        for (int i = 0; i < m_threads; ++i) {
            threads.emplace_back([this] {
                while (!stop) {
                    TaskWithPriority task_item;
                    if (tasks.try_pop(task_item)) {
                        task_item.task();
                    } else {
                        std::this_thread::yield(); // Nothing to do, yield CPU
                    }
                }
            });
        }
    }

    ~PriorityThreadPool() {
        stop = true;
        tasks.shutdown(); // <--- important
        for (auto& th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    // ExecuteTask now takes a priority
    template<class F, class... Args>
    auto ExecuteTask(int priority, F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        // Create a packaged_task
        auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // Get the future associated with the packaged_task
        std::future<return_type> res = task_ptr->get_future();

        // Create the TaskWithPriority object
        TaskWithPriority task_item;
        task_item.priority = priority;
        // Wrap the packaged_task execution in a lambda
        task_item.task = [task_ptr]() { (*task_ptr)(); };

        // Push the TaskWithPriority object into the safe priority queue
        tasks.push(std::move(task_item)); // Use move

        return res;
    }
};

class PMThreadPool {
    private:
        int m_threads;
        std::vector<std::thread> threads;
        std::vector<std::unique_ptr<SafeQueue<std::function<void()>>>> task_queues;
        std::atomic<bool> stop;
    
    public:
        explicit PMThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
            task_queues.resize(m_threads);
            for (int i = 0; i < m_threads; ++i) {
                task_queues[i] = std::make_unique<SafeQueue<std::function<void()>>>();
            }
            for (int i = 0; i < m_threads; ++i) {
                threads.emplace_back([this, i]() {
                    while (!stop) {
                        std::function<void()> task;
                        // Try to pop from own queue
                        if (task_queues[i]->try_pop(task)) {
                            task();
                        } else {
                            // Try to steal from others
                            bool stolen = false;
                            for (int j = 0; j < m_threads; ++j) {
                                if (j != i && task_queues[j]->try_pop(task)) {
                                    stolen = true;
                                    task();
                                    break;
                                }
                            }
                            if (!stolen) {
                                std::this_thread::yield(); // Nothing to do, yield CPU
                            }
                        }
                    }
                });
            }
        }
    
        ~PMThreadPool() {
            stop = true;
            for (auto& queue : task_queues) {
                queue->shutdown();
            }
            for (auto& th : threads) {
                th.join();
            }
        }
    
        template<class F, class... Args>
        auto ExecuteTask(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
            using return_type = decltype(f(args...));
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    
            std::future<return_type> res = task->get_future();
    
            // Randomly pick a thread's queue to push into
            int idx = rand() % m_threads;
            task_queues[idx]->push([task]() { (*task)(); });
    
            return res;
        }
    };
