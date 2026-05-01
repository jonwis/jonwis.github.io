#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <span>
#include <array>
#include "shared.h"

/*
WinRT interfaces all derive from `IInspectable`. RuntimeClass instances are all composites
of multiple `IInspectable`-based types. Large (or aged) classes like `StorageFile` are composed
of many versioned-interface types, like `IStorageItem`, `IStorageItem2`, `IStorageFile`, `IStorageFile2`,
and many others.

C++/WinRT's projection of `StorageFile` provides a single object with all those interfaces' members
in a single object. A `StorageFile` is-a `IStorageFile*` - class objects point to their default
interface from the metadata.  Calling `StorageFile::MemberOnIStorageFile` has the same cost as
calling any COM vtable-based member.  However, calling `StorageFile::MemberOnIStorageItem2`
requires a `QueryInterface` from `IStorageFile` -> `IStorageItem2`, the method call itself, and
then a `Release` of the returned `IStorageItem2` object.

RuntimeClass Caching aims to solve this problem by modelling a runtimeclass as a set of
interface pointers in the root type.

For this example, we'll use `Windows::Foundation::Collections::PropertySet` - a "marker type"
that only composes other interfaces. The composed interfaces are `IPropertySet` (the default
interface), `IMap<String, Object>`, `IIterable<KeyValuePair<String, Object> >`, `IStringable`,
and `IObservableMap<String, Object>`.
*/

// This model reuses all the cppwinrt base interface functionality, but provides caching of core and related interfaces
// on the type, stored via a tuple.

#include <tuple>

// Template metaprogramming that returns constepxr true if Q is in the tuple of Is, so I can write:
// std::tuple<int, short, long> my_tuple;
// constexpr auto v = tuple_contains_v<my_tuple, long>;
// constexpr auto q = tuple_contains_v<std::tuple<bool, char>, long>;
template<typename Tuple, typename Q>
struct tuple_contains;

template<typename Q>
struct tuple_contains<std::tuple<>, Q> : std::false_type {};

template<typename... Is, typename Q>
struct tuple_contains<std::tuple<Is...>, Q> : std::disjunction<std::is_same<Is, Q>...> {};

template<typename... Is>
struct ComposedRuntimeClass
{
    using storage_t = std::tuple<Is...>;
    using default_iface_t = std::tuple_element_t<0, storage_t>;

    ComposedRuntimeClass()
    {        
    }

    ComposedRuntimeClass(void* abi, winrt::take_ownership_from_abi_t)
    {
        std::get<0>(storage) = IPropertySet{abi, winrt::take_ownership_from_abi};
    }

    template<typename Q> operator Q const&() const
    {
        ensure_interface<Q>();
        return std::get<Q>(storage);
    }

    template<typename Q> Q const& ensure_interface() const
    {
        // Determine if it's the default interface in the tuple and fast-path that; no QI required.
        if constexpr (!std::is_same_v<Q, default_iface_t>)
        {
            auto& storage = std::get<Q>(this->storage);
            if (!storage)
            {
                storage = std::get<0>(this->storage).as<Q>();
            }
            return storage;
        }
        else
        {
            return std::get<0>(storage);
        }
    }

    template<typename Q> auto as() const
    {
        return std::get<0>(storage).as<Q>();
    }

    template<typename Q> auto try_as() const
    {
        return std::get<0>(storage).try_as<Q>();
    }

    template<typename TTarget, typename TCall> __declspec(noinline)  static auto call_facet(IInspectable const& src, TTarget& target, TCall&& call)
    {
        if (!target)
        {
            target = src.as<TTarget>();
        }
        return call(target);
    }

    mutable std::tuple<Is...> storage;
};

namespace use_cached_directly
{
    struct PropertySet : ComposedRuntimeClass<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>
    {
        PropertySet()
        {
            std::get<0>(storage) = winrt::Windows::Foundation::Collections::PropertySet{};
        }

        PropertySet(PropertySet const &t) = default;
        PropertySet(PropertySet&& t) = default;

        explicit PropertySet(void* abi, winrt::take_ownership_from_abi_t ot) : ComposedRuntimeClass(abi, ot)
        {
        }

        using ComposedRuntimeClass::operator=;

        auto First() const
        {
            ensure_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>();
            return std::get<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(storage).First();
        }

        auto Size() const
        {
            ensure_interface<IMap<winrt::hstring, IInspectable>>();
            return std::get<IMap<winrt::hstring, IInspectable>>(storage).Size();
        }

