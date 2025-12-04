// SchedulingToys.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <iostream>
#include <barrier>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <format>
#include <print>

struct thread_context
{
    std::thread thread;
    bool highPriority;
    int threadIndex;
    int arriveOrder;
    int execOrder;
};

struct critical_section
{
    CRITICAL_SECTION cs{};
    void lock()
    {
        EnterCriticalSection(&cs);
    }
    void unlock()
    {
        LeaveCriticalSection(&cs);
    }
    critical_section()
    {
        InitializeCriticalSection(&cs);
    }
    ~critical_section()
    {
        DeleteCriticalSection(&cs);
    }
};

struct srw_lock
{
    SRWLOCK srwlock{};
    void lock()
    {
        AcquireSRWLockExclusive(&srwlock);
    }
    void unlock()
    {
        ReleaseSRWLockExclusive(&srwlock);
    }
};

struct nt_mutex
{
    HANDLE hMutex{ nullptr };
    nt_mutex()
    {
        hMutex = ::CreateMutexA(nullptr, FALSE, nullptr);
    }
    void lock()
    {
        WaitForSingleObject(hMutex, INFINITE);
    }
    void unlock()
    {
        ReleaseMutex(hMutex);
    }
};

template<typename TPrimitive, size_t num_threads>
void test_thread_ordering(char const* primitive_name)
{
    int execOrder = 0;
    std::atomic<int> arriveOrder = 0;
    std::barrier sync_point(num_threads);
    thread_context threads[num_threads];
    TPrimitive primitive;

    for (int i = 0; i < num_threads; ++i) {
        threads[i].threadIndex = i;
        threads[i].thread = std::thread([i, &threads, &execOrder, &primitive, &arriveOrder, &sync_point]() {
            threads[i].highPriority = (i % 3 == 0);
            if (threads[i].highPriority) {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            }
            else {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            }
            threads[i].arriveOrder = ++arriveOrder;
            sync_point.arrive_and_wait();

            std::lock_guard lock(primitive);
            threads[i].execOrder = ++execOrder;
        });
    }

    for (auto& t : threads) {
        t.thread.join();
    }

    std::sort(std::begin(threads), std::end(threads), [](const thread_context& a, const thread_context& b) {
        return a.execOrder < b.execOrder;
        });

    for (const auto& t : threads) {
        std::cout << std::format("{} thread {:5} order arrive {:5} exec {:5} high-priority {}\n", primitive_name, t.threadIndex, t.arriveOrder, t.execOrder, t.highPriority);
    }
}

int test_queue_processor(bool inheritThreadPriority);

int main()
{
    test_queue_processor(false);
    test_queue_processor(true);
    test_thread_ordering<std::mutex, 50>("mutex");
    test_thread_ordering<std::recursive_mutex, 50>("recursive mutex");
    test_thread_ordering<std::shared_mutex, 50>("shared mutex");
    test_thread_ordering<critical_section, 50>("critical section");
    test_thread_ordering<srw_lock, 50>("SRW lock");
    test_thread_ordering<nt_mutex, 50>("Windows Mutex");
}
