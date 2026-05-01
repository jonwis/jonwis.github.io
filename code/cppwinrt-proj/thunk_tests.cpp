#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <span>
#include <array>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

// Pull in the thunked runtime class infrastructure from the same namespace
// We redefine it here to keep the test self-contained.

using winrt::Windows::Foundation::Collections::IPropertySet;
using winrt::Windows::Foundation::Collections::IObservableMap;
using winrt::Windows::Foundation::Collections::IIterable;
using winrt::Windows::Foundation::Collections::IKeyValuePair;
using winrt::Windows::Foundation::Collections::IIterator;
using winrt::Windows::Foundation::Collections::IMap;
using winrt::Windows::Foundation::Collections::IMapView;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IClosable;
using winrt::Windows::Storage::Streams::IRandomAccessStream;
using winrt::Windows::Storage::Streams::IInputStream;
using winrt::Windows::Storage::Streams::IOutputStream;

// ============================================================================
// Thunk infrastructure (matches experiment.cpp's generic_mutating namespace)
// ============================================================================

struct InterfaceThunk
{
    void const* const* vtable;
    void* default_abi;
    std::atomic<void*>* cache_slot;
    GUID const* iid;

    __declspec(noinline) void* resolve() const
    {
        void* current = cache_slot->load(std::memory_order_acquire);
        if (current != static_cast<void const*>(this))
            return current;

        void* real = nullptr;
        winrt::check_hresult(static_cast<::IUnknown*>(default_abi)->QueryInterface(*iid, &real));

        void* expected = const_cast<InterfaceThunk*>(this);
        if (!cache_slot->compare_exchange_strong(expected, real, std::memory_order_release, std::memory_order_acquire))
        {
            static_cast<::IUnknown*>(real)->Release();
            return expected;
        }
        return real;
    }
};

extern "C" void* generic_mutating_resolve_thunk(InterfaceThunk const* thunk)
{
    return thunk->resolve();
}

inline constexpr size_t kMaxVtableSlots = 256;
extern "C" const void* generic_mutating_thunk_vtable[kMaxVtableSlots];

inline void init_thunk(InterfaceThunk& t, void* default_abi, std::atomic<void*>* cache_slot, GUID const* iid)
{
    t.vtable = reinterpret_cast<void const* const*>(generic_mutating_thunk_vtable);
    t.default_abi = default_abi;
    t.cache_slot = cache_slot;
    t.iid = iid;
}

template<size_t N>
struct ThunkedRuntimeClass
{
    mutable std::atomic<void*> cache[N + 1]{};
    mutable InterfaceThunk thunks[N]{};
    GUID const* const* iids_{};

protected:
    ThunkedRuntimeClass(void* default_abi, std::span<GUID const* const, N> iids) : iids_(iids.data()) { attach(default_abi); }
    ThunkedRuntimeClass() = default;

    void attach(void* default_abi)
    {
        cache[0].store(default_abi, std::memory_order_relaxed);
        for (size_t i = 0; i < N; ++i)
        {
            cache[i + 1].store(&thunks[i], std::memory_order_relaxed);
            init_thunk(thunks[i], default_abi, &cache[i + 1], iids_[i]);
        }
    }

    void clear()
    {
        if (auto p = cache[0].exchange(nullptr, std::memory_order_acquire))
            static_cast<::IUnknown*>(p)->Release();
        for (size_t i = 0; i < N; ++i)
        {
            auto p = cache[i + 1].exchange(nullptr, std::memory_order_acquire);
            if (p && p != &thunks[i])
                static_cast<::IUnknown*>(p)->Release();
        }
    }

public:
    ~ThunkedRuntimeClass() { clear(); }

    ThunkedRuntimeClass(ThunkedRuntimeClass const& other) : iids_(other.iids_)
    {
        if (auto p = other.cache[0].load(std::memory_order_relaxed))
        {
            static_cast<::IUnknown*>(p)->AddRef();
            attach(p);
        }
    }

    ThunkedRuntimeClass(ThunkedRuntimeClass&& other) noexcept : iids_(other.iids_)
    {
        auto p = other.cache[0].exchange(nullptr, std::memory_order_acquire);
        if (p) attach(p);
        other.clear();
    }

