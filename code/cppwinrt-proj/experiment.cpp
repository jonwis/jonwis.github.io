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

#include "thunk_experiment.h"

// Define the resolve function exactly once — called from ASM thunk stubs.
extern "C" void* winrt_fast_resolve_thunk(winrt::fast::impl::InterfaceThunk const* thunk)
{
    return thunk->resolve();
}

// Side-by-side comparison functions for disassembly analysis
__declspec(noinline) IMapView<winrt::hstring, IInspectable> create_and_view_thunked()
{
    winrt::Windows::Foundation::Collections::fast::PropertySet ps;
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
    // Run both comparison modes for side-by-side disassembly analysis:
    // 1. winrt::fast::PropertySet — thunk-based caching (ASM stubs)
    // 2. winrt::...::PropertySet — require_one cache (if built with updated cppwinrt)
    // std::wcout << L"=== winrt::fast::PropertySet (thunk cache) ===" << std::endl;
    // comparison<winrt::fast::PropertySet>(); // needs public conversion operators
    std::wcout << L"=== winrt::PropertySet (require_one cache) ===" << std::endl;
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

