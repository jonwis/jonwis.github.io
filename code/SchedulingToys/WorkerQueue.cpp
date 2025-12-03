#include <Windows.h>
#include <functional>
#include <atomic>
#include <thread>
#include <barrier>
#include <iostream>
#include <condition_variable>
#include <queue>
#include <shared_mutex>
#include <wil/result.h>
#include <wil/result_macros.h>

using unique_threadpool = wil::unique_any<PTP_POOL, decltype(&CloseThreadpool), CloseThreadpool>;
using unique_threadpool_work = wil::unique_any<PTP_WORK, decltype(&CloseThreadpoolWork), CloseThreadpoolWork>;
using unique_threadpool_cleanup_group = wil::unique_any<PTP_CLEANUP_GROUP, decltype(&CloseThreadpoolCleanupGroup), CloseThreadpoolCleanupGroup>;

// This attempts to be a work queue that has a priority_queue behavior,
// but drives work based on the Windows Threadpool API, specific tp_work
struct WorkerQueue
{
    enum class QueueItemState
    {
        NotStarted,
        Completed,
        Aborted,
    };

    struct WorkItem
    {
        std::function<void()> operation;
        uint32_t priority{ 0 };

        bool operator<(const WorkItem& other) const
        {
            return priority < other.priority;
        }

        void cancel() noexcept
        {
            state.store(QueueItemState::Aborted, std::memory_order_release);
            state.notify_all();
        }

        void wait_for_completion()
        {
            state.wait(QueueItemState::NotStarted, std::memory_order_acquire);
            THROW_HR_IF(E_ABORT, state == QueueItemState::Aborted);
            if (exception)
            {
                throw exception;
            }
        }

        void execute() noexcept
        {
            try
            {
                operation();
            }
            catch (...)
            {
                exception = std::current_exception();
            }
            state.store(QueueItemState::Completed, std::memory_order_release);
            state.notify_one();
        }

    private:
        std::atomic<QueueItemState> state{ QueueItemState::NotStarted };
        std::exception_ptr exception;
    };

    void QueueWork(std::function<void()> func, uint32_t priority)
    {
        // Stick a work record onto the stack, insert its pointer into the queue anywhere
        WorkItem work;
        work.operation = std::move(func);
        work.priority = priority;

        {
            std::lock_guard lock(m_queueMutex);
            THROW_HR_IF(E_ABORT, m_isShuttingDown);
            m_workQueue.push_back(&work);
        }

        // Wake the worker thread to process any pending work
        m_eventWatcher.SetEvent();

        // Wait for the work to finish
        work.wait_for_completion();
    }

    WorkerQueue()
    {
        // Threadpool but only one at a time, please
        m_threadpool.reset(CreateThreadpool(nullptr));
        THROW_IF_NULL_ALLOC(m_threadpool.get());
        THROW_IF_WIN32_BOOL_FALSE(SetThreadpoolThreadMinimum(m_threadpool.get(), 0));
        SetThreadpoolThreadMaximum(m_threadpool.get(), 1);

        // Cleanup group so we can cancel all outstanding work items; Still necessary?
        m_cleanupGroup.reset(CreateThreadpoolCleanupGroup());
        THROW_IF_NULL_ALLOC(m_cleanupGroup.get());
        InitializeThreadpoolEnvironment(&m_callbackEnv);
        SetThreadpoolCallbackPool(&m_callbackEnv, m_threadpool.get());
        SetThreadpoolCallbackCleanupGroup(&m_callbackEnv, m_cleanupGroup.get(), nullptr);

        // Prime a wait object to monitor our event
        m_workEventWaiter.reset(::CreateThreadpoolWait(
            [](PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT, TP_WAIT_RESULT) {
                reinterpret_cast<WorkerQueue*>(context)->ProcessPending();
            },
            this,
            &m_callbackEnv));

        SetThreadpoolWait(m_workEventWaiter.get(), m_workEvent.get(), nullptr);
    }

    void ProcessPending() noexcept
    {
        auto resetter = m_workEvent.ResetEvent_scope_exit();

        while (true)
        {
            WorkItem* workItem = nullptr;
            {
                std::lock_guard lock(m_queueMutex);
                if (m_isShuttingDown || m_workQueue.empty())
                {
                    return;
                }

                // Find first highest priority item
                // Find the highest priority work item
                auto it = std::max_element(m_workQueue.begin(), m_workQueue.end());
                workItem = *it;
                m_workQueue.erase(it);
            }

            // Execute the work item
            workItem->execute();
        }
    }

    void StopAndWait()
    {
        // Mark the queue as stopped and steal all pending work
        std::vector<WorkItem*> workToComplete;
        {
            std::lock_guard lock(m_queueMutex);
            m_isShuttingDown = true;
            workToComplete = std::move(m_workQueue);
        }

        // Wait for any running work to complete by closing the cleanup group, cancelling anything outstanding.
        CloseThreadpoolCleanupGroupMembers(m_cleanupGroup.get(), TRUE, nullptr);
        m_cleanupGroup.reset();
        DestroyThreadpoolEnvironment(&m_callbackEnv);
        m_threadpool.reset();
        
        // Walk the list of work items we snagged and mark them as aborted.
        for (auto& item : workToComplete)
        {
            item->cancel();
        }
    }

    std::shared_mutex m_queueMutex;
    bool m_isShuttingDown{ false };
    std::vector<WorkItem*> m_workQueue;
    unique_threadpool m_threadpool;
    unique_threadpool_cleanup_group m_cleanupGroup;
    wil::unique_event m_workEvent{ wil::EventOptions::None };
    wil::unique_threadpool_wait m_workEventWaiter;
    TP_CALLBACK_ENVIRON m_callbackEnv{};
};