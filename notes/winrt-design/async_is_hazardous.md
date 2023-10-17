# Async is Hazardous

This guideline goes one further than Windows API Design's existing "you can
provide -Async if you want" guidance. Do _not_ define `-Async` methods unless
the underlying operation requires interaction with the user or a network
operation. The overall cost of creating and managing Async threads on a
per-method basis is simply too high compared to the theoretical value of Async
under the covers.

While thread creation on Windows is relatively cheap, management of threading
and movement of work (including coroutines, threadpool queuing, etc.) often
vastly outweighs the actual code being run in the little steps along the way.

## Where to use -Async

There are exactly two places that `-Async` should be used when creating
component interfaces, echoing the existing API design guidance:

-   `Foo.RequestAsync` – the "Request-" verb specifies that user visible
    interaction may be required, and the user is likely to take a long time to
    react.
-   `FooNet.VerbAsync` – the "-Async" here is because the implementation
    operates on a queue of messages bound to a single logical thread of
    execution. The primary case is networking, as network IO a queue of messages
    being passed around. Other work queues include objects bound to a
    Dispatcher.

## What about existing -Async methods?

The Windows API surface is full of -Async methods designed in the "fast and
fluid" era. It's questionable whether the value of that design ever materialized
in a way that was meaningful for users and reasonable in cost for developers.
Practically it produced heavy runtime and disk footprint costs. If your
implementation uses those types, consider these options:

1. **Consider an alternative**. For example, file IO should be performed with
   `CreateFile`/`ReadFile` or `fopen`/`fread` or `std::ifstream`/`getline`. Most
   of the `Windows.Storage` types either have a "get the path and operate on
   that" mode, or they have a synchronous alternative - sometimes on another
   object.
2. **Provide synchronous alternatives**. Find the –Async platform API and offer
   to add a synchronous form. Call the Synchronous form from the Async form
3. **Delete the -Async behavior**. If it's your object and it's not part of the
   Windows API surface, remove the -Async part of the name and the support
   infrastructure that made it asynchronous.
4. **Resume in the completion apartment**. Use `winrt::resume_agile` to
   [reduce movement between threads](https://github.com/microsoft/cppwinrt/pull/1356)
   – simply let your code run in the same thread as the async operation, rather
   than creating a new thread to resume the coroutine step. Make sure you are
   handling locks appropriately.
5. **Block the call**. Simply use `auto x = foo.ReadAsync().get()` to block the
   running thread. Use of `.get()` is a signal that you should investigate
   option #1 above.

## What about UI threads?

Applications still
[need to not block their UI framework threads](https://learn.microsoft.com/windows/uwp/cpp-and-winrt-apis/concurrency-2#programming-with-thread-affinity-in-mind).
Use `co_await winrt::resume_background()` before calling methods that would
block longer than ~50ms. A standard pattern is to split "user interface
operations" from "model and data operations" into separate threads. Handling a
"Click" event by bumping the work to a background thread then rejoining the main
user interface thread to update properties is much easier with C++/WinRT and
coroutines.

You should know which threads are bound to UI elements. Be sure to clearly
document in your ABI or internal object definitions which methods are expected
to take longer so a caller can make a reasonable choice about when to use
"resume background" vs simply run a little more code on the UI thread.

## What about overlapped IO?

As with all performance topics, implement it the simple way first, measure the
result, then optimize the algorithm, measure the result, then optimize the IO
and CPU patterns, measure the result. Have a goal in mind. Do not start with "IO
is expensive, I should use async or overlapped IO just in case."

Actually _implementing_ proper overlapped IO is exceedingly challenging.
Consider the trade-off between simplicity of authoring (fopen + fread + fwrite)
and performance. The Windows scheduler will already allow other threads to
execute while the IO stack is waiting on a completion event for you. If you have
moved your IO off the UI thread (see above and below) then there is basically no
additional cost to not using overlapped IO. The runtime overhead of setting up
the completion frame, configuring the callback handlers, managing completion
state, and interacting with the threadpool's IO features generally greatly
outweighs the cost of normal blocking IO.

## "Reentrancy" using Async

Consider the following XAML event handler:

```c++
void WindowType::MyButton_Clicked(IInspectable const&, IInspectable const&)
{
    winrt::apartment_context ui_thread();
    auto strong = get_strong();
    co_await winrt::resume_background(); // update storage and threading and make other calls
    co_await ui_thread;
    MyButton().Text("Completed");
}
```

A user clicking fast enough will cause parallel execution in the "update storage
and threading" section. To prevent this, be sure to _disable_ MyButton() before
calling "resume_background". You can also use a `wil::unique_semaphore` after
the `winrt::resume_background` and ensure only one thread is executing that
block at a time. Note that `wil::critical_section` and the C++ locking types do
_not_ support this use case; they are thread bound and are exited at coroutine
block boundaries, resulting in potential data store corruption.
