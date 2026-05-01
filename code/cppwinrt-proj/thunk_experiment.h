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

    A projected runtimeclass derives from ThunkedRuntimeClass<IDefault, I...>.

    Layout (for N = sizeof...(I) secondary interfaces):
        ThunkedRuntimeClassHeader (16 bytes):
            iid_table (8 bytes)          — pointer to static array of IID pointers
            default_cache (8 bytes)      — atomic<void*> holding the default interface pointer
        pairs[0] (24 bytes)              — { atomic<void*> cache, InterfaceThunk thunk }
        pairs[1] (24 bytes)              — ...
        pairs[N-1] (24 bytes)

    Each InterfaceThunk is 16 bytes: { vtable, owner_tagged }.
    owner_tagged packs a back-pointer to the ThunkedRuntimeClassHeader (for QI) and
    the pair index (in the low 3 bits, safe because header is 8-byte aligned).
    The thunk's cache slot is ALWAYS at (this - 8) — the CacheAndThunk layout guarantees this.

    Total per-instance cost for N=3: 16 + 3*24 = 88 bytes.

    Each InterfaceThunk masquerades as a COM object: its first field is a vtable pointer into
    a shared table of 256 MASM-generated stubs (thunk_stubs.asm). Each stub is 10 bytes:

        mov eax, <slot_index>
        jmp common_thunk_dispatch

    The common dispatch function saves the caller's register args, calls
    generic_mutating_resolve_thunk (the C++ resolve helper), loads the real vtable,
    indexes by the slot number in eax, and tail-jumps to the real method.

    On first call through any method on a thunked interface, resolve() fires:
        1. Atomic-loads the cache slot (at this - 8); if already replaced, returns the real pointer.
        2. Unpacks owner_tagged to get the header pointer and pair index.
        3. QIs the default interface (from header) for the target IID (from header's iid_table).
        4. compare_exchange_strong to swap the thunk pointer for the real one.
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

    // Non-templated header at the start of every ThunkedRuntimeClass instance.
    // alignas(16) gives 4 tag bits in tagged mode (bit 0 = flag, bits 1-3 = index).
    struct alignas(16) ThunkedRuntimeClassHeader
    {
        winrt::guid const* const* iid_table{};
        mutable std::atomic<void*> default_cache{};
    };

    // The thunk object layout — masquerades as a COM interface pointer.
    // vtable is first so &InterfaceThunk looks like a COM object to any caller.
    //
    // payload encoding (discriminated by bit 0):
    //   Tagged (bit 0 = 1): header_ptr | (index << 1) | 1.  Header is 16-byte aligned.
    //   Full   (bit 0 = 0): (uintptr_t)default_abi.  COM pointers are 8-byte aligned.
    //                        iid follows at this+16 in the enclosing CacheAndThunkFull.
    struct InterfaceThunk
    {
        void const* const* vtable;
        uintptr_t payload;

        // The cache slot is always the 8 bytes immediately before this thunk
        // in memory (CacheAndThunk layout guarantees this).
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

            void* default_abi;
            winrt::guid const* iid;

            if (payload & 1)
            {
                // Tagged mode: header pointer in upper bits, index in bits 1-3
                auto* hdr = reinterpret_cast<ThunkedRuntimeClassHeader*>(payload & ~uintptr_t(0xF));
                default_abi = hdr->default_cache.load(std::memory_order_relaxed);
                iid = hdr->iid_table[(payload >> 1) & 7];
            }
            else
            {
                // Full mode: payload is default_abi, iid follows thunk in memory
                default_abi = reinterpret_cast<void*>(payload);
                iid = *reinterpret_cast<winrt::guid const* const*>(
                    reinterpret_cast<char const*>(this) + sizeof(InterfaceThunk));
            }

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
    static_assert(sizeof(InterfaceThunk) == 16);

    // Called from the ASM thunk stubs.
    extern "C" void* generic_mutating_resolve_thunk(InterfaceThunk const* thunk);

    // The thunk vtable: 256 entries defined in thunk_stubs.asm.
    inline constexpr size_t kMaxVtableSlots = 256;
    extern "C" const void* generic_mutating_thunk_vtable[kMaxVtableSlots];

    // Tagged pair: compact 24 bytes. Used when N <= 8.
    struct CacheAndThunkTagged
    {
        mutable std::atomic<void*> cache{};
        mutable InterfaceThunk thunk{};
    };
    static_assert(sizeof(CacheAndThunkTagged) == 24);
    static_assert(offsetof(CacheAndThunkTagged, thunk) == sizeof(std::atomic<void*>));

    // Full pair: 32 bytes with explicit iid. Used when N > 8.
    // The iid field is at thunk + sizeof(InterfaceThunk), so resolve() finds it at this+16.
    struct CacheAndThunkFull
    {
        mutable std::atomic<void*> cache{};
        mutable InterfaceThunk thunk{};
        mutable winrt::guid const* iid{};
    };
    static_assert(sizeof(CacheAndThunkFull) == 32);
    static_assert(offsetof(CacheAndThunkFull, thunk) == sizeof(std::atomic<void*>));
    static_assert(offsetof(CacheAndThunkFull, iid) == offsetof(CacheAndThunkFull, thunk) + sizeof(InterfaceThunk));

    inline void init_pair_tagged(CacheAndThunkTagged& p, size_t index, ThunkedRuntimeClassHeader* header)
    {
        p.cache.store(&p.thunk, std::memory_order_relaxed);
        p.thunk.vtable = reinterpret_cast<void const* const*>(generic_mutating_thunk_vtable);
        p.thunk.payload = reinterpret_cast<uintptr_t>(header) | (index << 1) | 1;
    }

    inline void init_pair_full(CacheAndThunkFull& p, void* default_abi, winrt::guid const* iid)
    {
        p.cache.store(&p.thunk, std::memory_order_relaxed);
        p.thunk.vtable = reinterpret_cast<void const* const*>(generic_mutating_thunk_vtable);
        p.thunk.payload = reinterpret_cast<uintptr_t>(default_abi);
        p.iid = iid;
    }

    // Compile-time pair type selection.
    template<bool Tagged> using CacheAndThunkT = std::conditional_t<Tagged, CacheAndThunkTagged, CacheAndThunkFull>;

    // index_of_type takes a type and a variadic list of types and returns the index of the first type that matches.
    template<typename T, typename... Types>
    struct type_index;

    template<typename T, typename... Types>
    struct type_index<T, T, Types...> : std::integral_constant<size_t, 0> {};

    template<typename T, typename U, typename... Types>
    struct type_index<T, U, Types...> : std::integral_constant<size_t, 1 + type_index<T, Types...>::value> {};

    // Non-template base class that implements all COM lifecycle operations.
    // Works on any pair layout via (pointer, count, stride) iteration.
    // Both CacheAndThunkTagged and CacheAndThunkFull start with {atomic<void*>, InterfaceThunk},
    // so the cache is at offset 0 and the thunk is at offset 8 within each pair.
    struct ThunkedRuntimeClassBase : ThunkedRuntimeClassHeader
    {
    protected:
        // Releases default_cache and all resolved secondary interfaces.
        __declspec(noinline) void clear_impl(void* pairs_begin, size_t count, size_t stride)
        {
            if (auto p = default_cache.exchange(nullptr, std::memory_order_acquire))
                static_cast<::IUnknown*>(p)->Release();

            auto* base = static_cast<char*>(pairs_begin);
            for (size_t i = 0; i < count; ++i, base += stride)
            {
                auto& cache = *reinterpret_cast<std::atomic<void*>*>(base);
                auto* thunk = reinterpret_cast<InterfaceThunk*>(base + sizeof(std::atomic<void*>));
                auto p = cache.exchange(nullptr, std::memory_order_acquire);
                if (p && p != thunk)
                    static_cast<::IUnknown*>(p)->Release();
            }
        }

        // Stores default_abi and initializes all pairs.
        __declspec(noinline) void attach_impl(void* default_abi, void* pairs_begin, size_t count, size_t stride, bool tagged)
        {
            default_cache.store(default_abi, std::memory_order_relaxed);
            auto* base = static_cast<char*>(pairs_begin);
            if (tagged)
            {
                for (size_t i = 0; i < count; ++i, base += stride)
                    init_pair_tagged(*reinterpret_cast<CacheAndThunkTagged*>(base), i, this);
            }
            else
            {
                for (size_t i = 0; i < count; ++i, base += stride)
                    init_pair_full(*reinterpret_cast<CacheAndThunkFull*>(base), default_abi, iid_table[i]);
            }
        }

        // Copy from other: AddRef + attach.
        __declspec(noinline) void copy_from(ThunkedRuntimeClassBase const& other, void* pairs_begin, size_t count, size_t stride, bool tagged)
        {
            if (auto p = other.default_cache.load(std::memory_order_relaxed))
            {
                static_cast<::IUnknown*>(p)->AddRef();
                attach_impl(p, pairs_begin, count, stride, tagged);
            }
        }

        // Move from other: steal default + attach, then clear other.
        __declspec(noinline) void move_from(ThunkedRuntimeClassBase& other, void* my_pairs, void* other_pairs, size_t count, size_t stride, bool tagged)
        {
            auto p = other.default_cache.exchange(nullptr, std::memory_order_acquire);
            if (p) attach_impl(p, my_pairs, count, stride, tagged);
            other.clear_impl(other_pairs, count, stride);
        }

        // Copy-assign from other.
        __declspec(noinline) void assign_copy_impl(ThunkedRuntimeClassBase const& other, void* pairs_begin, size_t count, size_t stride, bool tagged)
        {
            if (this != &other)
            {
                clear_impl(pairs_begin, count, stride);
                copy_from(other, pairs_begin, count, stride, tagged);
            }
        }

        // Move-assign from other.
        __declspec(noinline) void assign_move_impl(ThunkedRuntimeClassBase& other, void* my_pairs, void* other_pairs, size_t count, size_t stride, bool tagged)
        {
            if (this != &other)
            {
                clear_impl(my_pairs, count, stride);
                move_from(other, my_pairs, other_pairs, count, stride, tagged);
            }
        }

        template<typename Q> auto as() const { return reinterpret_cast<winrt::Windows::Foundation::IInspectable const*>(&default_cache)->as<Q>(); }
        template<typename Q> auto try_as() const { return reinterpret_cast<winrt::Windows::Foundation::IInspectable const*>(&default_cache)->try_as<Q>(); }

        explicit operator bool() const noexcept
        {
            return default_cache.load(std::memory_order_relaxed) != nullptr;
        }
    };

    // Typed template that adds interface accessors on top of the non-template base.
    // All COM lifecycle operations dispatch to ThunkedRuntimeClassBase, so the compiler
    // generates only one copy regardless of how many ThunkedRuntimeClass instantiations exist.
    template<typename IDefault, typename... I>
    struct ThunkedRuntimeClass : ThunkedRuntimeClassBase
    {
        static constexpr size_t N = sizeof...(I);
        static constexpr bool use_tagged = N <= 8;
        using PairType = CacheAndThunkT<use_tagged>;
        static constexpr size_t pair_stride = sizeof(PairType);

        inline static const std::array<winrt::guid const*, N> iids{ &winrt::guid_of<I>()... };
        mutable std::array<PairType, N> pairs{};

    protected:
        ThunkedRuntimeClass(void* default_abi) { iid_table = iids.data(); attach_impl(default_abi, pairs.data(), N, pair_stride, use_tagged); }
        ThunkedRuntimeClass() { iid_table = iids.data(); }
        void clear() { clear_impl(pairs.data(), N, pair_stride); }

    public:
        ~ThunkedRuntimeClass() { clear(); }
        ThunkedRuntimeClass(ThunkedRuntimeClass const& other) { iid_table = iids.data(); copy_from(other, pairs.data(), N, pair_stride, use_tagged); }
        ThunkedRuntimeClass(ThunkedRuntimeClass&& other) noexcept { iid_table = iids.data(); move_from(other, pairs.data(), other.pairs.data(), N, pair_stride, use_tagged); }
        void assign_copy(ThunkedRuntimeClass const& other) { assign_copy_impl(other, pairs.data(), N, pair_stride, use_tagged); }
        void assign_move(ThunkedRuntimeClass&& other) noexcept { assign_move_impl(other, pairs.data(), other.pairs.data(), N, pair_stride, use_tagged); }

        using ThunkedRuntimeClassBase::operator bool;
        using ThunkedRuntimeClassBase::as;
        using ThunkedRuntimeClassBase::try_as;

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
