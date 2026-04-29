#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

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

using winrt::Windows::Foundation::Collections::IPropertySet;
using winrt::Windows::Foundation::Collections::IObservableMap;
using winrt::Windows::Foundation::Collections::IIterable;
using winrt::Windows::Foundation::Collections::IKeyValuePair;
using winrt::Windows::Foundation::Collections::IMap;
using winrt::Windows::Foundation::IStringable;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IMemoryBuffer;

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

    template<typename Q> void ensure_interface() const
    {
        // Determine if it's the default interface in the tuple and fast-path that; no QI required.
        if constexpr (!std::is_same_v<Q, default_iface_t>)
        {
            auto& storage = std::get<Q>(this->storage);
            if (!storage)
            {
                storage = std::get<0>(this->storage).as<Q>();
            }
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

void consume(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const& t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

void consume_2(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> t)
{
    std::wcout << L"Stringable: " << winrt::get_abi(t.First()) << std::endl;
}

template<typename PSType> int comparison()
{
    PSType ps;
    consume(ps);
    consume(ps);
    consume_2(ps);
    consume_2(ps);
    auto j = ps.try_as<IMemoryBuffer>();

    winrt::hstring key = L"Hello";
    auto value = winrt::box_value(123);

    ps.Insert(key, value);
    ps.Insert(key, value);
    ps.Insert(key, value);
    std::wcout << L"Size: " << ps.Size() << std::endl;

    return 0;
}

int main()
{
    winrt::init_apartment();
    comparison<PropertySet>();
    comparison<winrt::Windows::Foundation::Collections::PropertySet>();
}
