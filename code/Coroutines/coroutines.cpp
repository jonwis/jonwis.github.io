#include <windows.h>
#include <unknwn.h>
#include <format>
#include <iostream>
#include <thread>
#include <wil/stl.h>
#include <wil/coroutine.h>
#include <synchapi.h>
#include <wil/coroutine.h>
#include <winrt/Windows.Foundation.h>

struct print_things
{
    print_things()
    {
        printf("Constructor for %p on %lu\n", this, GetCurrentThreadId());
    }

    ~print_things()
    {
        printf("Destructor for %p on %lu\n", this, GetCurrentThreadId());
    }
};

auto resume_on_new_thread() noexcept
{
    struct awaiter
    {
        bool await_ready() const noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) const
        {
            std::thread([handle]() mutable {
                printf("Resuming coroutine on new thread %lu\n", GetCurrentThreadId());
                handle.resume();
            }).detach();
        }

        void await_resume() const noexcept
        {
        }
    };
    return awaiter{};
}

wil::task<uint32_t> example_coroutine()
{
    print_things pt1;
    co_await resume_on_new_thread();
    print_things pt2;
    co_await resume_on_new_thread();
    print_things pt3;
    co_return 42u;
}

winrt::Windows::Foundation::IAsyncAction test_async()
{
    printf("In start thread %lu\n", GetCurrentThreadId());
    co_await winrt::resume_background();
    printf("In background thread %lu\n", GetCurrentThreadId());
}

winrt::Windows::Foundation::IAsyncAction test_fromsta()
{
    printf("In STA thread %lu\n", GetCurrentThreadId());
    co_await test_async();
    printf("Back in STA thread %lu\n", GetCurrentThreadId());
}

void test()
{
    std::thread([] {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        auto q = test_fromsta();
        wil::unique_event evt(wil::EventOptions::ManualReset);
        q.Completed([&evt](auto&&...) {
            evt.SetEvent();
        });
        HANDLE waitable = evt.get();
        HRESULT hr = RPC_S_CALLPENDING;
        while (hr == RPC_S_CALLPENDING)
        {
            DWORD which;
            hr = CoWaitForMultipleHandles(COWAIT_DEFAULT, INFINITE, 1, &waitable, &which);
        }        
    }).join();
}

int main()
{
    test();

    printf("Starting coroutine from thread %lu\n", GetCurrentThreadId());
    auto task = example_coroutine();
    auto result = std::move(task).get();
    printf("Coroutine complete on thread %lu\n", GetCurrentThreadId());
    return 0;
}