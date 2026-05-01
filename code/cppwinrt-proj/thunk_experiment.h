#pragma once

#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include <span>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

/*
    Delay-load style interface caching for WinRT runtime classes.

    A projected runtimeclass derives from ThunkedRuntimeClass<N>, where N is the number of
    secondary (non-default) interfaces.

    Layout (for N secondary interfaces):
        default_cache (8 bytes)      — atomic<void*> holding the default interface pointer
        pairs[0] (32 bytes)          — { atomic<void*> cache, InterfaceThunk thunk }
        pairs[1] (32 bytes)          — ...
        ...
        pairs[N-1] (32 bytes)

    Each InterfaceThunk is 24 bytes: { vtable, default_abi, iid }.
    The thunk's cache slot is ALWAYS at (this - 8) — the CacheAndThunk layout guarantees this.
    This eliminates the need to store a cache_slot pointer in each thunk, saving 8 bytes per
    thunk (24 bytes for N=3) plus removing the iids_ pointer and separate cache array.

    Total per-instance cost for N=3: 8 + 3*32 = 104 bytes

    Each InterfaceThunk masquerades as a COM object: its first field is a vtable pointer into
    a shared table of 256 MASM-generated stubs (thunk_stubs.asm). Each stub is 10 bytes:

        mov eax, <slot_index>
        jmp common_thunk_dispatch

    The common dispatch function saves the caller's register args, calls
    generic_mutating_resolve_thunk (the C++ resolve helper), loads the real vtable,
    indexes by the slot number in eax, and tail-jumps to the real method.

    On first call through any method on a thunked interface, resolve() fires:
        1. Atomic-loads the cache slot (at this - 8); if already replaced, returns the real pointer.
        2. QIs the default interface for the target IID.
        3. compare_exchange_strong to swap the thunk pointer for the real one.
           If another thread won the race, releases the duplicate and returns the winner.

    After resolution, the cache slot holds the real COM pointer and all subsequent calls
    dispatch directly through the real vtable — the thunk is never touched again.

    Copy/move semantics: copy AddRefs the default interface and sets up fresh thunks (secondary
    interfaces are re-QI'd lazily). Move steals the default interface and clears the source.
*/

namespace generic_mutating
{
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

    // The thunk object layout — masquerades as a COM interface pointer.
    // vtable is first so &InterfaceThunk looks like a COM object to any caller.
    // The cache slot is always located at (this - 8) by layout convention.
    struct InterfaceThunk
    {
        void const* const* vtable;
        void* default_abi;
        winrt::guid const* iid;

        // Returns a pointer to the cache slot, which is always the 8 bytes
        // immediately before this thunk in memory (CacheAndThunk layout).
        std::atomic<void*>* cache_slot() const
        {
            return reinterpret_cast<std::atomic<void*>*>(
                const_cast<char*>(reinterpret_cast<char const*>(this)) - sizeof(std::atomic<void*>));
        }

        __declspec(noinline) void* resolve() const
        {
            auto* slot = cache_slot();
            void* current = slot->load(std::memory_order_acquire);
            if (current != static_cast<void const*>(this))
                return current;

            void* real = nullptr;
            winrt::check_hresult(static_cast<::IUnknown*>(default_abi)->QueryInterface(*iid, &real));

            void* expected = const_cast<InterfaceThunk*>(this);
            if (!slot->compare_exchange_strong(expected, real, std::memory_order_release, std::memory_order_acquire))
            {
                static_cast<::IUnknown*>(real)->Release();
                return expected;
            }
            return real;
        }
    };

    // Called from the ASM thunk stubs.
    extern "C" void* generic_mutating_resolve_thunk(InterfaceThunk const* thunk);

    // The thunk vtable: 256 entries defined in thunk_stubs.asm.
    inline constexpr size_t kMaxVtableSlots = 256;
    extern "C" const void* generic_mutating_thunk_vtable[kMaxVtableSlots];

    // A cache slot paired with its thunk. The cache slot is always immediately
    // before the thunk, so the thunk can find it at (this - 8).
    struct CacheAndThunk
    {
        mutable std::atomic<void*> cache{};
        mutable InterfaceThunk thunk{};
    };
    static_assert(offsetof(CacheAndThunk, thunk) == sizeof(std::atomic<void*>));

