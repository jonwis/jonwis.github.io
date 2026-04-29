#pragma once

#include <iostream>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

using winrt::Windows::Foundation::Collections::IPropertySet;
using winrt::Windows::Foundation::Collections::IObservableMap;
using winrt::Windows::Foundation::Collections::IIterable;
using winrt::Windows::Foundation::Collections::IKeyValuePair;
using winrt::Windows::Foundation::Collections::IIterator;
using winrt::Windows::Foundation::Collections::IMap;
using winrt::Windows::Foundation::Collections::IMapView;
using winrt::Windows::Foundation::IStringable;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::IMemoryBuffer;

void consume(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> const& t);
void consume_2(IIterable<IKeyValuePair<winrt::hstring, IInspectable>> t);

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

template<typename T, typename Tuple> struct type_index {
    static_assert(std::tuple_size_v<Tuple> > 0, "Tuple must have at least one type.");
};
template<typename T, typename... Types> struct type_index<T, std::tuple<T, Types...>> {
    static constexpr size_t value = 0;
};
template<typename T, typename U, typename... Types> struct type_index<T, std::tuple<U, Types...>> {
    static constexpr size_t value = 1 + type_index<T, std::tuple<Types...>>::value;
};
template<typename T, typename Tuple> constexpr size_t type_index_v = type_index<T, Tuple>::value;