    ThunkedRuntimeClass& operator=(ThunkedRuntimeClass const& other)
    {
        if (this != &other)
        {
            clear();
            iids_ = other.iids_;
            if (auto p = other.cache[0].load(std::memory_order_relaxed))
            {
                static_cast<::IUnknown*>(p)->AddRef();
                attach(p);
            }
        }
        return *this;
    }

    ThunkedRuntimeClass& operator=(ThunkedRuntimeClass&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            iids_ = other.iids_;
            auto p = other.cache[0].exchange(nullptr, std::memory_order_acquire);
            if (p) attach(p);
            other.clear();
        }
        return *this;
    }

    template<typename T>
    T const& iface(size_t slot) const { return *reinterpret_cast<T const*>(&cache[slot]); }

    explicit operator bool() const noexcept { return cache[0].load(std::memory_order_relaxed) != nullptr; }
};

// ============================================================================
// Thunked PropertySet (3 secondary interfaces)
// ============================================================================

struct PropertySet : protected ThunkedRuntimeClass<3>
{
    static inline const GUID iid_map = winrt::guid_of<IMap<winrt::hstring, IInspectable>>();
    static inline const GUID iid_iterable = winrt::guid_of<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>();
    static inline const GUID iid_observable = winrt::guid_of<IObservableMap<winrt::hstring, IInspectable>>();
    static constexpr GUID const* iids[] = { &iid_map, &iid_iterable, &iid_observable };

    PropertySet() : ThunkedRuntimeClass(winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{}), iids) {}
    PropertySet(nullptr_t) : ThunkedRuntimeClass(nullptr, iids) {}
    PropertySet(PropertySet const&) = default;
    PropertySet(PropertySet&&) noexcept = default;
    PropertySet& operator=(PropertySet const&) = default;
    PropertySet& operator=(PropertySet&&) noexcept = default;
    using ThunkedRuntimeClass::operator bool;

    auto& default_iface() const { return iface<IPropertySet>(0); }
    auto& map_iface() const { return iface<IMap<winrt::hstring, IInspectable>>(1); }
    auto& iterable_iface() const { return iface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(2); }
    auto& observable_iface() const { return iface<IObservableMap<winrt::hstring, IInspectable>>(3); }

    operator IPropertySet const&() const { return default_iface(); }
    operator IMap<winrt::hstring, IInspectable> const&() const { return map_iface(); }
    operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const { return iterable_iface(); }
    operator IObservableMap<winrt::hstring, IInspectable> const&() const { return observable_iface(); }

    template<typename Q> auto as() const { return default_iface().as<Q>(); }
    template<typename Q> auto try_as() const { return default_iface().try_as<Q>(); }

    auto First() const { return iterable_iface().First(); }
    auto Size() const { return map_iface().Size(); }
    auto Clear() const { return map_iface().Clear(); }
    auto GetView() const { return map_iface().GetView(); }
    auto HasKey(winrt::param::hstring key) const { return map_iface().HasKey(key); }
    auto Insert(winrt::param::hstring key, IInspectable const& value) const { return map_iface().Insert(key, value); }
    auto Lookup(winrt::param::hstring key) const { return map_iface().Lookup(key); }
    auto Remove(winrt::param::hstring key) const { return map_iface().Remove(key); }
};

// ============================================================================
// Thunked InMemoryRandomAccessStream (3 secondary: IInputStream, IOutputStream, IClosable)
// ============================================================================

struct InMemoryRandomAccessStream : protected ThunkedRuntimeClass<3>
{
    static inline const GUID iid_input = winrt::guid_of<IInputStream>();
    static inline const GUID iid_output = winrt::guid_of<IOutputStream>();
    static inline const GUID iid_closable = winrt::guid_of<IClosable>();
    static constexpr GUID const* iids[] = { &iid_input, &iid_output, &iid_closable };

    InMemoryRandomAccessStream()
        : ThunkedRuntimeClass(winrt::detach_abi(winrt::Windows::Storage::Streams::InMemoryRandomAccessStream{}), iids) {}
    InMemoryRandomAccessStream(nullptr_t) : ThunkedRuntimeClass(nullptr, iids) {}
    InMemoryRandomAccessStream(InMemoryRandomAccessStream const&) = default;
    InMemoryRandomAccessStream(InMemoryRandomAccessStream&&) noexcept = default;
    InMemoryRandomAccessStream& operator=(InMemoryRandomAccessStream const&) = default;
    InMemoryRandomAccessStream& operator=(InMemoryRandomAccessStream&&) noexcept = default;
    using ThunkedRuntimeClass::operator bool;

