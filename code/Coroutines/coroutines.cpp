#include <windows.h>
#include <unknwn.h>
#include <format>
#include <iostream>
#include <thread>
#include <wil/stl.h>
#include <wil/coroutine.h>
#include <synchapi.h>
#include <wil/coroutine.h>

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

int main()
{
    printf("Starting coroutine from thread %lu\n", GetCurrentThreadId());
    auto task = example_coroutine();
    auto result = std::move(task).get();
    printf("Coroutine complete on thread %lu\n", GetCurrentThreadId());
    return 0;
}