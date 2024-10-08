# WinRT is for ABI Definition

WinRT is an evolution of COM with improved semantics on type structure, async
methods, and a different "base" interface. Its use for cross-DLL interfaces is
excellent and well supported – it provides a stable ABI that can be consumed by
other DLLs, other processes, other languages, and other runtimes. Supporting
those goals are a set of strict rules on how objects are constructed, defined,
instantiated, invoked, and discarded. These rules are compatible with many
languages, but do not cater to any one particular language or runtime.

## Use WinRT at the Boundaries of a Component

WinRT as an ABI system is best used when two DLLs (or processes) interact.
Inside the library and DLL, use C++ types and abstractions – the `std::*`
collections types are very easy to work with and optimized for high performance.

Delay use of WinRT objects as much as possible. Manufacture the appropriate
C++/WinRT objects on demand when returning something to callers, or when calling
into other WinRT types across an ABI.

Consider using C++/WinRT only to implement a thin ABI wrapper around existing
C++ objects. Rather than using C++/WinRT-projected interfaces as your core
implementation currency, have them only be the wrapper layer that adapts your
`MyFooBarImpl` (C++) to `MyFooBar` (WinRT).

## Do not use WinRT for intra-DLL interfaces

Use C++ abstractions within a DLL or library whenever possible.

While it's attractive to use MIDL3 for defining interfaces between components,
the compile-time and runtime overhead of this use case is very high. Component
versioning requires different interfaces, resulting in many interface queries on
the same object to find related interfaces.

The optimizer cannot "see through" C++/WinRT projections into the
implementations, resulting in larger binaries and more expensive runtimes.
Devirtualization and link-time-code-generation are generally defeated when using
WinRT interfaces – even inside a single library.

Language runtime features – like collections – are orders of magnitude faster
than using the equivalent WinRT interfaces. Compare
`std::map<std::wstring, std::wstring>::emplace_back` with
`IMap<String, String>::Insert`. The optimizer is able to directly splat the code
for "emplace back" inline with the caller, occasionally doing
in-place-construction of the map slot key/value pair from an inbound property.

Access patterns are similarly concerning –
`std::find(v.begin(), v.end(), someValue)` is a linear scan of an array compiled
down to a very tight set of register incrementation and comparison calls. Using
`IVector<SomeWinRTValue>::IndexOf` for most collections only does
instance-finding, not semantic "is this value in the collection" lookup.

Instead, consider:

1. Using C++-style `struct IFoo { virtual void MyMethod() = 0; };` style
   interfaces in shared headers
2. Using C++ types with normal virtual methods
3. Using C++ collection types (`map`, `vector`, `wstring`, etc.)

## Use Your Language's Features

The general gist of this section is that your preferred runtime likely has
better support for authoring code than WinRT's "greatest common denominator"
model. Debugging support for WinRT instances is also uneven through natvis and
C# debugger functionality. Using your language directly whenever possible
ensures you get the best possible authoring, documentation, and debugging
support.

Languages like C++ strive for "zero overhead abstraction." Exampes include:

-   Lifecycle – use `std::unique_ptr<>` and `std::shared_ptr<>` and
    `std::weak_ptr<>`
-   Coroutines – use
    [wil::com_task](https://github.com/microsoft/wil/blob/master/include/wil/coroutine.h)
    or [async::task](https://github.com/microsoft/cpp-async) (in preference
    order, for building with Windows)
-   Locking – use `std::mutex` (critical section) or `std::shared_mutex`
    (srwlock)
-   Strings – use `std::wstring`, `std::wstring_view`, `std::string`,
    `std::string_view`, `wil::zstring_view`
-   Collections – use any of the std types (vectors, maps, lists, sets, etc.)

## Convert from WinRT to language types once

When taking in WinRT objects from an ABI call (such as calling the platform, or
being invoked by a caller) convert those types to native language types at most
once, at some form of demarcation line. Native types like `std::wstring` are
much much more well optimized-for in the rest of the STL than operating on
hstring directly – especially for producing new string values through
concatenation, formatting, splitting, etc.