    auto& default_iface() const { return iface<IRandomAccessStream>(0); }
    auto& input_iface() const { return iface<IInputStream>(1); }
    auto& output_iface() const { return iface<IOutputStream>(2); }
    auto& closable_iface() const { return iface<IClosable>(3); }

    operator IRandomAccessStream const&() const { return default_iface(); }
    operator IInputStream const&() const { return input_iface(); }
    operator IOutputStream const&() const { return output_iface(); }
    operator IClosable const&() const { return closable_iface(); }

    template<typename Q> auto as() const { return default_iface().as<Q>(); }
    template<typename Q> auto try_as() const { return default_iface().try_as<Q>(); }

    auto Size() const { return default_iface().Size(); }
    void Size(uint64_t value) const { default_iface().Size(value); }
    auto Position() const { return default_iface().Position(); }
    void Seek(uint64_t position) const { default_iface().Seek(position); }
    auto CanRead() const { return default_iface().CanRead(); }
    auto CanWrite() const { return default_iface().CanWrite(); }
    auto CloneStream() const { return default_iface().CloneStream(); }
    auto GetInputStreamAt(uint64_t position) const { return default_iface().GetInputStreamAt(position); }
    auto GetOutputStreamAt(uint64_t position) const { return default_iface().GetOutputStreamAt(position); }
    void Close() const { closable_iface().Close(); }
    auto FlushAsync() const { return output_iface().FlushAsync(); }
};

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
    PropertySet ps;
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
    PropertySet ps;
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
    PropertySet ps(nullptr);
    CHECK(!ps);
}

TEST(PropertySet_CopyConstruct)
{
    PropertySet ps;
    ps.Insert(L"x", winrt::box_value(99));

    PropertySet copy(ps);
    CHECK(copy);
    CHECK(copy.Size() == 1);
    CHECK(copy.HasKey(L"x"));

    // Both point to same underlying object (COM identity)
    copy.Insert(L"y", winrt::box_value(100));
    CHECK(ps.Size() == 2); // shared state
}

TEST(PropertySet_MoveConstruct)
{
    PropertySet ps;
    ps.Insert(L"m", winrt::box_value(7));

    PropertySet moved(std::move(ps));
    CHECK(moved);
    CHECK(moved.Size() == 1);
    CHECK(moved.HasKey(L"m"));
    CHECK(!ps); // source should be empty
}

TEST(PropertySet_CopyAssign)
{
    PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));

    PropertySet other;
    other = ps;
    CHECK(other);
    CHECK(other.Size() == 1);
}

TEST(PropertySet_MoveAssign)
{
    PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));

    PropertySet other;
    other = std::move(ps);
    CHECK(other);
    CHECK(other.Size() == 1);
    CHECK(!ps);
}

TEST(PropertySet_SelfAssign)
{
    PropertySet ps;
    ps.Insert(L"s", winrt::box_value(5));
    auto& ref = ps;
    ps = ref; // self-assign
    CHECK(ps);
    CHECK(ps.Size() == 1);
}

// Pass by const ref — thunks should resolve lazily
void use_propertyset_by_constref(PropertySet const& ps, uint32_t expected_size)
{
    CHECK(ps.Size() == expected_size);
    auto view = ps.GetView();
    CHECK(view.Size() == expected_size);
}

TEST(PropertySet_PassByConstRef)
{
    PropertySet ps;
    ps.Insert(L"k", winrt::box_value(1));
    use_propertyset_by_constref(ps, 1);
}

// Pass by value — triggers copy, thunks reset
void use_propertyset_by_value(PropertySet ps, uint32_t expected_size)
{
    CHECK(ps.Size() == expected_size);
    ps.Insert(L"local_only", winrt::box_value(0));
}

TEST(PropertySet_PassByValue)
{
    PropertySet ps;
    ps.Insert(L"k", winrt::box_value(1));
    use_propertyset_by_value(ps, 1);
    // "local_only" was added through the copy's thunk to the same COM object
    CHECK(ps.Size() == 2);
}

