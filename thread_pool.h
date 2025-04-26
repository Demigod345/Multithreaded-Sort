#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>

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
