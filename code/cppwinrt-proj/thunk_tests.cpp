#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include "thunk_experiment.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

// ============================================================================
// Test helpers
// ============================================================================

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::wcerr << L"FAIL: " << L#expr << L"  (" << __FILE__ << L":" << __LINE__ << L")" << std::endl; \
        g_fail++; \
    } else { \
        g_pass++; \
    } \
} while(0)

#define TEST(name) static void name(); \
    struct name##_reg { name##_reg() { tests.push_back({L#name, name}); } } name##_inst; \
    static void name()

struct TestEntry { const wchar_t* name; void(*fn)(); };
static std::vector<TestEntry> tests;

// ============================================================================
// PropertySet tests
// ============================================================================

TEST(PropertySet_BasicOperations)
{
    fast::PropertySet ps;
    CHECK(ps);
    CHECK(ps.Size() == 0);

    ps.Insert(L"key1", winrt::box_value(42));
    CHECK(ps.Size() == 1);
    CHECK(ps.HasKey(L"key1"));
    CHECK(!ps.HasKey(L"missing"));

    auto val = winrt::unbox_value<int32_t>(ps.Lookup(L"key1"));
    CHECK(val == 42);

    ps.Insert(L"key2", winrt::box_value(L"hello"));
    CHECK(ps.Size() == 2);

    ps.Remove(L"key1");
    CHECK(ps.Size() == 1);
    CHECK(!ps.HasKey(L"key1"));

    ps.Clear();
    CHECK(ps.Size() == 0);
}

TEST(PropertySet_Iteration)
{
    fast::PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));
    ps.Insert(L"b", winrt::box_value(2));
    ps.Insert(L"c", winrt::box_value(3));

    int count = 0;
    auto it = ps.First();
    while (it.HasCurrent())
    {
        count++;
        it.MoveNext();
    }
    CHECK(count == 3);
}

TEST(PropertySet_NullState)
{
    fast::PropertySet ps(nullptr);
    CHECK(!ps);
}

TEST(PropertySet_CopyConstruct)
{
    fast::PropertySet ps;
    ps.Insert(L"x", winrt::box_value(99));

    fast::PropertySet copy(ps);
    CHECK(copy);
    CHECK(copy.Size() == 1);
    CHECK(copy.HasKey(L"x"));

    // Both point to same underlying object (COM identity)
    copy.Insert(L"y", winrt::box_value(100));
    CHECK(ps.Size() == 2); // shared state
}

TEST(PropertySet_MoveConstruct)
{
    fast::PropertySet ps;
    ps.Insert(L"m", winrt::box_value(7));

    fast::PropertySet moved(std::move(ps));
    CHECK(moved);
    CHECK(moved.Size() == 1);
    CHECK(moved.HasKey(L"m"));
    CHECK(!ps); // source should be empty
}

TEST(PropertySet_CopyAssign)
{
    fast::PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));

    fast::PropertySet other;
    other = ps;
    CHECK(other);
    CHECK(other.Size() == 1);
}

TEST(PropertySet_MoveAssign)
{
    fast::PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));

    fast::PropertySet other;
    other = std::move(ps);
    CHECK(other);
    CHECK(other.Size() == 1);
    CHECK(!ps);
}

TEST(PropertySet_SelfAssign)
{
    fast::PropertySet ps;
    ps.Insert(L"s", winrt::box_value(5));
    auto& ref = ps;
    ps = ref; // self-assign
    CHECK(ps);
    CHECK(ps.Size() == 1);
}

// Pass by const ref — thunks should resolve lazily
void use_propertyset_by_constref(fast::PropertySet const& ps, uint32_t expected_size)
{
    CHECK(ps.Size() == expected_size);
    auto view = ps.GetView();
    CHECK(view.Size() == expected_size);
}

TEST(PropertySet_PassByConstRef)
{
    fast::PropertySet ps;
    ps.Insert(L"k", winrt::box_value(1));
    use_propertyset_by_constref(ps, 1);
}

// Pass by value — triggers copy, thunks reset
void use_propertyset_by_value(fast::PropertySet ps, uint32_t expected_size)
{
    CHECK(ps.Size() == expected_size);
    ps.Insert(L"local_only", winrt::box_value(0));
}

TEST(PropertySet_PassByValue)
{
    fast::PropertySet ps;
    ps.Insert(L"k", winrt::box_value(1));
    use_propertyset_by_value(ps, 1);
    // "local_only" was added through the copy's thunk to the same COM object
    CHECK(ps.Size() == 2);
}

TEST(PropertySet_AsAndTryAs)
{
    fast::PropertySet ps;
    auto inspectable = ps.as<IInspectable>();
    CHECK(inspectable != nullptr);

    auto maybe_map = ps.try_as<IMap<winrt::hstring, IInspectable>>();
    CHECK(maybe_map != nullptr);

    auto maybe_bad = ps.try_as<winrt::Windows::Foundation::IMemoryBuffer>();
    CHECK(maybe_bad == nullptr);
}

// ============================================================================
// Thread safety tests
// ============================================================================

TEST(PropertySet_ConcurrentResolve)
{
    // Multiple threads race to resolve thunked interfaces on the same object.
    fast::PropertySet ps;
    ps.Insert(L"init", winrt::box_value(0));

    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&ps, &errors, t]() {
            for (int i = 0; i < kIterations; ++i)
            {
                try {
                    auto sz = ps.Size();
                    if (sz < 1) errors++;

                    auto view = ps.GetView();
                    if (!view) errors++;

                    auto it = ps.First();
                    if (!it.HasCurrent()) errors++;
                }
                catch (...) {
                    errors++;
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    CHECK(errors.load() == 0);
}

TEST(PropertySet_ConcurrentCopyAndUse)
{
    // Copies made while other threads are resolving interfaces
    fast::PropertySet ps;
    for (int i = 0; i < 10; ++i)
        ps.Insert(winrt::to_hstring(i), winrt::box_value(i));

    constexpr int kThreads = 4;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&ps, &errors]() {
            for (int i = 0; i < 500; ++i)
            {
                try {
                    fast::PropertySet local(ps); // copy
                    auto sz = local.Size();
                    if (sz < 10) errors++;

                    auto it = local.First();
                    int count = 0;
                    while (it.HasCurrent()) { count++; it.MoveNext(); }
                    if (count < 10) errors++;
                }
                catch (...) {
                    errors++;
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    CHECK(errors.load() == 0);
}

// ============================================================================
// Entry point — called from experiment.cpp's main()
// ============================================================================

void thunk_test()
{
    std::wcout << L"Running " << tests.size() << L" thunk tests..." << std::endl;

    for (auto& test : tests)
    {
        std::wcout << L"  " << test.name << L"... ";
        try {
            test.fn();
            std::wcout << L"ok" << std::endl;
        }
        catch (winrt::hresult_error const& e) {
            std::wcerr << L"EXCEPTION: " << e.message().c_str() << std::endl;
            g_fail++;
        }
        catch (std::exception const& e) {
            std::wcerr << L"EXCEPTION: " << e.what() << std::endl;
            g_fail++;
        }
        catch (...) {
            std::wcerr << L"UNKNOWN EXCEPTION" << std::endl;
            g_fail++;
        }
    }

    std::wcout << std::endl;
    std::wcout << L"Thunk test results: " << g_pass << L" passed, " << g_fail << L" failed" << std::endl;
}