        auto Clear() const
        {
            ensure_interface<IMap<winrt::hstring, IInspectable>>();
            return std::get<IMap<winrt::hstring, IInspectable>>(storage).Clear();
        }

        auto GetView() const
        {
            ensure_interface<IMap<winrt::hstring, IInspectable>>();
            return std::get<IMap<winrt::hstring, IInspectable>>(storage).GetView();
        }

        auto HasKey(winrt::param::hstring key) const
        {
            ensure_interface<IMap<winrt::hstring, IInspectable>>();
            return std::get<IMap<winrt::hstring, IInspectable>>(storage).HasKey(key);
        }

        auto Insert(winrt::param::hstring key, IInspectable const& value) const
        {
            // ensure_interface<IMap<winrt::hstring, IInspectable>>();
            // return std::get<IMap<winrt::hstring, IInspectable>>(storage).Insert(key, value);
            return call_facet(std::get<0>(storage), std::get<IMap<winrt::hstring, IInspectable>>(storage), [&](auto& target) {
                target.Insert(key, value);
            });
        }

        auto Lookup(winrt::param::hstring key) const
        {
            // ensure_interface<IMap<winrt::hstring, IInspectable>>();
            // return std::get<IMap<winrt::hstring, IInspectable>>(storage).Lookup(key);
            return call_facet(std::get<0>(storage), std::get<IMap<winrt::hstring, IInspectable>>(storage), [&](auto& target) {
                return target.Lookup(key);
            });
        }

        auto Remove(winrt::param::hstring key) const
        {
            ensure_interface<IMap<winrt::hstring, IInspectable>>();
            return std::get<IMap<winrt::hstring, IInspectable>>(storage).Remove(key);
        }

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            ensure_interface<IObservableMap<winrt::hstring, IInspectable>>();
            return std::get<IObservableMap<winrt::hstring, IInspectable>>(storage).MapChanged(vhnd);
        }
    };
}

namespace virtual_wrapper
{
    /*
        This generates slightly less-inlined code, but worse than normal cppwinrt  - it does hide the caching logic
        behind a vtable call. Each call site for Insert() looks like:

            35 00000001`400031e6 498b06          mov     rax,qword ptr [r14]
            35 00000001`400031e9 48895de0        mov     qword ptr [rbp-20h],rbx
            35 00000001`400031ed 4c8d4538        lea     r8,[rbp+38h]
            35 00000001`400031f1 488d55e0        lea     rdx,[rbp-20h]
            35 00000001`400031f5 498bce          mov     rcx,r14
            35 00000001`400031f8 ff5050          call    qword ptr [rax+50h]

        Roughly:

        * Load shared_ptr<PropertySetVtable> pointer from 'value like object'
        * Store two parameters on the stack for the call (the key and value for Insert)
        * Store the 'this' pointer in rcx for the vtable call
        * Call the PropertySetVTable::Insert vtable slot
        
        In that vtable method, though, things are pretty good:

        * Inlined "ensure_interface<IMap>()" logic
        * Load of vtable for the real IMap interface
        * Jump to cppwinrt-generated "Insert" method for IMap::Insert
        
        Similarly, the cast operation:

            00000001`40003119 4c8975d0        mov     qword ptr [rbp-30h],r14
            00000001`4000311d 488975d8        mov     qword ptr [rbp-28h],rsi
            00000001`40003121 498b06          mov     rax,qword ptr [r14]
            00000001`40003124 498bce          mov     rcx,r14
            00000001`40003127 ff5018          call    qword ptr [rax+18h]
            00000001`4000312a 488bc8          mov     rcx,rax
            00000001`4000312d e8def7ffff      call    cppwinrt_proj!consume (00000001`40002910)

        * r14 is the 'value like object' pointer, loaded from the caller's stack
        * rsi contains the 'this' ptr for the vtable impl object
        * Load shared_ptr<PropertySetVtable> pointer from 'value like object'
        * Store 'this' pointer in rcx for the vtable call
        * Call the PropertySetVTable::operator IMap<>::() cast method
        * Move the returned IMap pointer from rax into rcx for the consume() call
        * Call consume() with the IMap pointer
    */

