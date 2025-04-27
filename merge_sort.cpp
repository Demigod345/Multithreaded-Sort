#include <benchmark/benchmark.h>
#include <execution>
#include <thread>
#include <vector>
#include <iostream>
#include "thread_pool.h"

void create_random_vector(std::vector<int>& vec, int size = 1e6) {
    vec.resize(size);
    for (int i = 0; i < size; ++i) {
        vec[i] = rand() % 10000;
    }
}

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

template <typename T>
void mergeSort(std::vector<T>& vec, int left, int right) {
    if (left < right) {
        int mid = left + (right - left) / 2;

        mergeSort(vec, left, mid);
        mergeSort(vec, mid + 1, right);

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void simple_parallel_merge_sort(std::vector<T>& vec, int left, int right) {
    if (right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        std::thread t1([&vec, left, mid]() {
            simple_parallel_merge_sort(vec, left, mid);
        });
        simple_parallel_merge_sort(vec, mid + 1, right);

        t1.join();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void parallelMergeSort(std::vector<T>& vec, int left, int right, int depth, ThreadPool& pool) {
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        auto future_left = pool.ExecuteTask([&vec, left, mid, depth, &pool]() {
            parallelMergeSort(vec, left, mid, depth - 1, pool);
            });

        parallelMergeSort(vec, mid + 1, right, depth - 1, pool);

        future_left.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void opParallelMergeSort(std::vector<T>& vec, int left, int right, int depth, ThreadPool& pool) {
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        auto future_left = pool.ExecuteTask([&vec, left, mid, depth, &pool]() {
            parallelMergeSort(vec, left, mid, depth - 1, pool);
            });

        int right_mid = mid + 1;
        auto future_right = pool.ExecuteTask([&vec, right_mid, right, depth, &pool]() {
            parallelMergeSort(vec,right_mid, right, depth - 1, pool);
        });


        future_left.wait();
        future_right.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void priorityMergeSort(std::vector<T>& vec, int left, int right, int depth, PriorityThreadPool& pool) {
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;
        int task_priority = depth;
        auto future_left = pool.ExecuteTask(task_priority,[&vec, left, mid, depth, &pool]() {
            priorityMergeSort(vec, left, mid, depth - 1, pool);
            });

        int right_mid = mid + 1; 
        auto future_right = pool.ExecuteTask(task_priority, [&vec, right_mid, right, depth, &pool]() {
            priorityMergeSort(vec, right_mid, right, depth - 1, pool);
        });

        future_left.wait();
        future_right.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
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

static void BM_StdSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));

        state.ResumeTiming();

        std::sort(vec.begin(), vec.end());
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

static void BM_ParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        ThreadPool pool(24);
        state.ResumeTiming();

        parallelMergeSort(vec, 0, vec.size() - 1, 5, pool);
    }
    state.SetComplexityN(state.range(0));
}

static void BM_OpParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        ThreadPool pool(24);
        state.ResumeTiming();

        opParallelMergeSort(vec, 0, vec.size() - 1, 5, pool);
    }
    state.SetComplexityN(state.range(0));
}

static void BM_PriorityMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        PriorityThreadPool pool(24);
        state.ResumeTiming();

        priorityMergeSort(vec, 0, vec.size() - 1, 5, pool);
    }
    state.SetComplexityN(state.range(0));
}

BENCHMARK(BM_StdSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_SeqMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_SimpleParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_ParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_OpParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_PriorityMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK_MAIN();



// int main(){
//     std::vector<int> vec = {5, 2, 4, 1, 3, 6, 8, 7};
//     std::cout << "Original vector: ";
//     ThreadPool pool(24);
//     std::cout << "Original vector: ";

//     opParallelMergeSort(vec, 0, vec.size() - 1, 5, pool);
//     for(int i = 0; i < vec.size(); i++) {
//         std::cout << vec[i] << " ";
//     }
// }