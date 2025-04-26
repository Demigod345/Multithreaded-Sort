#include <benchmark/benchmark.h>
#include <execution>
#include <iostream>
#include <vector>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <memory>
#include <algorithm>

// Safe Queue implementation that was missing from your code
template <typename T>
class Safe_Queue {
private:
    std::queue<T> q;               // Underlying queue to store elements
    std::condition_variable cv;    // Condition variable for synchronization
    std::mutex mtx;                // Mutex for exclusive access to the queue

public:
    // Pushes an element onto the queue
    void push(T const& val) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(val);
        cv.notify_one();  // Notify one waiting thread that data is available
    }

    // Pops and returns the front element of the queue
    T pop() {
        std::unique_lock<std::mutex> uLock(mtx);
        cv.wait(uLock, [&] { return !q.empty(); });  // Wait until the queue is not empty
        T front = q.front();
        q.pop();
        return front;
    }

    void emplace(T const& val) {
        std::lock_guard<std::mutex> lock(mtx);
        q.emplace(val);
        cv.notify_one();  // Notify one waiting thread that data is available
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }


};

// ThreadPool implementation with fixed typo in forward
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

// Merge function to merge two sorted subarrays
template <typename T>
void merge(std::vector<T>& vec, int left, int mid, int right) {
    int n1 = mid - left + 1;
    int n2 = right - mid;

    std::vector<T> L(n1), R(n2);

    for (int i = 0; i < n1; i++)
        L[i] = vec[left + i];
    for (int j = 0; j < n2; j++)
        R[j] = vec[mid + 1 + j];

    int i = 0, j = 0, k = left;
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            vec[k] = L[i];
            i++;
        }
        else {
            vec[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        vec[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) {
        vec[k] = R[j];
        j++;
        k++;
    }
}

// Sequential merge sort
template <typename T>
void mergeSort(std::vector<T>& vec, int left, int right) {
    if (left < right) {
        int mid = left + (right - left) / 2;

        mergeSort(vec, left, mid);
        mergeSort(vec, mid + 1, right);

        merge(vec, left, mid, right);
    }
}

// Parallel merge sort using ThreadPool
template <typename T>
void parallelMergeSort(std::vector<T>& vec, int left, int right, int depth, ThreadPool& pool) {
    if (depth <= 0 || right - left <= 1000) { // Switch to sequential for small arrays or at max depth
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        // Execute left half in a new task
        auto future_left = pool.ExecuteTask([&vec, left, mid, depth, &pool]() {
            parallelMergeSort(vec, left, mid, depth - 1, pool);
            });

        // Process right half in current thread
        parallelMergeSort(vec, mid + 1, right, depth - 1, pool);

        // Wait for left half to complete
        future_left.wait();

        // Merge the results
        merge(vec, left, mid, right);
    }
}

void create_random_vector(std::vector<int>& vec, int size = 1e6) {
    vec.resize(size);
    for (int i = 0; i < size; ++i) {
        vec[i] = rand() % 10000;
    }
}

void simple_parallel_merge_sort(std::vector<int>& vec, int left, int right) {
    if(right-left <= 8096) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        std::thread t1(simple_parallel_merge_sort, std::ref(vec), left, mid);
        simple_parallel_merge_sort(vec, mid + 1, right);

        t1.join();

        merge(vec, left, mid, right);
    }
}

static void BM_SeqMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));

        state.ResumeTiming();

        mergeSort(vec, 0, vec.size() - 1);
    }
    state.SetComplexityN(state.range(0));
}

static void BM_ParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        ThreadPool pool(24);
        // int depth = 0;
        // int temp = 12;
        // while (temp > 0) {
        //     depth++;
        //     temp >>= 1;
        // }

        state.ResumeTiming();
        
        parallelMergeSort(vec, 0, vec.size() - 1, 5, pool);
    }
    state.SetComplexityN(state.range(0));
}

static void BM_SimpleParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        state.ResumeTiming();
        
        simple_parallel_merge_sort(vec, 0, vec.size() - 1);
    }
    state.SetComplexityN(state.range(0));
}

BENCHMARK(BM_SeqMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_ParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_SimpleParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);

BENCHMARK_MAIN();