    struct PropertySetVTable
    {
        virtual ~PropertySetVTable() = default;
        virtual operator IPropertySet const&() const = 0;
        virtual operator IMap<winrt::hstring, IInspectable> const&() const = 0;
        virtual operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const = 0;
        virtual operator IObservableMap<winrt::hstring, IInspectable> const&() const = 0;

        virtual IIterator<IKeyValuePair<winrt::hstring, IInspectable>> First() const = 0;
        virtual uint32_t Size() const = 0;
        virtual void Clear() const = 0;
        virtual IMapView<winrt::hstring, IInspectable> GetView() const = 0;
        virtual bool HasKey(winrt::param::hstring key) const = 0;
        virtual bool Insert(winrt::param::hstring key, IInspectable const& value) const = 0;
        virtual IInspectable Lookup(winrt::param::hstring key) const = 0;
        virtual void Remove(winrt::param::hstring key) const = 0;
        virtual winrt::event_token MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const = 0;
        virtual void MapChanged(winrt::event_token const& token) const noexcept = 0;
        virtual IInspectable const& DefaultIFace() const = 0;
    };

    struct PropertySetImpl : PropertySetVTable, use_cached_directly::PropertySet
    {
        using use_cached_directly::PropertySet::PropertySet;

        operator IPropertySet const&() const override { return *this; }
        operator IMap<winrt::hstring, IInspectable> const&() const override { return *this; }
        operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const override { return *this; }
        operator IObservableMap<winrt::hstring, IInspectable> const&() const override { return *this; }

        IIterator<IKeyValuePair<winrt::hstring, IInspectable>> First() const override { 
            return ensure_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>().First();
        }
        uint32_t Size() const override {
            return ensure_interface<IMap<winrt::hstring, IInspectable>>().Size(); 
        }
        void Clear() const override { return ensure_interface<IMap<winrt::hstring, IInspectable>>().Clear(); }
        IMapView<winrt::hstring, IInspectable> GetView() const override { return ensure_interface<IMap<winrt::hstring, IInspectable>>().GetView(); }
        bool HasKey(winrt::param::hstring key) const override { 
            return ensure_interface<IMap<winrt::hstring, IInspectable>>().HasKey(key);
        }
        bool Insert(winrt::param::hstring key, IInspectable const& value) const override {
            return ensure_interface<IMap<winrt::hstring, IInspectable>>().Insert(key, value);
        }
        IInspectable Lookup(winrt::param::hstring key) const override {
            return ensure_interface<IMap<winrt::hstring, IInspectable>>().Lookup(key);
        }
        void Remove(winrt::param::hstring key) const override {
            return ensure_interface<IMap<winrt::hstring, IInspectable>>().Remove(key);
        }
        winrt::event_token MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const override { return ensure_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(vhnd); }
        void MapChanged(winrt::event_token const& token) const noexcept override { ensure_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(token); }

        IInspectable const& DefaultIFace() const override { return std::get<0>(storage); }
    };

    struct PropertySet
    {
        std::shared_ptr<PropertySetVTable> impl;
        template<typename... Args> PropertySet(Args&&... args) : impl(std::make_shared<PropertySetImpl>(std::forward<Args>(args)...)) {}
        operator IPropertySet const&() const { return *impl; }
        operator IMap<winrt::hstring, IInspectable> const&() const { return *impl; }
        operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const { return *impl; }
        operator IObservableMap<winrt::hstring, IInspectable> const&() const { return *impl; }
        IIterator<IKeyValuePair<winrt::hstring, IInspectable>> First() const { return impl->First(); }
        uint32_t Size() const { return impl->Size(); }
        void Clear() const { impl->Clear(); }
        IMapView<winrt::hstring, IInspectable> GetView() const { return impl->GetView(); }
        bool HasKey(winrt::param::hstring key) const { return impl->HasKey(static_cast<winrt::hstring const&>(key)); }
        bool Insert(winrt::param::hstring key, IInspectable const& value) const { return impl->Insert(static_cast<winrt::hstring const&>(key), value); }
        IInspectable Lookup(winrt::param::hstring key) const { return impl->Lookup(static_cast<winrt::hstring const&>(key)); }
        void Remove(winrt::param::hstring key) const { impl->Remove(static_cast<winrt::hstring const&>(key)); }
        winrt::event_token MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const { return impl->MapChanged(vhnd); }
        void MapChanged(winrt::event_token const& token) const noexcept { impl->MapChanged(token); }

