#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <vector>
#include <thread>
#include <utility>


struct TaskWithPriority {
    // Define a special struct for tasks with priority as a parameter
    // lower value = higher priority
    int priority;
    std::function<void()> task;
    bool operator>(const TaskWithPriority& other) const {
        return priority > other.priority;
    }
};

template <typename T>
class SafeQueue {
    // implements a task queue which supports synchronised updates and accesses
    // uses mutex and CVs to achieve synchronisation
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
    // implements a priority-based task queue which supports synchronised updates and accesses
    // uses mutex and CVs to achieve synchronisation
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
    std::atomic<bool> stop; // identifies when execution stops

public:
    explicit ThreadPool(int numThreads) : m_threads(numThreads), stop(false) {
        threads.resize(m_threads);
        for (int i = 0; i < m_threads; i++) {
            threads.emplace_back([this] {
                while (!stop) {
                    // Tries to pop a task from the task queue
                    std::function<void()> task;
                    tasks.try_pop(task);
                    if (task) {
                        task(); // Runs task if available
                    } else {
                        std::this_thread::yield(); // Nothing to do, yields CPU
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

        tasks.push([task]() -> void { (*task)(); }); // Moves task to queue

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
                    // Tries to pop a task from the task priority queue
                    TaskWithPriority task_item;
                    if (tasks.try_pop(task_item)) {
                        task_item.task(); // Runs task if available
                    } else {
                        std::this_thread::yield(); // Nothing to do, yields CPU
                    }
                }
            });
        }
    }

    ~PriorityThreadPool() {
        stop = true;
        tasks.shutdown();
        for (auto& th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    template<class F, class... Args>
    auto ExecuteTask(int priority, F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task_ptr->get_future();

        TaskWithPriority task_item;
        task_item.priority = priority;
        task_item.task = [task_ptr]() { (*task_ptr)(); };

        tasks.push(std::move(task_item)); // Moves task to priority queue after initialising TaskWithPriority object

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
                        // Tries to pop a task from its own task queue
                        if (task_queues[i]->try_pop(task)) {
                            task();
                        } else {
                            // Tries to steal from others' task queue
                            bool stolen = false;
                            for (int j = 0; j < m_threads; ++j) {
                                if (j != i && task_queues[j]->try_pop(task)) {
                                    stolen = true;
                                    task();
                                    break;
                                }
                            }
                            if (!stolen) {
                                std::this_thread::yield(); // Nothing to do, yields CPU
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
    
            int idx = rand() % m_threads; // Chooses random thread to push task
            task_queues[idx]->push([task]() { (*task)(); }); // Moves task to chosen thread's queue
    
            return res;
        }
    };
