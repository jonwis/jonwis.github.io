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
    secondary (non-default) interfaces. The base owns an array of N+1 atomic cache slots and
    N embedded InterfaceThunk objects. Slot 0 always holds the real default interface pointer.
    Slots 1..N initially point at the corresponding thunk.

    Each InterfaceThunk masquerades as a COM object: its first field is a vtable pointer into
    a shared table of 256 MASM-generated stubs (thunk_stubs.asm). Each stub is 10 bytes:

        mov eax, <slot_index>
        jmp common_thunk_dispatch

    The common dispatch function (also in thunk_stubs.asm) saves the caller's register args,
    calls generic_mutating_resolve_thunk (the C++ resolve helper), then loads the real vtable,
    indexes by the slot number in eax, and tail-jumps to the real method. Total code size for
    256 stubs + dispatch: ~2.6 KB.

    On first call through any method on a thunked interface, resolve() fires:
        1. Atomic-loads the cache slot; if already replaced, returns the real pointer.
        2. QIs the default interface for the target IID.
        3. compare_exchange_strong to swap the thunk pointer for the real one.
            If another thread won the race, releases the duplicate and returns the winner.

    After resolution, the cache slot holds the real COM pointer and all subsequent calls
    dispatch directly through the real vtable — the thunk is never touched again.

    The projected type (e.g. PropertySet) provides typed accessors that reinterpret_cast the
    atomic cache slot as a C++/WinRT projected interface reference. This is safe because
    sizeof(std::atomic<void*>) == sizeof(void*) on x64, and the projected types are just
    thin wrappers around a single interface pointer.

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

    // The thunk object layout - masquerades as a COM interface pointer.
    // 'vtable' is first so &InterfaceThunk looks like a COM object to any caller.
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

    // Declared here, defined in exactly one TU (experiment.cpp).
    // Called from the ASM thunk stubs.
    extern "C" void* generic_mutating_resolve_thunk(InterfaceThunk const* thunk);

    // The thunk vtable: 256 entries defined in thunk_stubs.asm.
    inline constexpr size_t kMaxVtableSlots = 256;
    extern "C" const void* generic_mutating_thunk_vtable[kMaxVtableSlots];

    inline void init_thunk(InterfaceThunk& t, void* default_abi, std::atomic<void*>* cache_slot, GUID const* iid)
    {
        t.vtable = reinterpret_cast<void const* const*>(generic_mutating_thunk_vtable);
        t.default_abi = default_abi;
        t.cache_slot = cache_slot;
        t.iid = iid;
    }

    // Base type for thunked runtime classes. N is the number of secondary (non-default) interfaces.
    // cache[0] holds the default interface; cache[1..N] hold thunks initially, replaced on first use.
    template<size_t N>
    struct ThunkedRuntimeClass
    {
        GUID const* const* iids_{};
        mutable std::atomic<void*> cache[N + 1]{};
        mutable std::array<InterfaceThunk, N> thunks{};

    protected:
        ThunkedRuntimeClass(void* default_abi, std::span<GUID const* const, N> iids)
            : iids_(iids.data())
        {
            attach(default_abi);
        }

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

        ThunkedRuntimeClass(ThunkedRuntimeClass const& other)
            : iids_(other.iids_)
        {
            if (auto p = other.cache[0].load(std::memory_order_relaxed))
            {
                static_cast<::IUnknown*>(p)->AddRef();
                attach(p);
            }
        }

        ThunkedRuntimeClass(ThunkedRuntimeClass&& other) noexcept
            : iids_(other.iids_)
        {
            attach(other.cache[0].exchange(nullptr, std::memory_order_acquire));
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
                attach(other.cache[0].exchange(nullptr, std::memory_order_acquire));
                other.clear();
            }
            return *this;
        }

        template<typename T>
        T const& iface(size_t slot) const
        {
            return *reinterpret_cast<T const*>(&cache[slot]);
        }

        explicit operator bool() const noexcept
        {
            return cache[0].load(std::memory_order_relaxed) != nullptr;
        }
    };

    // ---- PropertySet built on the generic thunk system ----

    struct PropertySet : protected ThunkedRuntimeClass<3>
    {
        static inline const GUID iid_map = winrt::guid_of<IMap<winrt::hstring, IInspectable>>();
        static inline const GUID iid_iterable = winrt::guid_of<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>();
        static inline const GUID iid_observable = winrt::guid_of<IObservableMap<winrt::hstring, IInspectable>>();
        static constexpr GUID const* iids[] = { &iid_map, &iid_iterable, &iid_observable };

        PropertySet()
            : PropertySet(winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{}), winrt::take_ownership_from_abi)
        {
        }

        PropertySet(nullptr_t) : ThunkedRuntimeClass(nullptr, iids) {}
        PropertySet(void* p, winrt::take_ownership_from_abi_t) : ThunkedRuntimeClass(p, iids) {}
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

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            return observable_iface().MapChanged(vhnd);
        }
        void MapChanged(winrt::event_token const& token) const noexcept
        {
            observable_iface().MapChanged(token);
        }
    };

    // ---- InMemoryRandomAccessStream (3 secondary: IInputStream, IOutputStream, IClosable) ----

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
}