        template<typename Q> auto as() const { return impl->DefaultIFace().as<Q>(); }
        template<typename Q> auto try_as() const { return impl->DefaultIFace().try_as<Q>(); }
    };
}

namespace cppwinrt_cheese
{
    struct PropertySet : winrt::Windows::Foundation::Collections::PropertySet
    {
        PropertySet() = default;

        auto First() const
        {
            return static_cast<IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&>(*this).First();
        }

        auto Size() const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Size();
        }

        auto Clear() const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Clear();
        }

        auto GetView() const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).GetView();
        }

        auto HasKey(winrt::param::hstring key) const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).HasKey(static_cast<winrt::hstring const&>(key));
        }

        auto Insert(winrt::param::hstring key, IInspectable const& value) const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Insert(static_cast<winrt::hstring const&>(key), value);
        }

        auto Lookup(winrt::param::hstring key) const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Lookup(static_cast<winrt::hstring const&>(key));
        }

        auto Remove(winrt::param::hstring key) const
        {
            return static_cast<IMap<winrt::hstring, IInspectable> const&>(*this).Remove(static_cast<winrt::hstring const&>(key));
        }

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            return static_cast<IObservableMap<winrt::hstring, IInspectable> const&>(*this).MapChanged(vhnd);
        }

        auto MapChanged(winrt::event_token const& token) const noexcept
        {
            return static_cast<IObservableMap<winrt::hstring, IInspectable> const&>(*this).MapChanged(token);
        }
    };
}

namespace another_attempt
{
    /*
        This produces similar codegen to the above; the inliner loves to inline the slot-check-then-call:

            00000001`40003340 488b4dc8        mov     rcx,qword ptr [rbp-38h]
            00000001`40003344 4885c9          test    rcx,rcx
            00000001`40003347 752e            jne     cppwinrt_proj!comparison<another_attempt::PropertySet>+0x97 (00000001`40003377)
            00000001`40003349 c745a86c010000  mov     dword ptr [rbp-58h],16Ch
            00000001`40003350 488975b0        mov     qword ptr [rbp-50h],rsi
            00000001`40003354 488b45b8        mov     rax,qword ptr [rbp-48h]
            00000001`40003358 488b08          mov     rcx,qword ptr [rax]
            00000001`4000335b 488b01          mov     rax,qword ptr [rcx]
            00000001`4000335e 4c8d45c8        lea     r8,[rbp-38h]
            00000001`40003362 488d15e75a0000  lea     rdx,[cppwinrt_proj!winrt::impl::guid_v<winrt::Windows::Foundation::Collections::IIterable<winrt::Windows::Foundation::Collections::IKeyValuePair<winrt::hstring,winrt::Windows::Foundation::IInspectable> > > (00000001`40008e50)]
            00000001`40003369 ff10            call    qword ptr [rax]
            00000001`4000336b 85c0            test    eax,eax
            00000001`4000336d 0f88b7020000    js      cppwinrt_proj!comparison<another_attempt::PropertySet>+0x34a (00000001`4000362a)
            00000001`40003373 488b4dc8        mov     rcx,qword ptr [rbp-38h]
            00000001`40003377 e894f5ffff      call    cppwinrt_proj!consume (00000001`40002910)

        So this is:
        
            * Test the cache slot [rbp-38h] for the requested interface; if it's non-null, jump to the call point
            * If it's null, prepare the parameters for the QueryInterface call:
                * Move the default-interface 'this' pointer from [rbp-40h] into rax
                * Dereference the vtable from rax, then dereference the QueryInterface slot from the vtable into rax
                * Load the address of the cache slot into r8
                * Load the IID of the requested interface into rdx
                * Call the QueryInterface method pointer with those parameters
                * Test the HRESULT for failure and jump to an exit point if it failed
                * Move the returned interface pointer from rax into rcx for the consume() call
            * Call consume() with the requested interface pointer
    */
    
    template<typename... Is> struct InterfaceCacheBlock
    {
        InterfaceCacheBlock(void* abi = nullptr) : cache{abi}
        {
        }

        using type_list_t = std::tuple<Is...>;
        mutable std::array<void*, sizeof...(Is)> cache{nullptr};
        template<typename I> static constexpr size_t index_of = type_index_v<I, type_list_t>;

        template<typename TIface> TIface const& get_interface() const
        {
            return *static_cast<TIface const*>(get_interface_abi(get_default(), cache[index_of<TIface>], winrt::guid_of<TIface>()));
        }

        static void* get_interface_abi(IInspectable const& default_iface, void* &stored, winrt::guid const& iid)
        {
            if (!stored)
            {
                winrt::check_hresult(default_iface.as(iid, &stored));
            }
            return stored;
        }

        auto& get_default() const
        {
            return *static_cast<std::tuple_element_t<0, type_list_t> const*>(cache[0]);
        }
    };

