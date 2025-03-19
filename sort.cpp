#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <chrono>

using namespace std;

// Thread Pool Class
class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queueMutex;
    condition_variable condition;
    bool stop;

public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });

                        if (stop && tasks.empty()) return;
                        task = move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    void enqueue(function<void()> task) {
        {
            unique_lock<mutex> lock(queueMutex);
            tasks.push(move(task));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers) worker.join();
    }
};

// Merge Function
void merge(vector<int>& arr, int left, int mid, int right) {
    int n1 = mid - left + 1, n2 = right - mid;
    vector<int> L(n1), R(n2);

    for (int i = 0; i < n1; i++) L[i] = arr[left + i];
    for (int i = 0; i < n2; i++) R[i] = arr[mid + 1 + i];

    int i = 0, j = 0, k = left;
    while (i < n1 && j < n2) arr[k++] = (L[i] <= R[j]) ? L[i++] : R[j++];
    while (i < n1) arr[k++] = L[i++];
    while (j < n2) arr[k++] = R[j++];
}

// Multithreaded Merge Sort
void mergeSort(vector<int>& arr, int left, int right, ThreadPool& pool, int depth = 0) {
    if (left >= right) return;

    int mid = left + (right - left) / 2;

    if (depth < 3) {  // Limit depth to prevent excessive threads
        auto leftTask = [&arr, left, mid, &pool, depth] { mergeSort(arr, left, mid, pool, depth + 1); };
        auto rightTask = [&arr, mid, right, &pool, depth] { mergeSort(arr, mid + 1, right, pool, depth + 1); };

        pool.enqueue(leftTask);
        pool.enqueue(rightTask);

        // Wait for these tasks to complete before merging
        this_thread::sleep_for(chrono::milliseconds(10));  
    } else {
        mergeSort(arr, left, mid, pool, depth + 1);
        mergeSort(arr, mid + 1, right, pool, depth + 1);
    }

    merge(arr, left, mid, right);
}

// Single-Threaded Merge Sort
void singleThreadMergeSort(vector<int>& arr, int left, int right) {
    if (left >= right) return;

    int mid = left + (right - left) / 2;
    singleThreadMergeSort(arr, left, mid);
    singleThreadMergeSort(arr, mid + 1, right);
    merge(arr, left, mid, right);
}

// Main function
int main() {
    size_t SIZE = 1e6;  // Use a large array for better comparison
    vector<int> arrMultiThread(SIZE), arrSingleThread(SIZE);

    // Fill arrays with random numbers
    for (size_t i = 0; i < SIZE; i++) {
        arrMultiThread[i] = arrSingleThread[i] = rand() % 100000;
    }

    // Multithreaded Sort
    ThreadPool pool(thread::hardware_concurrency());
    auto start = chrono::high_resolution_clock::now();
    mergeSort(arrMultiThread, 0, SIZE - 1, pool);
    auto end = chrono::high_resolution_clock::now();
    cout << "Multithreaded Sort Time: " << chrono::duration<double>(end - start).count() << " seconds\n";

    // Single-Threaded Sort
    start = chrono::high_resolution_clock::now();
    singleThreadMergeSort(arrSingleThread, 0, SIZE - 1);
    end = chrono::high_resolution_clock::now();
    cout << "Single-Threaded Sort Time: " << chrono::duration<double>(end - start).count() << " seconds\n";

    return 0;
}