    inline void init_pair(CacheAndThunk& p, void* default_abi, winrt::guid const* iid)
    {
        p.cache.store(&p.thunk, std::memory_order_relaxed);
        p.thunk.vtable = reinterpret_cast<void const* const*>(generic_mutating_thunk_vtable);
        p.thunk.default_abi = default_abi;
        p.thunk.iid = iid;
    }

    // index_of_type takes a type and a variadic list of types and returns the index of the first type that matches.
    // If the type is not found, the index returned is sizeof...(Types), so it's important to check that the result is less than sizeof...(Types) before using it.
    template<typename T, typename... Types>
    struct type_index;

    template<typename T, typename... Types>
    struct type_index<T, T, Types...> : std::integral_constant<size_t, 0> {};

    template<typename T, typename U, typename... Types>
    struct type_index<T, U, Types...> : std::integral_constant<size_t, 1 + type_index<T, Types...>::value> {};

    // Base type for thunked runtime classes. N is the number of secondary (non-default) interfaces.
    // default_cache holds the default interface; pairs[0..N-1] each hold a cache slot + thunk.
    template<typename IDefault, typename... I>
    struct ThunkedRuntimeClass
    {
        inline static const std::array<winrt::guid const*, sizeof...(I)> iids{ &winrt::guid_of<I>()... };
        mutable std::atomic<void*> default_cache{};
        mutable std::array<CacheAndThunk, sizeof...(I)> pairs{};

    protected:
        ThunkedRuntimeClass(void* default_abi)
        {
            attach(default_abi, iids);
        }

        ThunkedRuntimeClass() = default;

        void attach(void* default_abi, std::span<winrt::guid const* const> iids)
        {
            default_cache.store(default_abi, std::memory_order_relaxed);
            for (size_t i = 0; i < iids.size(); ++i)
                init_pair(pairs[i], default_abi, iids[i]);
        }

        void clear()
        {
            if (auto p = default_cache.exchange(nullptr, std::memory_order_acquire))
                static_cast<::IUnknown*>(p)->Release();
            for (auto& slot : pairs)
            {
                auto p = slot.cache.exchange(nullptr, std::memory_order_acquire);
                if (p && p != &slot.thunk)
                    static_cast<::IUnknown*>(p)->Release();
            }
        }

    public:
        ~ThunkedRuntimeClass() { clear(); }

        // Copy/move require iids from the derived class.
        // Derived types must supply them via a static constexpr iids array.
        ThunkedRuntimeClass(ThunkedRuntimeClass const& other)
        {
            if (auto p = other.default_cache.load(std::memory_order_relaxed))
            {
                static_cast<::IUnknown*>(p)->AddRef();
                attach(p, iids);
            }
        }

        ThunkedRuntimeClass(ThunkedRuntimeClass&& other) noexcept
        {
            auto p = other.default_cache.exchange(nullptr, std::memory_order_acquire);
            if (p) attach(p, iids);
            other.clear();
        }

        ThunkedRuntimeClass& assign_copy(ThunkedRuntimeClass const& other)
        {
            if (this != &other)
            {
                clear();
                if (auto p = other.default_cache.load(std::memory_order_relaxed))
                {
                    static_cast<::IUnknown*>(p)->AddRef();
                    attach(p, iids);
                }
            }
            return *this;
        }

        ThunkedRuntimeClass& assign_move(ThunkedRuntimeClass&& other) noexcept
        {
            if (this != &other)
            {
                clear();
                auto p = other.default_cache.exchange(nullptr, std::memory_order_acquire);
                if (p) attach(p, iids);
                other.clear();
            }
            return *this;
        }

        template<typename Q> auto as() const { return reinterpret_cast<IDefault const*>(&default_cache)->as<Q>(); }
        template<typename Q> auto try_as() const { return reinterpret_cast<IDefault const*>(&default_cache)->try_as<Q>(); }
        operator IDefault const&() const { return *reinterpret_cast<IDefault const*>(&default_cache); }

        template<typename T>
        operator T const&() const
        {
            constexpr size_t iface_index = type_index<T, I...>::value;
            if constexpr (iface_index == sizeof...(I))
            {
                return as<T>();
            }
            else
            {
                return *reinterpret_cast<T const*>(&pairs[iface_index].cache);
            }
        }

        explicit operator bool() const noexcept
        {
            return default_cache.load(std::memory_order_relaxed) != nullptr;
        }
    };

    // ---- PropertySet built on the generic thunk system ----