    struct PropertySet : protected InterfaceCacheBlock<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>
    {
    public:
        PropertySet() : InterfaceCacheBlock(winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{}))
        {
        }

        operator IPropertySet const&() const { return get_interface<IPropertySet>(); }
        operator IMap<winrt::hstring, IInspectable> const&() const { return get_interface<IMap<winrt::hstring, IInspectable>>(); }
        operator IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const&() const { return get_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(); }
        operator IObservableMap<winrt::hstring, IInspectable> const&() const { return get_interface<IObservableMap<winrt::hstring, IInspectable>>(); }

        template<typename Q> auto as() const { return get_default().as<Q>(); }
        template<typename Q> auto try_as() const { return get_default().try_as<Q>(); }

        auto First() const
        {
            return get_interface<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>().First();
        }

        auto Size() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Size();
        }

        auto Clear() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Clear();
        }

        auto GetView() const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().GetView();
        }

        auto HasKey(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().HasKey(static_cast<winrt::hstring const&>(key));
        }

        auto Insert(winrt::param::hstring key, IInspectable const& value) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Insert(static_cast<winrt::hstring const&>(key), value);
        }

        auto Lookup(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Lookup(static_cast<winrt::hstring const&>(key));
        }

        auto Remove(winrt::param::hstring key) const
        {
            return get_interface<IMap<winrt::hstring, IInspectable>>().Remove(static_cast<winrt::hstring const&>(key));
        }

        auto MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const
        {
            return get_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(vhnd);
        }

        void MapChanged(winrt::event_token const& token) const noexcept
        {
            get_interface<IObservableMap<winrt::hstring, IInspectable>>().MapChanged(token);
        }
    };
}

#if 0
namespace self_replacing
{
    /*
        This is a sketch of a more complex design that would use self-replacing trampoline objects to cache the interfaces.
        The idea is that the first time an interface is requested, the trampoline queries for it and then replaces itself
        in the cache with the real interface pointer. Subsequent calls would then hit the real interface directly from the cache.

        This design relies on "racy-set" lock-free replacement of the cache slot, which is safe in this scenario. The trampoline type
        is generic. It's passed a raw `void*` abi pointer for the default object, and a reference to the `void*` cache slot, and
        the address of the GUID to query for. Each vtable slot is a thunk that calls a replacement function that does the QI and
        modifies the cache slot.
    */

    __declspec(novtable) struct ManyManyTrampolineVtableSlots :
        winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>,
        winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>,
        winrt::impl::abi_t<IObservableMap<winrt::hstring, IInspectable>>
    {
        virtual IIterator<IKeyValuePair<winrt::hstring, IInspectable>> First() const = 0;
        virtual uint32_t Size() const = 0;
        virtual void Clear() const = 0;
        virtual IMapView<winrt::hstring, IInspectable> GetView() const = 0;
        virtual bool HasKey(winrt::param::hstring key) const = 0;
        virtual bool Insert(winrt::param::hstring key, IInspectable const& value) const = 0;
        virtual IInspectable Lookup(winrt::param::hstring key) const = 0;
        virtual void Remove(winrt::param::hstring key) const = 0;
        virtual winrt::event_token MapChanged(winrt::Windows::Foundation::Collections::MapChangedEventHandler<winrt::hstring, IInspectable> const& vhnd) const = 0;
        virtual void MapChanged(winrt::event_token const& token) const noexcept = 0;
    };

    struct PropertySet
    {
        using tuple_t = std::tuple<IPropertySet, IMap<winrt::hstring, IInspectable>, IIterable<IKeyValuePair<winrt::hstring, IInspectable>>, IObservableMap<winrt::hstring, IInspectable>>;
        tuple_t cache;

