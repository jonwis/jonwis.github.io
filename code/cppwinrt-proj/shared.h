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