    struct PropertySet : protected ThunkedRuntimeClass<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>
    {
        PropertySet()
            : PropertySet(winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{}), winrt::take_ownership_from_abi)
        {
        }

        PropertySet(nullptr_t) : ThunkedRuntimeClass(nullptr) {}
        PropertySet(void* p, winrt::take_ownership_from_abi_t) : ThunkedRuntimeClass(p) {}
        PropertySet(PropertySet const& other) : ThunkedRuntimeClass(other) {}
        PropertySet(PropertySet&& other) noexcept : ThunkedRuntimeClass(std::move(other)) {}
        PropertySet& operator=(PropertySet const& other) { assign_copy(other); return *this; }
        PropertySet& operator=(PropertySet&& other) noexcept { assign_move(std::move(other)); return *this; }

        using ThunkedRuntimeClass::operator bool;
        using ThunkedRuntimeClass::as;
        using ThunkedRuntimeClass::try_as;

        auto First() const { return static_cast<IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&>(*this).First(); }
        auto Size() const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Size(); }
        auto Clear() const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Clear(); }
        auto GetView() const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).GetView(); }
        auto HasKey(winrt::param::hstring key) const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).HasKey(static_cast<winrt::hstring const&>(key)); }
        auto Insert(winrt::param::hstring key, IInspectable const& value) const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Insert(static_cast<winrt::hstring const&>(key), value); }
        auto Lookup(winrt::param::hstring key) const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Lookup(static_cast<winrt::hstring const&>(key)); }
        auto Remove(winrt::param::hstring key) const { return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Remove(static_cast<winrt::hstring const&>(key)); }

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            return static_cast<IObservableMap<winrt::hstring, IInspectable> const&>(*this).MapChanged(vhnd);
        }
        void MapChanged(winrt::event_token const& token) const noexcept
        {
            static_cast<IObservableMap<winrt::hstring, IInspectable> const&>(*this).MapChanged(token);
        }
    };

    // ---- InMemoryRandomAccessStream (3 secondary: IInputStream, IOutputStream, IClosable) ----

    struct InMemoryRandomAccessStream : protected ThunkedRuntimeClass<IRandomAccessStream, IInputStream, IOutputStream, IClosable>
    {
        InMemoryRandomAccessStream()
            : ThunkedRuntimeClass(winrt::detach_abi(winrt::Windows::Storage::Streams::InMemoryRandomAccessStream{})) {}
        InMemoryRandomAccessStream(nullptr_t) : ThunkedRuntimeClass(nullptr) {}
        InMemoryRandomAccessStream(InMemoryRandomAccessStream const& other) : ThunkedRuntimeClass(other) {}
        InMemoryRandomAccessStream(InMemoryRandomAccessStream&& other) noexcept : ThunkedRuntimeClass(std::move(other)) {}
        InMemoryRandomAccessStream& operator=(InMemoryRandomAccessStream const& other) { assign_copy(other); return *this; }
        InMemoryRandomAccessStream& operator=(InMemoryRandomAccessStream&& other) noexcept { assign_move(std::move(other)); return *this; }
        using ThunkedRuntimeClass::as;
        using ThunkedRuntimeClass::try_as;
        using ThunkedRuntimeClass::operator bool;

        auto Size() const { return static_cast<IRandomAccessStream const&>(*this).Size(); }
        void Size(uint64_t value) const { static_cast<IRandomAccessStream const&>(*this).Size(value); }
        auto Position() const { return static_cast<IRandomAccessStream const&>(*this).Position(); }
        void Seek(uint64_t position) const { static_cast<IRandomAccessStream const&>(*this).Seek(position); }
        auto CanRead() const { return static_cast<IRandomAccessStream const&>(*this).CanRead(); }
        auto CanWrite() const { return static_cast<IRandomAccessStream const&>(*this).CanWrite(); }
        auto CloneStream() const { return static_cast<IRandomAccessStream const&>(*this).CloneStream(); }
        auto GetInputStreamAt(uint64_t position) const { return static_cast<IRandomAccessStream const&>(*this).GetInputStreamAt(position); }
        auto GetOutputStreamAt(uint64_t position) const { return static_cast<IRandomAccessStream const&>(*this).GetOutputStreamAt(position); }
        void Close() const { static_cast<IClosable const&>(*this).Close(); }
        auto FlushAsync() const { return static_cast<IOutputStream const&>(*this).FlushAsync(); }
    };
}
