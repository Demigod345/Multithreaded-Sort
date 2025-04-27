#include <benchmark/benchmark.h>
#include <execution>
#include <thread>
#include <vector>
#include <iostream>
#include "thread_pool.h"

void create_random_vector(std::vector<int>& vec, int size = 1e6) {
    vec.resize(size); // Set vector size
    for (int i = 0; i < size; ++i) {
        vec[i] = rand() % 10000; // Assign random value
    }
}

template <typename T>
void mergeSort(std::vector<T>& vec, int left, int right) {
    // Sequential Merge Sort
    if (left < right) {
        int mid = left + (right - left) / 2; // Find midpoint
        
        mergeSort(vec, left, mid); // Sort left half
        mergeSort(vec, mid + 1, right); // Sort right half
        
        // Merge using STL's inplace_merge
        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}


template <typename T>
void simple_parallel_merge_sort(std::vector<T>& vec, int left, int right, int depth) {
    // To control number of threads spawned
    if (right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        // Initialize new thread for left half
        std::thread t1([&vec, left, mid, depth]() {
            simple_parallel_merge_sort(vec, left, mid, depth - 1);
        });
        simple_parallel_merge_sort(vec, mid + 1, right, depth - 1);

        t1.join();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void parallelMergeSort(std::vector<T>& vec, int left, int right, int depth, ThreadPool& pool) {
    // To control number of threads spawned
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        // Submit left half as a task to the basic thread pool
        auto future_left = pool.ExecuteTask([&vec, left, mid, depth, &pool]() {
            parallelMergeSort(vec, left, mid, depth - 1, pool);
        });
        parallelMergeSort(vec, mid + 1, right, depth - 1, pool);

        future_left.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void priorityMergeSort(std::vector<T>& vec, int left, int right, int depth, PriorityThreadPool& pool) {
    // To control number of threads spawned
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;
        int task_priority = depth;

        // Submit left half as a task to the priority-based thread pool, with depth as the priority
        auto future_left = pool.ExecuteTask(task_priority,[&vec, left, mid, depth, &pool]() {
            priorityMergeSort(vec, left, mid, depth - 1, pool);
        });
        priorityMergeSort(vec, mid + 1, right, depth - 1, pool);
        
        future_left.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void OMPParallelMergeSort(std::vector<T>& vec, int left, int right) {
    // To control number of threads spawned
    if (right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }
    
    if (left < right) {
        int mid = left + (right - left) / 2;

        // To use OMP for parallelisation
        #pragma omp parallel sections
        {
            #pragma omp section
            {
                OMPParallelMergeSort(vec, left, mid);
            }
            #pragma omp section
            {
                OMPParallelMergeSort(vec, mid + 1, right);
            }
        }

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

template <typename T>
void PMParallelMergeSort(std::vector<T>& vec, int left, int right, int depth, PMThreadPool& pool) {
    // To control number of threads spawned
    if (depth <= 0 || right - left <= 1000) {
        mergeSort(vec, left, right);
        return;
    }

    if (left < right) {
        int mid = left + (right - left) / 2;

        // Submit left half as a task to the pull-based migration thread pool
        auto future_left = pool.ExecuteTask([&vec, left, mid, depth, &pool]() {
            PMParallelMergeSort(vec, left, mid, depth - 1, pool);
        });
        PMParallelMergeSort(vec, mid + 1, right, depth - 1, pool);

        future_left.wait();

        std::inplace_merge(vec.begin() + left, vec.begin() + mid + 1, vec.begin() + right + 1);
    }
}

// Initialise calls for each benchmarks, defining recursive depth and initialising thread pools wherever needed
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

        simple_parallel_merge_sort(vec, 0, vec.size() - 1, 5);
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

static void BM_PMParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        PMThreadPool pool(24);
        state.ResumeTiming();

        PMParallelMergeSort(vec, 0, vec.size() - 1, 5, pool);
    }
    state.SetComplexityN(state.range(0));
}

static void BM_OMPParMergeSort(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();

        std::vector<int> vec;
        create_random_vector(vec, state.range(0));
        state.ResumeTiming();

        OMPParallelMergeSort(vec, 0, vec.size() - 1);
    }
    state.SetComplexityN(state.range(0));
}

// Calling the individual benchmarks
BENCHMARK(BM_StdSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_SeqMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_SimpleParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_ParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_PMParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_PriorityMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);
BENCHMARK(BM_OMPParMergeSort)->RangeMultiplier(2)->Range(1 << 10, 1 << 20)->Complexity(benchmark::oNLogN);

// Calling the main function from benchmark library
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
