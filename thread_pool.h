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
class Safe_Queue {
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

    void emplace(T const& val) {
        std::lock_guard<std::mutex> lock(mtx);
        q.emplace(val);
        cv.notify_one();
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }
};

template <typename T>
class SafePriorityQueue {
private:
    std::priority_queue<T, std::vector<T>, std::greater<T>> pq;
    std::condition_variable cv;
    std::mutex mtx;
    bool stop = false; // Add this

public:
    void push(T val) {
        std::lock_guard<std::mutex> lock(mtx);
        pq.push(std::move(val));
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> uLock(mtx);
        cv.wait(uLock, [this] { return stop || !pq.empty(); });

        if (stop && pq.empty()) {
            throw std::runtime_error("Queue stopped");
        }

        T top_task = std::move(const_cast<T&>(pq.top()));
        pq.pop();
        return top_task;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mtx);
        stop = true;
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
    Safe_Queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;

public:
    explicit ThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
        for (int i = 0; i < m_threads; i++) {
            threads.emplace_back([this] {
                std::function<void()> task;
                while (1) {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this] {
                        return !tasks.empty() || stop;
                        });
                    if (stop)
                        return;
                    task = std::move(tasks.pop());
                    lock.unlock();
                    task();
                }
                });
        }
    }

    ~ThreadPool() {
        std::unique_lock<std::mutex> lock(mtx);
        stop = true;
        lock.unlock();
        cv.notify_all();

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

        std::unique_lock<std::mutex> lock(mtx);
        tasks.emplace([task]() -> void { (*task)(); });
        lock.unlock();
        cv.notify_one();
        return res;
    }
};


class PriorityThreadPool {
private:
    int m_threads;
    std::vector<std::thread> threads;
    SafePriorityQueue<TaskWithPriority> tasks;
    std::mutex pool_mutex; 
    std::condition_variable pool_cv; 
    bool stop;

public:
     explicit PriorityThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
        for (int i = 0; i < m_threads; ++i) {
            threads.emplace_back([this] {
                TaskWithPriority task_item;
                while (true) {
                   try {
                        task_item = tasks.pop();
                    } catch (const std::runtime_error&) {
                        return; 
                    }
                    task_item.task();
                            }
                        });
                }
    }

    ~PriorityThreadPool() {
        {
            std::unique_lock<std::mutex> lock(pool_mutex);
            stop = true;
        }
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

