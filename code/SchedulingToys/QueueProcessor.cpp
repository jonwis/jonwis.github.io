#include <Windows.h>
#include <functional>
#include <atomic>
#include <thread>
#include <barrier>
#include <iostream>
#include <wil/result.h>
#include <wil/result_macros.h>

using unique_threadpool = wil::unique_any<PTP_POOL, decltype(&CloseThreadpool), CloseThreadpool>;
using unique_threadpool_work = wil::unique_any<PTP_WORK, decltype(&CloseThreadpoolWork), CloseThreadpoolWork>;
using unique_threadpool_cleanup_group = wil::unique_any<PTP_CLEANUP_GROUP, decltype(&CloseThreadpoolCleanupGroup), CloseThreadpoolCleanupGroup>;

class ThreadpoolQueueProcessor
{
public:
    ThreadpoolQueueProcessor()
    {
        m_threadpool.reset(CreateThreadpool(nullptr));
        THROW_IF_NULL_ALLOC(m_threadpool.get());

        m_cleanupGroup.reset(CreateThreadpoolCleanupGroup());
        THROW_IF_NULL_ALLOC(m_cleanupGroup.get());

        InitializeThreadpoolEnvironment(&m_normalPriorityCallbackEnv);
        SetThreadpoolCallbackPool(&m_normalPriorityCallbackEnv, m_threadpool.get());
        SetThreadpoolCallbackCleanupGroup(&m_normalPriorityCallbackEnv, m_cleanupGroup.get(), QueueItemCleanup);
        SetThreadpoolCallbackPriority(&m_normalPriorityCallbackEnv, TP_CALLBACK_PRIORITY_NORMAL);

        InitializeThreadpoolEnvironment(&m_highPriorityCallbackEnv);
        SetThreadpoolCallbackPool(&m_highPriorityCallbackEnv, m_threadpool.get());
        SetThreadpoolCallbackCleanupGroup(&m_highPriorityCallbackEnv, m_cleanupGroup.get(), QueueItemCleanup);
        SetThreadpoolCallbackPriority(&m_highPriorityCallbackEnv, TP_CALLBACK_PRIORITY_HIGH);

        THROW_IF_WIN32_BOOL_FALSE(SetThreadpoolThreadMinimum(m_threadpool.get(), 0));
        SetThreadpoolThreadMaximum(m_threadpool.get(), 1);
    }

    void QueueAndWait(std::function<void()> func, bool highPriority)
    {
        auto scopedLock = m_lock.lock_shared();

        THROW_HR_IF(E_ABORT, m_isShuttingDown);

        QueueItem item;
        item.highPriority = highPriority;
        item.queue = this;
        item.operation = std::move(func);

        unique_threadpool_work work(CreateThreadpoolWork(
            [](PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) {
                auto queueItem = reinterpret_cast<QueueItem*>(context);
                try
                {
                    queueItem->queue->AdjustPriorities(queueItem->highPriority);
                    queueItem->operation();
                }
                catch (...)
                {
                    queueItem->m_exception = std::current_exception();
                }
                queueItem->state.store(static_cast<uint32_t>(QueueItemState::Completed), std::memory_order_release);
                queueItem->state.notify_one();
            },
            std::addressof(item),
            highPriority ? &m_highPriorityCallbackEnv : &m_normalPriorityCallbackEnv));
        THROW_IF_NULL_ALLOC(work.get());

        SubmitThreadpoolWork(work.get());

        // Block here until work is complete.
        item.state.wait(0, std::memory_order_acquire);
        if (item.m_exception)
        {
            std::rethrow_exception(item.m_exception);
        }
    }

    template <typename Func>
    auto QueueAndWaitForResult(Func func, bool highPriority) -> decltype(func())
    {
        decltype(func()) returnValue;

        QueueAndWait(
            [&]() {
                returnValue = func();
            },
            highPriority);

        return returnValue;
    }

    void StopAndWait()
    {
        {
            auto scopedLock = m_lock.lock_exclusive();
            m_isShuttingDown = true;
        }

        if (m_cleanupGroup)
        {
            CloseThreadpoolCleanupGroupMembers(m_cleanupGroup.get(), TRUE, nullptr);
            m_cleanupGroup.reset();
        }

        DestroyThreadpoolEnvironment(&m_normalPriorityCallbackEnv);
        DestroyThreadpoolEnvironment(&m_highPriorityCallbackEnv);

        if (m_threadpool)
        {
            m_threadpool.reset();
        }
    }

private:

    void AdjustPriorities(bool /* highPriority */)
    {
        // This is where we'd call out to some helper to adjust priorities for us.
    }

    enum class QueueItemState : uint32_t
    {
        Pending = 0,
        Completed = 1,
        Aborted = 2
    };

    struct QueueItem
    {
        ThreadpoolQueueProcessor* queue{ nullptr };
        std::function<void()> operation;
        std::atomic<uint32_t> state{ 0 };
        std::exception_ptr m_exception;
        bool highPriority{ false };
    };

    static void CALLBACK QueueItemCleanup(PVOID context, PVOID /* cleanupContext */)
    {
        auto item = reinterpret_cast<QueueItem*>(context);
        item->state.store(static_cast<uint32_t>(QueueItemState::Aborted), std::memory_order_release);
        item->state.notify_one();
    }

    wil::srwlock m_lock;
    std::atomic<bool> m_isShuttingDown{ false };
    unique_threadpool m_threadpool;
    unique_threadpool_cleanup_group m_cleanupGroup;
    TP_CALLBACK_ENVIRON m_normalPriorityCallbackEnv{};
    TP_CALLBACK_ENVIRON m_highPriorityCallbackEnv{};
};

int test_queue_processor()
{
    ThreadpoolQueueProcessor processor;
    std::thread threads[25];
    bool thread_threw[25]{};
    std::barrier stop_point(15);

    for (int i = 0; i < std::size(threads); ++i)
    {
        threads[i] = std::thread([&processor, i, &stop_point, &thread_threw]() {
            bool highPriority = (i % 3 == 0);
            try
            {
                processor.QueueAndWait([i, highPriority, &stop_point]() {
                    // Simulate work
                    Sleep(10);
                    std::ignore = stop_point.arrive();
                    }, highPriority);
            }
            catch(wil::ResultException&)
            {
                thread_threw[i] = true;
            }
        });
    }

    stop_point.arrive_and_wait();
    processor.StopAndWait();

    // Wait for all threads to complete
    for (auto& t : threads)
    {
        t.join();
    }

    // Some of them should have throwin.
    auto threw_count = std::count(std::begin(thread_threw), std::end(thread_threw), true);
    std::cout << "Some threads threw: " << (threw_count ? "yes" : "no") << std::endl;
    return 0;
}