# Implementing a WinRT object with ATL

Generally, you [should use C++/WinRT](http://github.com/microsoft/cppwinrt) for all new
C++ implementations and uses of WinRT types. Some codebases 
[might use WRL](https://learn.microsoft.com/cpp/cppcx/wrl/windows-runtime-cpp-template-library-wrl?view=msvc-170)
if they can't use C++ exceptions.

Code [that's still on ATL](https://learn.microsoft.com/cpp/atl/atl-com-desktop-components?view=msvc-170)
can still reference WinRT types [through their ABI format](https://www.nuget.org/packages/Microsoft.Windows.AbiWinRT/).

_Implementing_ a WinRT type with ATL is supported - WinRT is just COM with a better type system
and metadata.  WinRT interfaces are multiply derived, such as `IUnknown` <- `IInspectable` <- `IMemoryBuffer`.
ATL requires you list all your base interfaces, as any of those three could be requested from your
implementation object.

Many `runtimeclass` types implement multiple interfaces. See [Windows.Foundation.Collections.StringMap](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.collections.stringmap?view=winrt-26100)
for one example - the `StringMap` type is an `IMap<String, String>` as well as `IEnumerable<KeyValuePair<String, String>>`
(inherited from `IMapView<String, String>`) as well as an `IObservableMap<String, String>` (which inherits from `IMap<String, String>`).
This complicated inheritance hierarchy requires you to specify the path from a more-derived type to its parent type when the parent
type is inherited by multiple bases.

## Example

[CMyWinRTComponent](./SampleObject.cpp) implements `IMemoryBuffer` and `IMemoryBufferReference` using ATL.
As both of these interfaces derive from `IInspectable`, the implementation indicates which one should be
used [with the `COM_INTERFACE_ENTRY2` macro.](https://learn.microsoft.com/cpp/atl/reference/com-interface-entry-macros?view=msvc-170#com_interface_entry2)

```cpp
// Because this object implements IMemoryBuffer and IMemoryBufferReference, both of which derive
// from IInspectable, we specify that requests for IInspectable follow the path through one or
// the other of their common ancestor. Doesn't matter which one, just has to be one of them.
BEGIN_COM_MAP(CMyWinRTComponent)
    COM_INTERFACE_ENTRY(ABI::Windows::Foundation::IMemoryBuffer)
    COM_INTERFACE_ENTRY(ABI::Windows::Foundation::IMemoryBufferReference)
    COM_INTERFACE_ENTRY2(IInspectable, ABI::Windows::Foundation::IMemoryBufferReference)
END_COM_MAP()
```

To find the interface hierarchy, check http://learn.microsoft.com for the interface name.

Or use [Tempo API Viewer](https://apps.microsoft.com/detail/9MX439NHBC9B) which shows the complete set
of implemented interfaces for any Windows type.

1. Load the SLN into Visual Studio 2022 or later (with the ATL and C++ for Desktop Development workloads)
1. Build the SLN
1. Observe that the calls to `QueryInterface` resolve correctly
1. 