        struct trampoline : winrt::implements<trampoline>,
            winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>,
            winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>,
            winrt::impl::abi_t<IObservableMap<winrt::hstring, IInspectable>>
        {
            trampoline(tuple_t& cache) : cache{cache} {}

            tuple_t& cache;

            template<typename T> int32_t upgrade(winrt::impl::abi_t<T>*& slot) const
            {
                auto& default_if = std::get<0>(cache);
                auto& target_if = std::get<T>(cache);
                if (!target_if)
                {
                    auto default_if_abi = static_cast<::IUnknown*>(winrt::get_abi(default_if));
                    int32_t hr = default_if_abi->QueryInterface(winrt::guid_of<T>(), reinterpret_cast<void**>(&target_if));
                    if (FAILED(hr))
                    {
                        return hr;
                    }
                }
                slot = static_cast<winrt::impl::abi_t<T>*>(winrt::get_abi(target_if));
                return S_OK;
            }

            template<typename T, typename TMethod, typename... TArgs>
            int32_t call_and_cache(TMethod method, TArgs&&... args) const
            {
                winrt::impl::abi_t<T>* slot = nullptr;
                int32_t hr = upgrade(&slot);
                if (SUCCEEDED(hr))
                {
                    hr = std::invoke(method, slot, std::forward<TArgs>(args)...);
                }
                return hr;
            }

            int32_t __stdcall winrt::impl::abi_t<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>::First(void** a) noexcept
            {
                return call_and_cache<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>([](auto* slot, void** a) { return slot->First(a); }, a);
            }

            int32_t __stdcall winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>::Size(uint32_t* size) noexcept
            {
                return upgrade<IMap<winrt::hstring, IInspectable>>()->Size(size);
            }

            int32_t __stdcall winrt::impl::abi_t<IMap<winrt::hstring, IInspectable>>::Clear() noexcept
            {
                return upgrade<IMap<winrt::hstring, IInspectable>>()->Clear();
            }

            // Similar for other methods and interfaces...

        };

        PropertySet()
        {
            std::get<IPropertySet>(cache) = winrt::Windows::Foundation::Collections::PropertySet{};
            std::get<IMap<winrt::hstring, IInspectable>>(cache) = ReplacementTrampoline<IMap<winrt::hstring, IInspectable>>(&std::get<IPropertySet>(cache), &std::get<IMap<winrt::hstring, IInspectable>(cache));
            cache[0] = winrt::detach_abi(winrt::Windows::Foundation::Collections::PropertySet{});
            cache[1] = new ReplacementTrampoline<IMap<winrt::hstring, IInspectable>>(&cache[0], &cache[1]);
            cache[2] = new ReplacementTrampoline<IIterable<IKeyValuePair<winrt::hstring, IInspectable>>>(&cache[0], &cache[2]);
            cache[3] = new ReplacementTrampoline<IObservableMap<winrt::hstring, IInspectable>>(&cache[0], &cache[3]);
        }
    };    
}
#endif


#include "thunk_experiment.h"

using namespace generic_mutating;

// Define the resolve function exactly once — called from ASM thunk stubs.
extern "C" void* generic_mutating_resolve_thunk(generic_mutating::InterfaceThunk const* thunk)
{
    return thunk->resolve();
}

// Side-by-side comparison functions for disassembly analysis
__declspec(noinline) IMapView<winrt::hstring, IInspectable> create_and_view_thunked()
{
    generic_mutating::PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));
    ps.Insert(L"b", winrt::box_value(2));
    ps.Insert(L"c", winrt::box_value(3));
    return ps.GetView();
}

__declspec(noinline) IMapView<winrt::hstring, IInspectable> create_and_view_cppwinrt()
{
    winrt::Windows::Foundation::Collections::PropertySet ps;
    ps.Insert(L"a", winrt::box_value(1));
    ps.Insert(L"b", winrt::box_value(2));
    ps.Insert(L"c", winrt::box_value(3));
    return ps.GetView();
}

void thunk_test();

int main()
{
    winrt::init_apartment();
    auto v1 = create_and_view_thunked();
    auto v2 = create_and_view_cppwinrt();
    std::wcout << L"Thunked view size: " << v1.Size() << L", CppWinRT view size: " << v2.Size() << std::endl;
    comparison<use_cached_directly::PropertySet>();
    comparison<cppwinrt_cheese::PropertySet>();
    comparison<virtual_wrapper::PropertySet>();
    comparison<another_attempt::PropertySet>();
    comparison<generic_mutating::PropertySet>();
    comparison<winrt::Windows::Foundation::Collections::PropertySet>();
    thunk_test();
}


void consume(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const& t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

void consume_2(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