TEST(PropertySet_AsAndTryAs)
{
    PropertySet ps;
    auto inspectable = ps.as<IInspectable>();
    CHECK(inspectable != nullptr);

    auto maybe_map = ps.try_as<IMap<winrt::hstring, IInspectable>>();
    CHECK(maybe_map != nullptr);

    auto maybe_bad = ps.try_as<winrt::Windows::Foundation::IMemoryBuffer>();
    CHECK(maybe_bad == nullptr);
}

// ============================================================================
// InMemoryRandomAccessStream tests
// ============================================================================

TEST(IMRAS_BasicOperations)
{
    InMemoryRandomAccessStream stream;
    CHECK(stream);
    CHECK(stream.Size() == 0);
    CHECK(stream.Position() == 0);
    CHECK(stream.CanRead());
    CHECK(stream.CanWrite());
}

TEST(IMRAS_NullState)
{
    InMemoryRandomAccessStream stream(nullptr);
    CHECK(!stream);
}

TEST(IMRAS_CopyAndMove)
{
    InMemoryRandomAccessStream stream;
    stream.Size(1024);
    CHECK(stream.Size() == 1024);

    InMemoryRandomAccessStream copy(stream);
    CHECK(copy);
    CHECK(copy.Size() == 1024);

    InMemoryRandomAccessStream moved(std::move(stream));
    CHECK(moved);
    CHECK(moved.Size() == 1024);
    CHECK(!stream);
}

TEST(IMRAS_SeekAndPosition)
{
    InMemoryRandomAccessStream stream;
    stream.Size(4096);
    stream.Seek(100);
    CHECK(stream.Position() == 100);
    stream.Seek(0);
    CHECK(stream.Position() == 0);
}

TEST(IMRAS_CloneStream)
{
    InMemoryRandomAccessStream stream;
    stream.Size(512);
    auto clone = stream.CloneStream();
    CHECK(clone != nullptr);
    CHECK(clone.Size() == 512);
}

TEST(IMRAS_GetSubStreams)
{
    InMemoryRandomAccessStream stream;
    auto input = stream.GetInputStreamAt(0);
    CHECK(input != nullptr);
    auto output = stream.GetOutputStreamAt(0);
    CHECK(output != nullptr);
}

TEST(IMRAS_Close)
{
    InMemoryRandomAccessStream stream;
    stream.Close(); // should not throw
    CHECK(true);
}

TEST(IMRAS_FlushAsync)
{
    InMemoryRandomAccessStream stream;
    auto op = stream.FlushAsync();
    auto result = op.get();
    CHECK(true); // didn't throw
}

void use_stream_by_constref(InMemoryRandomAccessStream const& s)
{
    CHECK(s.CanRead());
    CHECK(s.CanWrite());
}

TEST(IMRAS_PassByConstRef)
{
    InMemoryRandomAccessStream stream;
    use_stream_by_constref(stream);
}

void use_stream_by_value(InMemoryRandomAccessStream s)
{
    CHECK(s.CanRead());
    s.Size(999);
}

TEST(IMRAS_PassByValue)
{
    InMemoryRandomAccessStream stream;
    use_stream_by_value(stream);
    CHECK(stream.Size() == 999); // same COM object
}

// ============================================================================
// Thread safety tests
// ============================================================================

TEST(PropertySet_ConcurrentResolve)
{
    // Multiple threads race to resolve thunked interfaces on the same object.
    PropertySet ps;
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

TEST(IMRAS_ConcurrentResolve)
{
    InMemoryRandomAccessStream stream;
    stream.Size(4096);

    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&stream, &errors]() {
            for (int i = 0; i < kIterations; ++i)
            {
                try {
                    auto sz = stream.Size();
                    if (sz != 4096) errors++;

                    auto canRead = stream.CanRead();
                    if (!canRead) errors++;

                    auto canWrite = stream.CanWrite();
                    if (!canWrite) errors++;
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
    PropertySet ps;
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
                    PropertySet local(ps); // copy
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
// Entry point
// ============================================================================

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    std::wcout << L"Running " << tests.size() << L" tests..." << std::endl;

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
    std::wcout << L"Results: " << g_pass << L" passed, " << g_fail << L" failed" << std::endl;

    return g_fail > 0 ? 1 : 0;
}
