#include <windows.h>
#include <unknwn.h>
#include <iostream>
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

int main()
{
    winrt::init_apartment();
    comparison<use_cached_directly::PropertySet>();
    comparison<virtual_wrapper::PropertySet>();
    comparison<winrt::Windows::Foundation::Collections::PropertySet>();
}


void consume(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const& t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

void consume_2(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

